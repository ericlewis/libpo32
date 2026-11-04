#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/po35_analysis.c"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static po35_analysis_workspace_t workspace;

static void test_lar_quantization_bounds(void) {
  /* The clamped interval is monotonic: round(64/3 * log((1+k)/(1-k)))
   * ranges from -128 to 128. Only the positive endpoint needs saturation. */
  for (int i = -9950; i <= 9950; ++i) {
    double k = i / 10000.0;
    int expected = (int)round((64.0 / 3.0) * log((1 + k) / (1 - k)));
    int actual = nearest((64.0 / 3.0) * po_math_log((1 + k) / (1 - k)));
    assert(actual == expected && actual >= -128 && actual <= 128);
  }
}

static void test_gain_quantization(void) {
  static const double amplitudes[] = {0, 1.0 / 65536, .25, 1, 3.9, 4, 16, DBL_MAX};
  static const uint8_t expected[] = {0, 1, 64, 128, 253, 255, 255, 255};
  for (unsigned i = 0; i < sizeof(amplitudes) / sizeof(*amplitudes); ++i) {
    uint8_t actual = quantize_gain(amplitudes[i]);
    assert(actual == expected[i]);
  }
}

static void test_quiet_pitch_interpolation(void) {
  double periodicity, frequency;
  setup(&workspace, 24000);
  for (unsigned i = 0; i < 1024; ++i)
    workspace.high[i] = 1e-7 * sin(2 * PO_MATH_PI * 440 * i / 24000.0);
  frequency = pitch(&workspace, &periodicity);
  /* At tiny correlation magnitudes, use the peak's integer lag rather than
   * amplifying rounding error with a nearly-zero interpolation denominator. */
  assert(fabs(frequency - 24000.0 / 55.0) < 1e-12);
  assert(periodicity > .95 && periodicity <= 1.0);
  for (unsigned i = 0; i < 1024; ++i)
    workspace.high[i] = .2 * sin(2 * PO_MATH_PI * 440 * i / 24000.0);
  frequency = pitch(&workspace, &periodicity);
  assert(fabs(frequency - 440) < 1.0);
}

static double reference_power(const uint8_t *bytes, unsigned order, double rate, double f0,
                              double uv) {
  double coefficients[31] = {1}, temporary[31], residual = 1, sum = 0;
  unsigned harmonics = (unsigned)(rate * .5 / f0);
  for (unsigned i = 1; i <= order; ++i) {
    int q = bytes[i - 1] < 128 ? bytes[i - 1] : (int)bytes[i - 1] - 256;
    double reflection = tanh(q * 3.0 / 128.0);
    residual *= 1 - reflection * reflection;
    for (unsigned j = 1; j < i; ++j)
      temporary[j] = coefficients[j] + reflection * coefficients[i - j];
    for (unsigned j = 1; j < i; ++j)
      coefficients[j] = temporary[j];
    coefficients[i] = reflection;
  }
  if (!harmonics)
    return uv;
  for (unsigned h = 1; h <= harmonics; ++h) {
    double re = 1, im = 0;
    for (unsigned i = 1; i <= order; ++i) {
      double angle = 2 * PO_MATH_PI * f0 * h * i / rate;
      re += coefficients[i] * cos(angle);
      im += coefficients[i] * sin(angle);
    }
    sum += 1 / fmax(1e-20, re * re + im * im);
  }
  return uv + (1 - uv) * residual * sum / harmonics;
}

static void test_power_model(void) {
  uint8_t coefficients[30] = {0};
  static const unsigned orders[] = {10, 30};
  for (unsigned bank = 0; bank < 2; ++bank) {
    unsigned order = orders[bank];
    double rate = bank ? 24000 : 8000;
    /* No filter has unit power; frequencies above Nyquist have no voiced harmonics. */
    double actual = output_power(&workspace, coefficients, order, rate, 400, .25);
    assert(fabs(actual - 1) < 1e-14);
    actual = output_power(&workspace, coefficients, order, rate, rate, .25);
    assert(actual == .25);
    for (unsigned i = 0; i < order; ++i)
      coefficients[i] = (uint8_t)(3 * ((int)(i % 5) - 2));
    for (unsigned unvoiced = 0; unvoiced < 3; ++unvoiced) {
      double uv = .5 * unvoiced;
      actual = output_power(&workspace, coefficients, order, rate, 440, uv);
      double expected = reference_power(coefficients, order, rate, 440, uv);
      assert(fabs(actual - expected) < 1e-12);
    }
    memset(coefficients, 0, sizeof(coefficients));
  }
  /* A legal extreme LAR bank has a near-zero response denominator at Nyquist.
   * The power floor prevents division by cancellation noise. */
  memset(coefficients, 128, 10);
  double actual = output_power(&workspace, coefficients, 10, 8000, 4000, 0);
  double reflection = tanh(-3.0);
  double expected = pow(1 - reflection * reflection, 10) / 1e-20;
  assert(isfinite(actual) && fabs(actual / expected - 1) < 1e-12);
}

int main(void) {
  test_lar_quantization_bounds();
  test_gain_quantization();
  test_quiet_pitch_interpolation();
  test_power_model();
  puts("private PO-35 analysis tests passed");
  return 0;
}
