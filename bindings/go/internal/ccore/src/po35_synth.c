#include "po35_synth.h"
#include "po_math_internal.h"
#include "po35_synth_tables.h"

static int valid_fps(uint32_t fps) {
  return fps == 20u || fps == 40u || fps == 60u || fps == 80u;
}

static uint32_t bank_rate(po35_synth_bank_t bank) {
  if (bank == PO35_SYNTH_BANK_LOW)
    return 8000u;
  if (bank == PO35_SYNTH_BANK_HIGH)
    return 24000u;
  return 0u;
}

static size_t rounded_tail(size_t count, uint32_t rate, uint32_t fps) {
  size_t numerator = (count % fps) * rate;
  size_t tail = numerator / fps;
  size_t remainder = numerator % fps;
  /* Supported rates/fps yield only integral, 1/3 or 2/3 sample boundaries. */
  if (remainder * 2u > fps)
    ++tail;
  return tail;
}

po_status_t po35_synth_init(po35_synth_t *synth, po35_synth_bank_t bank, uint32_t seed) {
  if (!synth)
    return PO32_ERR_INVALID_ARG;
  synth->bank = bank;
  synth->seed = seed;
  return bank_rate(bank) ? PO32_OK : PO32_ERR_RANGE;
}

po_status_t po35_synth_samples_for_records(const po35_synth_t *synth, size_t count, uint32_t fps,
                                           size_t *out) {
  uint32_t rate;
  size_t whole, tail;
  if (!out)
    return PO32_ERR_INVALID_ARG;
  *out = 0;
  if (!synth)
    return PO32_ERR_INVALID_ARG;
  rate = bank_rate(synth->bank);
  if (!rate || !valid_fps(fps))
    return PO32_ERR_RANGE;
  whole = count / fps;
  tail = rounded_tail(count, rate, fps);
  if (whole > ((size_t)-1 - tail) / rate)
    return PO32_ERR_RANGE;
  *out = whole * rate + tail;
  return PO32_OK;
}

static void clear_dsp(po35_synth_voice_t *v) {
  unsigned i;
  v->samples_rendered = 0;
  v->record_index = 0;
  v->record_sample = 0;
  v->record_samples = 0;
  v->rand_state = v->seed ? v->seed : UINT32_C(0x6d2b79f5);
  v->frame_ready = 0;
  v->noise_cached = 0;
  v->status = PO32_OK;
  v->phase = 0.0;
  v->increment = 0.0;
  v->harmonics = 0.0;
  v->periodic_scale = 0.0;
  v->noise_scale = 0.0;
  v->residual_gain = 0.0;
  v->noise_cache = 0.0;
  for (i = 0; i <= PO35_SYNTH_MAX_ORDER; ++i)
    v->lpc[i] = 0.0;
  for (i = 0; i < PO35_SYNTH_MAX_ORDER; ++i) {
    v->history[i] = 0.0;
    v->delay[i] = 0.0;
  }
}

po_status_t po35_synth_voice_init(po35_synth_voice_t *v, const po35_synth_t *synth,
                                  const uint8_t *records, size_t bytes, uint32_t fps) {
  size_t total;
  po_status_t status;
  if (!v)
    return PO32_ERR_INVALID_ARG;
  v->records = NULL;
  v->record_count = 0;
  v->total_samples = 0;
  v->sample_rate = 0;
  v->fps = 0;
  v->seed = 0;
  v->order = 0;
  clear_dsp(v);
  if (!synth || (!records && bytes))
    return PO32_ERR_INVALID_ARG;
  if (bytes % PO35_SYNTH_RECORD_BYTES)
    return PO32_ERR_RANGE;
  status = po35_synth_samples_for_records(synth, bytes / PO35_SYNTH_RECORD_BYTES, fps, &total);
  if (status != PO32_OK)
    return status;
  v->records = records;
  v->record_count = bytes / PO35_SYNTH_RECORD_BYTES;
  v->total_samples = total;
  v->sample_rate = bank_rate(synth->bank);
  v->fps = fps;
  v->seed = synth->seed;
  v->order = synth->bank == PO35_SYNTH_BANK_LOW ? 10u : 30u;
  clear_dsp(v);
  return PO32_OK;
}

void po35_synth_voice_reset(po35_synth_voice_t *v) {
  if (v)
    clear_dsp(v);
}

size_t po35_synth_voice_samples_remaining(const po35_synth_voice_t *v) {
  return v && v->total_samples > v->samples_rendered ? v->total_samples - v->samples_rendered : 0;
}

int po35_synth_voice_done(const po35_synth_voice_t *v) {
  return po35_synth_voice_samples_remaining(v) == 0;
}

static uint32_t random_u32(po35_synth_voice_t *v) {
  uint32_t state = v->rand_state;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  v->rand_state = state;
  return state;
}

static double gaussian(po35_synth_voice_t *v) {
  double x, y, radius, scale;
  if (v->noise_cached) {
    v->noise_cached = 0;
    return v->noise_cache;
  }
  do {
    x = ((double)random_u32(v) + 0.5) / 2147483648.0 - 1.0;
    y = ((double)random_u32(v) + 0.5) / 2147483648.0 - 1.0;
    radius = x * x + y * y;
    /* Each coordinate is an odd multiple of 2^-32, so radius cannot be zero. */
  } while (radius >= 1.0);
  scale = po_math_sqrt(-2.0 * po_math_log(radius) / radius);
  v->noise_cache = y * scale;
  v->noise_cached = 1;
  return x * scale;
}

static void prepare_record(po35_synth_voice_t *v) {
  const uint8_t *r = v->records + v->record_index * PO35_SYNTH_RECORD_BYTES;
  unsigned i, j, offset = v->order == 10u ? 4u : 14u;
  unsigned raw_pitch = (unsigned)r[0] | (unsigned)r[1] << 8;
  int pitch = raw_pitch < 32768u ? (int)raw_pitch : (int)raw_pitch - 65536;
  double residual = 1.0;
  double gain = (double)r[2] / 128.0;
  double unvoiced = (double)r[3] / 255.0;
  double temporary[PO35_SYNTH_MAX_ORDER + 1];
  size_t boundary;
  v->increment = 261.6255653005986 * po_math_exp2((double)pitch / 2048.0) / v->sample_rate;
  if (v->increment > 0.49)
    v->increment = 0.49;
  v->harmonics = po_math_floor(0.5 / v->increment);
  v->periodic_scale = po_math_sqrt(1.0 - unvoiced);
  v->noise_scale = po_math_sqrt(unvoiced);
  v->lpc[0] = 1.0;
  for (i = 1; i <= v->order; ++i) {
    double k = po35_lar_reflection[r[offset + i - 1]];
    residual *= 1.0 - k * k;
    for (j = 1; j < i; ++j)
      temporary[j] = v->lpc[j] + k * v->lpc[i - j];
    for (j = 1; j < i; ++j)
      v->lpc[j] = temporary[j];
    v->lpc[i] = k;
  }
  v->residual_gain = gain * gain * po_math_sqrt(residual);
  /* Initialize transposed filter state from newest-first output history.
   * |reflection| < 1 and order <= 30 bound the LPC L1 norm by 2^30.
   * History contains only committed, finite samples bounded by FLT_MAX,
   * so these sums cannot overflow binary64. */
  for (i = 0; i < v->order; ++i) {
    double state = 0.0;
    for (j = 0; j < v->order - i; ++j)
      state -= v->lpc[i + j + 1] * v->history[j];
    v->delay[i] = state;
  }
  /* Voice initialization checked the final boundary for overflow. Every
   * preceding boundary is no larger because record_index < record_count. */
  boundary = ((v->record_index + 1) / v->fps) * v->sample_rate +
             rounded_tail(v->record_index + 1, v->sample_rate, v->fps);
  v->record_samples = boundary - v->samples_rendered;
  v->record_sample = 0;
  v->frame_ready = 1;
}

static double blit(const po35_synth_voice_t *v, double phase) {
  double odd = 2.0 * v->harmonics + 1.0;
  double pulse;
  if (phase >= 1e-5 && phase < 0.99999) {
    /* Reduce in cycles before converting to radians. At minimum pitch the
       high bank has over three million harmonics. */
    double cycles = odd * phase;
    cycles -= 2.0 * po_math_floor(cycles * 0.5);
    pulse = 0.5 * po_math_sin(PO_MATH_PI * cycles) / po_math_sin(PO_MATH_PI * phase) - 0.5;
  } else {
    double delta = phase >= 0.5 ? phase - 1.0 : phase;
    double theta = delta * 1.8137993;
    pulse = 0.5 * odd * po_math_cos(odd * theta) / po_math_cos(theta) - 0.5;
  }
  return pulse / po_math_sqrt(v->harmonics * 0.5);
}

po_status_t po35_synth_voice_render_f32(po35_synth_voice_t *v, float *out, size_t capacity,
                                        size_t *out_len) {
  size_t n = 0;
  if (!out_len)
    return PO32_ERR_INVALID_ARG;
  *out_len = 0;
  if (!v || (!out && capacity))
    return PO32_ERR_INVALID_ARG;
  if (v->status != PO32_OK)
    return v->status;
  while (n < capacity && v->samples_rendered < v->total_samples) {
    double phase, excitation, sample, noise;
    unsigned i;
    if (!v->frame_ready)
      prepare_record(v);
    phase = v->phase + (double)v->record_sample * v->increment;
    phase -= po_math_floor(phase);
    noise = gaussian(v); /* Consume one sample even for fully voiced/silent frames. */
    excitation = (v->periodic_scale * blit(v, phase) + v->noise_scale * noise) * v->residual_gain;
    sample = excitation + v->delay[0];
    /* Negated ordered comparisons reject NaN as well as infinity/overflow. */
    if (!(sample >= -3.4028234663852886e38 && sample <= 3.4028234663852886e38)) {
      v->status = PO32_ERR_RANGE;
      break;
    }
    /* Each delay is a finite sum of at most order LPC/history products,
     * with the same 2^30 * FLT_MAX bound used by prepare_record. */
    for (i = 0; i < v->order; ++i) {
      double next = i + 1u < v->order ? v->delay[i + 1] : 0.0;
      v->delay[i] = next - v->lpc[i + 1] * sample;
    }
    for (i = v->order - 1; i > 0; --i)
      v->history[i] = v->history[i - 1];
    v->history[0] = sample;
    out[n++] = (float)sample;
    ++v->samples_rendered;
    if (++v->record_sample == v->record_samples) {
      v->phase += (double)v->record_samples * v->increment;
      v->phase -= po_math_floor(v->phase);
      ++v->record_index;
      v->frame_ready = 0;
    }
  }
  *out_len = n;
  return v->status;
}

po_status_t po35_synth_render(const po35_synth_t *synth, const uint8_t *records, size_t bytes,
                              uint32_t fps, float *out, size_t capacity, size_t *out_len) {
  po35_synth_voice_t voice;
  po_status_t status;
  if (!out_len)
    return PO32_ERR_INVALID_ARG;
  *out_len = 0;
  status = po35_synth_voice_init(&voice, synth, records, bytes, fps);
  if (status != PO32_OK)
    return status;
  if (capacity < voice.total_samples)
    return PO32_ERR_BUFFER_TOO_SMALL;
  return po35_synth_voice_render_f32(&voice, out, capacity, out_len);
}
