#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po35_synth.h"
#include "../src/po_math_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint8_t records[44 * 1000];
static float audio[240000];
static float chunks[240000];

static void make_record(uint8_t *r, int pitch, unsigned gain, unsigned noise) {
  memset(r, 0, 44);
  r[0] = (uint8_t)pitch;
  r[1] = (uint8_t)((unsigned)pitch >> 8);
  r[2] = (uint8_t)gain;
  r[3] = (uint8_t)noise;
}

static void test_arguments_and_timing(void) {
  po35_synth_t s;
  po35_synth_voice_t v;
  size_t n = 99, i;
  assert(po35_synth_init(NULL, PO35_SYNTH_BANK_LOW, 350) == PO32_ERR_INVALID_ARG);
  assert(po35_synth_init(&s, (po35_synth_bank_t)99, 350) == PO32_ERR_RANGE);
  assert(po35_synth_init(&s, PO35_SYNTH_BANK_LOW, 350) == PO32_OK);
  assert(po35_synth_samples_for_records(&s, 1, 60, &n) == PO32_OK && n == 133);
  assert(po35_synth_samples_for_records(&s, 2, 60, &n) == PO32_OK && n == 267);
  assert(po35_synth_samples_for_records(&s, 3, 60, &n) == PO32_OK && n == 400);
  assert(po35_synth_samples_for_records(&s, (size_t)-1, 20, &n) == PO32_ERR_RANGE && n == 0);
  assert(po35_synth_samples_for_records(&s, 1, 30, &n) == PO32_ERR_RANGE);
  assert(po35_synth_voice_init(&v, &s, NULL, 0, 20) == PO32_OK);
  assert(po35_synth_voice_done(&v) && po35_synth_voice_samples_remaining(&v) == 0);
  assert(po35_synth_voice_render_f32(&v, NULL, 0, &n) == PO32_OK && n == 0);
  assert(po35_synth_voice_init(&v, &s, records, 43, 20) == PO32_ERR_RANGE);
  assert(po35_synth_voice_done(&v));
  assert(po35_synth_voice_init(&v, &s, NULL, 44, 20) == PO32_ERR_INVALID_ARG);
  for (i = 0; i < 3; i++)
    make_record(records + 44 * i, 1536, (unsigned)(i + 1) * 32, 255);
  assert(po35_synth_voice_init(&v, &s, records, 132, 60) == PO32_OK);
  assert(po35_synth_voice_render_f32(&v, audio, 133, &n) == PO32_OK && n == 133);
  assert(v.record_index == 1 && v.samples_rendered == 133);
  assert(po35_synth_voice_render_f32(&v, audio, 134, &n) == PO32_OK && n == 134);
  assert(v.record_index == 2 && v.samples_rendered == 267);
  assert(po35_synth_voice_render_f32(&v, audio, 133, &n) == PO32_OK && n == 133);
  assert(po35_synth_voice_done(&v));
}

static void test_streaming_reset_and_capacity(void) {
  po35_synth_t s;
  po35_synth_voice_t v;
  size_t n, total, pos, i;
  int bank;
  for (i = 0; i < 17; i++) {
    make_record(records + 44 * i, (int)i * 91 - 600, (unsigned)(i + 1) * 7, (unsigned)i * 13);
    records[44 * i + 4] = (uint8_t)(i - 8);
    records[44 * i + 14] = (uint8_t)(8 - i);
  }
  for (bank = 0; bank < 2; bank++) {
    assert(po35_synth_init(&s, (po35_synth_bank_t)bank, 350) == PO32_OK);
    assert(po35_synth_samples_for_records(&s, 17, 60, &total) == PO32_OK);
    audio[0] = 17;
    assert(po35_synth_render(&s, records, 44 * 17, 60, audio, total - 1, &n) ==
           PO32_ERR_BUFFER_TOO_SMALL);
    assert(n == 0 && audio[0] == 17);
    assert(po35_synth_render(&s, records, 44 * 17, 60, audio, total, &n) == PO32_OK && n == total);
    assert(po35_synth_voice_init(&v, &s, records, 44 * 17, 60) == PO32_OK);
    assert(po35_synth_voice_render_f32(&v, NULL, 1, &n) == PO32_ERR_INVALID_ARG && n == 0);
    assert(po35_synth_voice_render_f32(&v, NULL, 0, &n) == PO32_OK && n == 0);
    for (pos = 0; pos < total; pos += n) {
      size_t cap = 1 + pos % 137;
      assert(po35_synth_voice_render_f32(&v, chunks + pos, cap, &n) == PO32_OK && n > 0 &&
             n <= cap);
    }
    assert(pos == total && memcmp(audio, chunks, total * sizeof(float)) == 0);
    po35_synth_voice_reset(&v);
    assert(po35_synth_voice_samples_remaining(&v) == total);
    assert(po35_synth_voice_render_f32(&v, chunks, total, &n) == PO32_OK && n == total);
    assert(memcmp(audio, chunks, total * sizeof(float)) == 0);
  }
}

static void test_voiced_reference(void) {
  po35_synth_t s;
  size_t n, i;
  const double pi = 3.14159265358979323846, step = 440.0 / 8000.0;
  make_record(records, 1536, 128, 0);
  assert(po35_synth_init(&s, PO35_SYNTH_BANK_LOW, 350) == PO32_OK);
  assert(po35_synth_render(&s, records, 44, 20, audio, 400, &n) == PO32_OK && n == 400);
  for (i = 0; i < n; i++) {
    double p = fmod(step * i, 1), theta, expected;
    if (p >= 1e-5 && p < .99999) {
      theta = pi * p;
      expected = (.5 * sin(19 * theta) / sin(theta) - .5) / sqrt(4.5);
    } else {
      if (p >= .5)
        p -= 1;
      theta = p * 1.8137993;
      expected = (9.5 * cos(19 * theta) / cos(theta) - .5) / sqrt(4.5);
    }
    assert(fabs(audio[i] - expected) < 3e-6);
  }
  assert(audio[0] > 1.0f); /* Float output is deliberately unnormalized. */
}

static void test_noise_distribution_and_zero_seed(void) {
  po35_synth_t s;
  size_t n, i;
  double sum = 0, sum2 = 0, lag = 0;
  for (i = 0; i < 1000; i++)
    make_record(records + 44 * i, 1536, 128, 255);
  assert(po35_synth_init(&s, PO35_SYNTH_BANK_LOW, 0) == PO32_OK);
  assert(po35_synth_render(&s, records, 44000, 40, audio, 240000, &n) == PO32_OK && n == 200000);
  for (i = 0; i < n; i++) {
    assert(isfinite(audio[i]));
    sum += audio[i];
    sum2 += (double)audio[i] * audio[i];
    if (i)
      lag += (double)audio[i] * audio[i - 1];
  }
  assert(fabs(sum / n) < .01 && fabs(sum2 / n - 1) < .015 && fabs(lag / n) < .015);
  printf("  Gaussian n=%zu mean=%.7f second moment=%.7f lag1=%.7f\n", n, sum / n, sum2 / n,
         lag / n);
}

static void test_numeric_failure_and_reset(void) {
  po35_synth_t s;
  po35_synth_voice_t v;
  size_t n, i;
  make_record(records, 1536, 128, 0);
  assert(po35_synth_init(&s, PO35_SYNTH_BANK_HIGH, 350) == PO32_OK);
  assert(po35_synth_voice_init(&v, &s, records, 44, 20) == PO32_OK);
  assert(po35_synth_voice_render_f32(&v, audio, 1, &n) == PO32_OK && n == 1);
  v.delay[0] = 1e100;
  audio[0] = 17;
  assert(po35_synth_voice_render_f32(&v, audio, 100, &n) == PO32_ERR_RANGE && n == 0 &&
         audio[0] == 17);
  assert(po35_synth_voice_render_f32(&v, audio, 100, &n) == PO32_ERR_RANGE && n == 0);
  assert(!po35_synth_voice_done(&v));
  po35_synth_voice_reset(&v);
  assert(po35_synth_voice_render_f32(&v, audio, 1200, &n) == PO32_OK && n == 1200);
  for (i = 0; i < n; i++)
    assert(isfinite(audio[i]));
  /* All extreme signed LAR bytes remain valid records, but a 30-pole direct
     form can be numerically unstable. The renderer must stop explicitly. */
  memset(records + 14, 127, 30);
  assert(po35_synth_voice_init(&v, &s, records, 44, 20) == PO32_OK);
  for (i = 0; i < 1200; i++)
    audio[i] = 17;
  assert(po35_synth_voice_render_f32(&v, audio, 1200, &n) == PO32_ERR_RANGE);
  assert(n > 0 && n < 1200 && v.samples_rendered == n);
  for (i = 0; i < n; i++)
    assert(isfinite(audio[i]));
  assert(audio[n] == 17 && audio[1199] == 17);
  printf("  extreme 30-pole record: numeric stop after %zu finite samples\n", n);
  {
    size_t again;
    po35_synth_voice_reset(&v);
    assert(po35_synth_voice_render_f32(&v, chunks, 1200, &again) == PO32_ERR_RANGE);
    assert(again == n && memcmp(audio, chunks, n * sizeof(float)) == 0);
  }
}

static void test_lpc_and_audio_reference(void) {
  /* Fixed tanh/Schur and filter regression values, using
     q[i] = 7 * (i % 7 - 3), pitch 440 Hz, gain 1, no noise. */
  static const double lpc[] = {1,
                               0.42478968335997452,
                               -0.94326727934548493,
                               -1.8401115210551036,
                               -1.4099686173838861,
                               0.43595735172416317,
                               2.8981704075003942,
                               4.3770556224439234,
                               0.62808117837246757,
                               -4.0804858044997161,
                               -5.6384481140057519,
                               -2.9789169723012119,
                               2.2186646921723998,
                               6.4430338727650112,
                               6.1515601042042451,
                               -0.5629124289717824,
                               -5.7263935985056378,
                               -5.7908542521789075,
                               -1.9631680794944131,
                               2.7250144748397238,
                               5.036244365844448,
                               3.3469571273536705,
                               -1.2958126008899769,
                               -3.0133261832018237,
                               -2.0223517410068328,
                               -0.29064645258497845,
                               1.0329913634009835,
                               1.2770602598648175,
                               0.50878240484388115,
                               -0.54476877213187247,
                               -0.31683500112336599};
  static const double expected[] = {
      1.4369262355405543,  -0.64910183187392889, 1.61664410845125,    1.3063412766921949,
      1.7871792960581685,  1.8673684316570527,   1.6799826725971196,  1.0262417990351707,
      3.7744309323001666,  -0.32040913623302464, 0.66598932365892227, 0.19765609660850481,
      -0.0253752139981289, -0.55853933476887263, -1.4994542015329981, -2.0296141604321454};
  po35_synth_t s;
  po35_synth_voice_t v;
  size_t n, i;
  make_record(records, 1536, 128, 0);
  for (i = 0; i < 30; i++)
    records[14 + i] = (uint8_t)(7 * ((int)(i % 7) - 3));
  assert(po35_synth_init(&s, PO35_SYNTH_BANK_HIGH, 350) == PO32_OK);
  assert(po35_synth_voice_init(&v, &s, records, 44, 20) == PO32_OK);
  assert(po35_synth_voice_render_f32(&v, audio, 16, &n) == PO32_OK && n == 16);
  for (i = 0; i < 31; i++)
    assert(fabs(v.lpc[i] - lpc[i]) < 3e-14);
  assert(fabs(v.residual_gain - 0.19554089306070185) < 2e-15);
  for (i = 0; i < 16; i++)
    assert(fabs(audio[i] - expected[i]) < 3e-7);
}

static void test_noise_golden_and_consumption(void) {
  /* Fixed xorshift32/Marsaglia regression values, seed 350. */
  static const float expected[] = {
      -0.37978512048721313f, -0.041792649775743484f, 0.6129953861236572f,  -0.05307433009147644f,
      0.07902200520038605f,  0.6286685466766357f,    0.49596133828163147f, 0.12208618968725204f};
  po35_synth_t s;
  po35_synth_voice_t v;
  size_t n;
  make_record(records, 1536, 128, 255);
  make_record(records + 44, 1536, 128, 255);
  assert(po35_synth_init(&s, PO35_SYNTH_BANK_LOW, 350) == PO32_OK);
  assert(po35_synth_render(&s, records, 88, 80, audio, 200, &n) == PO32_OK && n == 200);
  assert(memcmp(audio, expected, sizeof(expected)) == 0);
  records[2] = 0;
  records[3] = 0; /* A silent, fully voiced frame still consumes noise. */
  assert(po35_synth_voice_init(&v, &s, records, 88, 80) == PO32_OK);
  assert(po35_synth_voice_render_f32(&v, chunks, 200, &n) == PO32_OK && n == 200);
  assert(memcmp(audio + 100, chunks + 100, 100 * sizeof(float)) == 0);
}

static void test_extreme_pitch_and_timing(void) {
  po35_synth_t s;
  po35_synth_voice_t v;
  static const int pitches[] = {-32768, -1, 0, 1536, 32767};
  static const double phases[] = {0, 0.000001, 0.77363637, 0.99998, 0.999999};
  int bank;
  size_t i, j, n, total;
  unsigned fps;
  for (bank = 0; bank < 2; bank++) {
    unsigned sr = bank ? 24000 : 8000;
    assert(po35_synth_init(&s, (po35_synth_bank_t)bank, 350) == PO32_OK);
    for (fps = 20; fps <= 80; fps += 20) {
      for (i = 0; i < 30; i++) {
        assert(po35_synth_samples_for_records(&s, i, fps, &total) == PO32_OK);
        assert(total == (size_t)floor((double)i * sr / fps + .5));
      }
    }
    for (i = 0; i < sizeof(pitches) / sizeof(*pitches); i++) {
      double step = 261.6255653005986 * exp2(pitches[i] / 2048.0) / sr;
      double h, odd;
      if (step > .49)
        step = .49;
      h = floor(.5 / step);
      odd = 2 * h + 1;
      make_record(records, pitches[i], 128, 0);
      for (j = 0; j < sizeof(phases) / sizeof(*phases); j++) {
        double p = phases[j], theta, expected;
        assert(po35_synth_voice_init(&v, &s, records, 44, 20) == PO32_OK);
        v.phase = p;
        assert(po35_synth_voice_render_f32(&v, audio, 1, &n) == PO32_OK && n == 1);
        if (p >= 1e-5 && p < .99999) {
          theta = PO_MATH_PI * p;
          expected = (.5 * sin(odd * theta) / sin(theta) - .5) / sqrt(h * .5);
        } else {
          if (p >= .5)
            p -= 1;
          theta = p * 1.8137993;
          expected = (.5 * odd * cos(odd * theta) / cos(theta) - .5) / sqrt(h * .5);
        }
        assert(fabs((double)audio[0] - expected) < 1e-6 + fabs(expected) * 6e-8);
      }
    }
  }
}

static void test_math_accuracy(void) {
  int i;
  for (i = -10000; i <= 10000; i++) {
    double x = i * .01, p = exp2(x);
    double actual_exp = po_math_exp2(x);
    double actual_log = po_math_log(p), expected_log = log(p);
    double actual_sqrt = po_math_sqrt(p);
    assert(fabs(po_math_sin(x) - sin(x)) < 2e-14);
    assert(fabs(po_math_cos(x) - cos(x)) < 2e-14);
    assert(fabs(po_math_atan2(x, .193) - atan2(x, .193)) < 1e-15);
    assert(fabs(actual_exp / p - 1) < 2e-15);
    assert(fabs(actual_log - expected_log) < 2e-14);
    assert(fabs(actual_sqrt / sqrt(p) - 1) < 1e-15);
    assert(po_math_floor(x) == floor(x));
  }
  assert(fabs(po_math_tan(PO_MATH_PI * 3 / 192000) / tan(PO_MATH_PI * 3 / 192000) - 1) < 1e-15);
  int infinity_is_finite = po_math_isfinite(INFINITY), nan_is_finite = po_math_isfinite(NAN);
  double sqrt_zero = po_math_sqrt(0), log_one = po_math_log(1), exp_zero = po_math_exp2(0);
  assert(!infinity_is_finite && !nan_is_finite);
  assert(sqrt_zero == 0 && log_one == 0 && exp_zero == 1);
}

int main(void) {
  test_arguments_and_timing();
  test_streaming_reset_and_capacity();
  test_voiced_reference();
  test_noise_distribution_and_zero_seed();
  test_numeric_failure_and_reset();
  test_lpc_and_audio_reference();
  test_noise_golden_and_consumption();
  test_extreme_pitch_and_timing();
  test_math_accuracy();
  puts("po35 synth tests passed");
  return 0;
}
