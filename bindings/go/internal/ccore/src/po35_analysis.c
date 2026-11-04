#include "po35_analysis.h"
#include "po_math_internal.h"
#include "po_resample_internal.h"
#include "po35_synth_tables.h"

static double absd(double x) {
  return x < 0.0 ? -x : x;
}
static int nearest(double x) {
  return (int)(x < 0.0 ? x - 0.5 : x + 0.5);
}

/* Shared initial/calibrated gain quantization for finite nonnegative amplitudes. */
static uint8_t quantize_gain(double amplitude) {
  double gain = 128.0 * po_math_sqrt(amplitude);
  return gain >= 255.0 ? 255u : (uint8_t)(gain + 0.5);
}

po_status_t po35_analysis_size_for_pcm(size_t count, uint32_t rate, uint32_t fps, size_t *bytes) {
  size_t n;
  if (!bytes)
    return PO_ERR_INVALID_ARG;
  *bytes = 0;
  if (rate < 8000u || rate > 192000u || (fps != 20u && fps != 40u && fps != 60u && fps != 80u))
    return PO_ERR_RANGE;
  if (count / rate > 65535u / fps)
    return PO_ERR_RANGE;
  n = (count / rate) * fps + ((count % rate) * fps + rate - 1u) / rate;
  if (n > 65535u)
    return PO_ERR_RANGE;
  *bytes = n * PO35_ANALYSIS_RECORD_BYTES;
  return PO_OK;
}

static double native_sample(const po35_analysis_workspace_t *w, const float *pcm, size_t count,
                            uint32_t rate, int64_t index) {
  po_resample_kernel_t kernel = {w->resample_cutoff,          w->resample_radius,
                                 w->resample_sin_step,        w->resample_cos_step,
                                 w->resample_window_sin_step, w->resample_window_cos_step};
  return po_resample_f32(&kernel, pcm, count, rate, 24000.0, index);
}

static void setup(po35_analysis_workspace_t *w, uint32_t rate) {
  unsigned i;
  double sum = 0.0;
  po_resample_kernel_t kernel;
  for (i = 0; i < 1024; ++i) {
    double phase = (double)i / 1024.0;
    double sine = po_math_sin(PO_MATH_PI * phase * phase);
    w->window[i] = 1.9074796 * sine * sine;
  }
  /* 49-tap Blackman-windowed lowpass, 3.4 kHz cutoff. */
  for (i = 0; i < 49; ++i) {
    int k = (int)i - 24;
    double taper = 0.42 - 0.5 * po_math_cos(2.0 * PO_MATH_PI * i / 48.0) +
                   0.08 * po_math_cos(4.0 * PO_MATH_PI * i / 48.0);
    double sinc = k ? po_math_sin(2.0 * PO_MATH_PI * (3400.0 / 24000.0) * k) / (PO_MATH_PI * k)
                    : 6800.0 / 24000.0;
    w->decimator[i] = taper * sinc;
    sum += w->decimator[i];
  }
  for (i = 0; i < 49; ++i)
    w->decimator[i] /= sum;
  po_resample_init(&kernel, rate, 24000.0);
  w->resample_cutoff = kernel.cutoff;
  w->resample_radius = kernel.radius;
  w->resample_sin_step = kernel.sin_step;
  w->resample_cos_step = kernel.cos_step;
  w->resample_window_sin_step = kernel.window_sin_step;
  w->resample_window_cos_step = kernel.window_cos_step;
}

static void window_signal(po35_analysis_workspace_t *w, const double *input, unsigned n) {
  double low = 0.0;
  double c = 1.0 - po_math_exp2(-2.0 * PO_MATH_PI * (n == 512 ? 150.0 / 8000.0 : 75.0 / 24000.0) /
                                PO_MATH_LN2);
  for (unsigned i = 0; i < n; ++i) {
    low += (input[i] - low) * c;
    w->filtered[i] = (input[i] - low) * w->window[i * (1024u / n)];
  }
}

static void lpc_bank(po35_analysis_workspace_t *w, const double *input, unsigned n, unsigned order,
                     uint8_t *out) {
  unsigned i, j, lag;
  double error;
  window_signal(w, input, n);
  for (lag = 0; lag <= order; ++lag) {
    double r = 0.0;
    for (i = lag; i < n; ++i)
      r += w->filtered[i] * w->filtered[i - lag];
    w->autocorrelation[lag] = r * po_math_exp2(-0.152003124356 * lag / n);
  }
  error = w->autocorrelation[0];
  w->lpc[0] = 1.0;
  for (i = 1; i <= order; ++i) {
    double k = 0.0;
    if (error > 1e-20) {
      double r = w->autocorrelation[i];
      for (j = 1; j < i; ++j)
        r += w->lpc[j] * w->autocorrelation[i - j];
      k = -r / error;
      if (k > 0.999999)
        k = 0.999999;
      if (k < -0.999999)
        k = -0.999999;
    }
    for (j = 1; j < i; ++j)
      w->temporary[j] = w->lpc[j] + k * w->lpc[i - j];
    for (j = 1; j < i; ++j)
      w->lpc[j] = w->temporary[j];
    w->lpc[i] = k;
    error *= 1.0 - k * k;
  }
  /* Bandwidth expansion applies to the polynomial, then step down to LAR. */
  for (i = 1; i <= order; ++i)
    w->lpc[i] *= po_math_exp2(po_math_log(order == 10 ? 0.9 : 0.8) / PO_MATH_LN2 * i / order);
  for (i = order; i > 0; --i) {
    double k = w->lpc[i];
    if (k > 0.995)
      k = 0.995;
    if (k < -0.995)
      k = -0.995;
    int q = nearest((64.0 / 3.0) * po_math_log((1.0 + k) / (1.0 - k)));
    if (q > 127)
      q = 127;
    /* k >= -0.995 gives an unrounded value >= -127.765, hence q >= -128. */
    out[i - 1] = (uint8_t)q;
    for (j = 1; j < i; ++j)
      w->temporary[j] = (w->lpc[j] - k * w->lpc[i - j]) / (1.0 - k * k);
    for (j = 1; j < i; ++j)
      w->lpc[j] = w->temporary[j];
  }
}

static double pitch(po35_analysis_workspace_t *w, double *periodicity) {
  unsigned i, lag, start = 24, best_lag = 0;
  double r0 = 0.0, best = 0.0, delta = 0.0;
  window_signal(w, w->high, 1024);
  for (i = 0; i < 1024; ++i)
    r0 += w->filtered[i] * w->filtered[i];
  for (lag = 23; lag < 800; ++lag) {
    double cross = 0.0, f = (double)lag / 1024.0;
    for (i = lag; i < 1024; ++i)
      cross += w->filtered[i] * w->filtered[i - lag];
    w->pitch_correlation[lag] =
        1.4 * cross / (0.4 + 0.995899140835 * po_math_exp2(-11.524889946 * f * f / PO_MATH_LN2));
  }
  while (start < 798 && w->pitch_correlation[start] > 0.0)
    ++start;
  for (lag = start; lag < 799; ++lag) {
    double a = w->pitch_correlation[lag - 1], b = w->pitch_correlation[lag],
           c = w->pitch_correlation[lag + 1];
    if (b > 1e-12 && a < b && c < b) {
      double score = b + 0.5 * absd(a - c);
      if (score > best) {
        double denominator = b - (a < c ? a : c);
        best = score;
        best_lag = lag;
        delta = denominator >= 1e-12 ? (c - a) / (2.0 * denominator) : 0.0;
      }
    }
  }
  double uv = r0 > 1e-20 ? 1.14426005 * (1.0 - best / r0) : 1.0;
  if (uv < 0.0)
    uv = 0.0;
  if (uv > 1.0)
    uv = 1.0;
  *periodicity = 1.0 - uv;
  return best_lag ? 24000.0 / (best_lag + delta) : 150.0;
}

/* Stationary power under reconstruction model 1, not original hardware. */
static double output_power(po35_analysis_workspace_t *w, const uint8_t *r, unsigned order,
                           double rate, double f0, double uv) {
  double residual = 1.0, sum = 0.0;
  unsigned i, j, harmonics = (unsigned)(0.5 * rate / f0);
  w->lpc[0] = 1.0;
  for (i = 1; i <= order; ++i) {
    double k = po35_lar_reflection[r[i - 1]];
    residual *= 1.0 - k * k;
    for (j = 1; j < i; ++j)
      w->temporary[j] = w->lpc[j] + k * w->lpc[i - j];
    for (j = 1; j < i; ++j)
      w->lpc[j] = w->temporary[j];
    w->lpc[i] = k;
  }
  if (!harmonics)
    return uv;
  for (i = 1; i <= harmonics; ++i) {
    double angle = 2.0 * PO_MATH_PI * f0 * i / rate;
    double cs = po_math_cos(angle), sn = po_math_sin(angle), re = 1.0, im = 0.0;
    double zr = 1.0, zi = 0.0;
    for (j = 1; j <= order; ++j) {
      double next = zr * cs - zi * sn;
      zi = zr * sn + zi * cs;
      zr = next;
      re += w->lpc[j] * zr;
      im += w->lpc[j] * zi;
    }
    double denominator = re * re + im * im;
    if (denominator < 1e-20)
      denominator = 1e-20;
    sum += 1.0 / denominator;
  }
  return uv + (1.0 - uv) * residual * sum / harmonics;
}

po_status_t po35_analysis_encode_f32(po35_analysis_workspace_t *w, const float *pcm, size_t count,
                                     uint32_t rate, uint32_t fps, uint8_t *records, size_t capacity,
                                     size_t *bytes) {
  size_t required, frame, i;
  po_status_t status;
  if (!bytes)
    return PO_ERR_INVALID_ARG;
  *bytes = 0;
  if (!w || (!pcm && count))
    return PO_ERR_INVALID_ARG;
  status = po35_analysis_size_for_pcm(count, rate, fps, &required);
  if (status != PO_OK)
    return status;
  if (!records && required)
    return PO_ERR_INVALID_ARG;
  if (capacity < required)
    return PO_ERR_BUFFER_TOO_SMALL;
  for (i = 0; i < count; ++i)
    if (!po_math_isfinite(pcm[i]) || absd(pcm[i]) > 1.0)
      return PO_ERR_RANGE;
  if (!required)
    return PO_OK;
  setup(w, rate);
  for (frame = 0; frame < required / 44u; ++frame) {
    size_t start = frame * 24000u / fps, end = (frame + 1u) * 24000u / fps;
    size_t center = (start + end) / 2u;
    uint8_t *r = records + frame * 44u;
    double energy = 0.0, mean = 0.0, periodicity, frequency, rms;
    int q, unvoiced;
    /* Union of the two analysis windows and the widest (20 fps) RMS window. */
    for (i = 0; i < 1672; ++i)
      w->native[i] = native_sample(w, pcm, count, rate, (int64_t)center + (int64_t)i - 1072);
    for (i = start; i < end; ++i)
      mean += w->native[i + 1072 - center];
    mean /= end - start;
    for (i = start; i < end; ++i) {
      double x = w->native[i + 1072 - center] - mean;
      energy += x * x;
    }
    rms = po_math_sqrt(energy / (end - start));
    for (i = 0; i < 1024; ++i)
      w->high[i] = w->native[i + 372];
    for (i = 0; i < 512; ++i) {
      double x = 0.0;
      for (unsigned k = 0; k < 49; ++k)
        x += w->decimator[k] * w->native[3 * i + k];
      w->low[i] = x;
    }
    frequency = pitch(w, &periodicity);
    q = nearest(2048.0 * po_math_log(frequency / 261.6255653005986) / PO_MATH_LN2);
    unvoiced = nearest(255.0 * (1.0 - periodicity));
    r[0] = (uint8_t)q;
    r[1] = (uint8_t)((unsigned)q >> 8);
    r[2] = quantize_gain(rms);
    r[3] = (uint8_t)unvoiced;
    lpc_bank(w, w->low, 512, 10, r + 4);
    lpc_bank(w, w->high, 1024, 30, r + 14);
    frequency = 261.6255653005986 * po_math_exp2((double)q / 2048.0);
    double a = output_power(w, r + 4, 10, 8000.0, frequency, (double)unvoiced / 255.0);
    double b = output_power(w, r + 14, 30, 24000.0, frequency, (double)unvoiced / 255.0);
    double scale = po_math_sqrt(a > b ? a : b);
    if (scale > 1e-12)
      r[2] = quantize_gain(rms / scale);
  }
  *bytes = required;
  return PO_OK;
}
