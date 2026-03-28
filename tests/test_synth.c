#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po32.h"
#include "po32_synth.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR 44100
#define MAX_SAMPLES ((size_t)SR * 2u)

static float buf[MAX_SAMPLES];

static float rms(const float *samples, size_t count) {
  double sum = 0;
  size_t i;
  for (i = 0; i < count; i++) sum += (double)samples[i] * samples[i];
  return (float)sqrt(sum / (double)count);
}

static float peak(const float *samples, size_t count) {
  float p = 0;
  size_t i;
  for (i = 0; i < count; i++) {
    float a = fabsf(samples[i]);
    if (a > p) p = a;
  }
  return p;
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

  printf("  kick: len=%zu peak=%.3f front_rms=%.4f tail_rms=%.4f OK\n",
         len, p, r1, r2);
}

static void test_noise_only(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  size_t len = 0;
  float p;

  po32_synth_init(&synth, SR);
  memset(&params, 0, sizeof(params));

  params.Mix = 1.0f;        /* all noise */
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

int main(void) {
  printf("po32 synth tests\n");
  printf("=================\n");

  test_silence();
  test_kick();
  test_noise_only();
  test_level();
  test_duration();
  test_invalid_args();

  printf("=================\n");
  printf("all synth tests passed\n");
  return 0;
}
