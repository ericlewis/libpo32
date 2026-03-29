#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po32.h"
#include "po32_synth.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR          44100
#define MAX_SAMPLES ((size_t)SR * 2u)

static float buf[MAX_SAMPLES];
static float aux_a[MAX_SAMPLES];
static float aux_b[MAX_SAMPLES];
static float aux_c[MAX_SAMPLES];
static float aux_d[MAX_SAMPLES];

static float rms(const float *samples, size_t count) {
  double sum = 0;
  size_t i;
  for (i = 0; i < count; i++)
    sum += (double)samples[i] * samples[i];
  return (float)sqrt(sum / (double)count);
}

static float peak(const float *samples, size_t count) {
  float p = 0;
  size_t i;
  for (i = 0; i < count; i++) {
    float a = fabsf(samples[i]);
    if (a > p)
      p = a;
  }
  return p;
}

static float mean_abs_diff(const float *samples, size_t count) {
  double sum = 0.0;
  size_t i;

  if (count < 2u)
    return 0.0f;

  for (i = 1u; i < count; ++i)
    sum += fabsf(samples[i] - samples[i - 1u]);

  return (float)(sum / (double)(count - 1u));
}

static float lag1_autocorrelation(const float *samples, size_t count) {
  double num = 0.0;
  double den = 0.0;
  size_t i;

  if (count < 2u)
    return 0.0f;

  for (i = 1u; i < count; ++i) {
    num += (double)samples[i] * samples[i - 1u];
    den += (double)samples[i] * samples[i];
  }

  return den > 0.0 ? (float)(num / den) : 0.0f;
}

static size_t collect_positive_zero_crossings(const float *samples, size_t count, size_t *out,
                                              size_t out_capacity) {
  size_t found = 0u;
  size_t i;

  for (i = 1u; i < count; ++i) {
    if (samples[i - 1u] <= 0.0f && samples[i] > 0.0f) {
      if (found < out_capacity)
        out[found] = i;
      found++;
    }
  }

  return found;
}

static float mean_positive_crossing_period(const float *samples, size_t count) {
  size_t crossings[256];
  size_t found;
  double sum = 0.0;
  size_t i;

  found = collect_positive_zero_crossings(samples, count, crossings, 256u);
  assert(found >= 2u);

  for (i = 1u; i < found; ++i)
    sum += (double)(crossings[i] - crossings[i - 1u]);

  return (float)(sum / (double)(found - 1u));
}

static float rms_diff(const float *a, const float *b, size_t count) {
  double sum = 0.0;
  size_t i;

  for (i = 0u; i < count; ++i) {
    double delta = (double)a[i] - (double)b[i];
    sum += delta * delta;
  }

  return (float)sqrt(sum / (double)count);
}

static float positive_crossing_period_spread(const float *samples, size_t count) {
  size_t crossings[256];
  size_t found;
  float min_period = 1.0e9f;
  float max_period = 0.0f;
  size_t i;

  found = collect_positive_zero_crossings(samples, count, crossings, 256u);
  assert(found >= 2u);

  for (i = 1u; i < found; ++i) {
    float period = (float)(crossings[i] - crossings[i - 1u]);
    if (period < min_period)
      min_period = period;
    if (period > max_period)
      max_period = period;
  }

  return max_period - min_period;
}

static void test_silence(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  po32_status_t st;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  st = po32_synth_render(&synth, &params, 100, 0.5f, buf, MAX_SAMPLES, &len);
  assert(st == PO32_OK);
  assert(len > 0);
  assert(peak(buf, len) < 0.01f);

  printf("  silence: len=%zu peak=%.4f OK\n", len, peak(buf, len));
}

static void test_kick(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float p, r1, r2;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  /* 808-style kick */
  params.OscWave = 0.0f;
  params.OscFreq = 0.25f;
  params.OscDcy = 0.55f;
  params.ModMode = 0.0f;
  params.ModRate = 0.3f;
  params.ModAmt = 0.3f;
  params.Mix = 0.3f;
  params.Level = 0.836f;

  po32_synth_render(&synth, &params, 100, 0.8f, buf, MAX_SAMPLES, &len);
  assert(len > 0);

  p = peak(buf, len);
  assert(p > 0.1f);

  /* First quarter should be louder than last quarter (decay) */
  r1 = rms(buf, len / 4);
  r2 = rms(buf + len * 3 / 4, len / 4);
  assert(r1 > r2 * 2.0f);

  printf("  kick: len=%zu peak=%.3f front_rms=%.4f tail_rms=%.4f OK\n", len, p, r1, r2);
}

static void test_noise_only(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float p;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.Mix = 1.0f; /* all noise */
  params.NFilFrq = 0.7f;
  params.NEnvDcy = 0.5f;
  params.Level = 0.836f;

  po32_synth_render(&synth, &params, 100, 0.5f, buf, MAX_SAMPLES, &len);
  assert(len > 0);

  p = peak(buf, len);
  assert(p > 0.01f);

  printf("  noise: len=%zu peak=%.3f OK\n", len, p);
}

static void test_level(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));
  params.OscFreq = 0.4f;
  params.OscDcy = 0.5f;

  /* Level = 0 → silence */
  params.Level = 0.0f;
  po32_synth_render(&synth, &params, 100, 0.3f, buf, MAX_SAMPLES, &len);
  assert(peak(buf, len) < 0.001f);

  /* Level = 0.836 → ~unity */
  params.Level = 0.836f;
  po32_synth_render(&synth, &params, 100, 0.3f, buf, MAX_SAMPLES, &len);
  assert(peak(buf, len) > 0.01f);

  printf("  level: zero=silent, unity=audible OK\n");
}

static void test_duration(void) {
  po32_synth_t synth;
  size_t n;

  po32_synth_init(&synth, SR);
  n = po32_synth_samples_for_duration(&synth, 1.0f);
  assert(n == SR);

  n = po32_synth_samples_for_duration(&synth, 0.5f);
  assert(n == SR / 2);

  printf("  duration: sample count OK\n");
}

static void test_invalid_args(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  po32_status_t st;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  st = po32_synth_render(NULL, &params, 100, 0.5f, buf, MAX_SAMPLES, &len);
  assert(st == PO32_ERR_INVALID_ARG);

  st = po32_synth_render(&synth, NULL, 100, 0.5f, buf, MAX_SAMPLES, &len);
  assert(st == PO32_ERR_INVALID_ARG);

  st = po32_synth_render(&synth, &params, 100, 0.5f, NULL, MAX_SAMPLES, &len);
  assert(st == PO32_ERR_INVALID_ARG);

  printf("  invalid_args: NULL checks OK\n");
}

static void test_alternate_modes(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float p;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.OscWave = 0.5f; /* triangle */
  params.OscFreq = 0.35f;
  params.OscAtk = 0.3f;
  params.OscDcy = 0.4f;
  params.ModMode = 0.5f; /* sine */
  params.ModRate = 0.2f;
  params.ModAmt = 0.7f;
  params.NFilMod = 0.5f; /* band-pass */
  params.NFilFrq = 0.6f;
  params.NFilQ = 0.4f;
  params.NEnvMod = 0.5f; /* linear */
  params.NEnvAtk = 0.1f;
  params.NEnvDcy = 0.4f;
  params.Mix = 0.7f;
  params.DistAmt = 0.4f;
  params.EQFreq = 0.6f;
  params.EQGain = 0.8f;
  params.Level = 0.836f;

  assert(po32_synth_render(&synth, &params, 100, 0.5f, buf, MAX_SAMPLES, &len) == PO32_OK);
  assert(len > 0);
  p = peak(buf, len);
  assert(p > 0.01f);

  printf("  alternate_modes: triangle/sine/bp/linear peak=%.3f OK\n", p);
}

static void test_noise_mod_and_mod_env(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float p;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.OscWave = 1.0f; /* saw */
  params.OscFreq = 0.45f;
  params.OscDcy = 0.5f;
  params.ModMode = 1.0f; /* noise */
  params.ModRate = 0.6f;
  params.ModAmt = 0.65f;
  params.NFilMod = 1.0f; /* high-pass */
  params.NFilFrq = 0.8f;
  params.NFilQ = 0.6f;
  params.NEnvMod = 1.0f; /* mod */
  params.NEnvDcy = 0.55f;
  params.Mix = 0.9f;
  params.DistAmt = 0.2f;
  params.EQFreq = 0.7f;
  params.EQGain = 0.2f;
  params.Level = 0.836f;

  assert(po32_synth_render(&synth, &params, 127, 0.6f, buf, MAX_SAMPLES, &len) == PO32_OK);
  assert(len > 0);
  p = peak(buf, len);
  assert(p > 0.01f);

  printf("  noise_mod_env: saw/noise/hp/mod peak=%.3f OK\n", p);
}

static void test_waveform_modes_are_distinct(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len_a = 0;
  size_t len_b = 0;
  size_t len_c = 0;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.Mix = 0.0f;
  params.OscFreq = 0.1f;
  params.OscDcy = 0.8f;
  params.Level = 0.836f;

  params.OscWave = 0.0f;
  assert(po32_synth_render(&synth, &params, 100, 0.1f, aux_a, MAX_SAMPLES, &len_a) == PO32_OK);
  params.OscWave = 0.5f;
  assert(po32_synth_render(&synth, &params, 100, 0.1f, aux_b, MAX_SAMPLES, &len_b) == PO32_OK);
  params.OscWave = 1.0f;
  assert(po32_synth_render(&synth, &params, 100, 0.1f, aux_c, MAX_SAMPLES, &len_c) == PO32_OK);

  assert(len_a == len_b);
  assert(len_b == len_c);
  assert(aux_a[0] > 0.0f && aux_a[0] < 0.1f);
  assert(aux_b[0] > 0.9f);
  assert(aux_c[0] < -0.9f);

  printf("  waveform_modes: sine=%.4f triangle=%.4f saw=%.4f OK\n", aux_a[0], aux_b[0], aux_c[0]);
}

static void test_filter_modes_have_distinct_roughness(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float rough_lp, rough_hp;
  float lag_lp, lag_bp, lag_hp;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.Mix = 1.0f;
  params.NFilFrq = 0.35f;
  params.NFilQ = 0.45f;
  params.NEnvDcy = 0.5f;
  params.Level = 0.836f;

  params.NFilMod = 0.0f;
  assert(po32_synth_render(&synth, &params, 100, 0.25f, aux_a, MAX_SAMPLES, &len) == PO32_OK);
  rough_lp = mean_abs_diff(aux_a, len);
  lag_lp = lag1_autocorrelation(aux_a, len);

  params.NFilMod = 0.5f;
  assert(po32_synth_render(&synth, &params, 100, 0.25f, aux_b, MAX_SAMPLES, &len) == PO32_OK);
  lag_bp = lag1_autocorrelation(aux_b, len);

  params.NFilMod = 1.0f;
  assert(po32_synth_render(&synth, &params, 100, 0.25f, aux_c, MAX_SAMPLES, &len) == PO32_OK);
  rough_hp = mean_abs_diff(aux_c, len);
  lag_hp = lag1_autocorrelation(aux_c, len);

  assert(lag_lp > lag_bp);
  assert(lag_bp > lag_hp);
  assert(rough_hp > rough_lp * 10.0f);

  printf("  filter_modes: lag lp=%.4f bp=%.4f hp=%.4f rough lp=%.4f hp=%.4f OK\n", lag_lp, lag_bp,
         lag_hp, rough_lp, rough_hp);
}

static void test_pitch_mod_modes_have_distinct_period_behavior(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float decay_early_period, decay_late_period;
  float sine_rms_delta;
  float base_period_spread, sine_period_spread, noise_period_spread;
  size_t early_span = SR / 8u;
  size_t late_start = SR / 3u;
  size_t late_span = SR / 3u;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.Mix = 0.0f;
  params.OscWave = 0.0f;
  params.OscFreq = 0.2f;
  params.OscDcy = 0.9f;
  params.Level = 0.836f;
  params.ModAmt = 0.7f;

  params.ModMode = 0.0f;
  params.ModRate = 0.2f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_a, MAX_SAMPLES, &len) == PO32_OK);
  decay_early_period = mean_positive_crossing_period(aux_a, early_span);
  decay_late_period = mean_positive_crossing_period(aux_a + late_start, SR / 8u);
  assert(decay_early_period < decay_late_period - 10.0f);

  params.ModAmt = 0.5f;
  params.ModMode = 0.0f;
  params.ModRate = 0.2f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_b, MAX_SAMPLES, &len) == PO32_OK);
  base_period_spread = positive_crossing_period_spread(aux_b + late_start, late_span);

  params.ModAmt = 0.7f;
  params.ModMode = 0.5f;
  params.ModRate = 0.05f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_c, MAX_SAMPLES, &len) == PO32_OK);
  sine_period_spread = positive_crossing_period_spread(aux_c + late_start, late_span);
  sine_rms_delta = rms_diff(aux_b, aux_c, len);

  params.ModMode = 1.0f;
  params.ModRate = 0.1f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_d, MAX_SAMPLES, &len) == PO32_OK);
  noise_period_spread = positive_crossing_period_spread(aux_d + late_start, late_span);

  assert(sine_rms_delta > 0.1f);
  assert(noise_period_spread > sine_period_spread * 10.0f);

  printf("  pitch_mod_modes: decay_early=%.2f decay_late=%.2f base=%.2f sine=%.2f noise=%.2f "
         "delta=%.3f OK\n",
         decay_early_period, decay_late_period, base_period_spread, sine_period_spread,
         noise_period_spread, sine_rms_delta);
}

static void test_noise_envelopes_have_distinct_shapes(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  size_t early_window = SR / 100u;
  float exp_early_rms, lin_early_rms, mod_early_rms;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.Mix = 1.0f;
  params.NFilFrq = 0.55f;
  params.NFilQ = 0.35f;
  params.NEnvAtk = 0.4f;
  params.NEnvDcy = 0.45f;
  params.Level = 0.836f;

  params.NEnvMod = 0.0f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_a, MAX_SAMPLES, &len) == PO32_OK);

  params.NEnvMod = 0.5f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_b, MAX_SAMPLES, &len) == PO32_OK);

  params.NEnvMod = 1.0f;
  assert(po32_synth_render(&synth, &params, 100, 0.6f, aux_c, MAX_SAMPLES, &len) == PO32_OK);

  assert(aux_b[0] == 0.0f);
  assert(fabsf(aux_a[0]) > 1.0e-6f);
  assert(fabsf(aux_c[0]) > fabsf(aux_a[0]) * 100.0f);

  exp_early_rms = rms(aux_a, early_window);
  lin_early_rms = rms(aux_b, early_window);
  mod_early_rms = rms(aux_c, early_window);

  assert(lin_early_rms > exp_early_rms * 10.0f);
  assert(mod_early_rms > lin_early_rms * 4.0f);

  printf("  noise_env_modes: exp=%.6f lin=%.6f mod=%.6f early_rms OK\n", exp_early_rms,
         lin_early_rms, mod_early_rms);
}

int main(void) {
  printf("po32 synth tests\n");
  printf("=================\n");

  test_silence();
  test_kick();
  test_noise_only();
  test_level();
  test_duration();
  test_invalid_args();
  test_alternate_modes();
  test_noise_mod_and_mod_env();
  test_waveform_modes_are_distinct();
  test_filter_modes_have_distinct_roughness();
  test_pitch_mod_modes_have_distinct_period_behavior();
  test_noise_envelopes_have_distinct_shapes();

  printf("=================\n");
  printf("all synth tests passed\n");
  return 0;
}
