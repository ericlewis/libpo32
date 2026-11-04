#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/po_math_internal.h"
#include "../src/po_resample_internal.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void test_binary64_boundaries(void) {
  static const double floors[] = {0.0,
                                  -0.0,
                                  .25,
                                  -.25,
                                  1.0,
                                  -1.0,
                                  4503599627370495.5,
                                  -4503599627370495.5,
                                  4503599627370496.0,
                                  -4503599627370496.0,
                                  DBL_MAX,
                                  -DBL_MAX,
                                  INFINITY,
                                  -INFINITY};
  static const double roots[] = {0.0,     -0.0, 0x1p-1074, 0x1p-1050, DBL_MIN / 2,
                                 DBL_MIN, .25,  2.0,       DBL_MAX,   INFINITY};
  static const double powers[] = {-INFINITY, -1076, -1075, -1074, -1073.5, -1050.25, -1023,   -1022,
                                  -1,        0,     1,     1023,  1023.5,  1024,     INFINITY};
  static const double logs[] = {0x1p-1074, 0x1p-1050, DBL_MIN / 2, DBL_MIN, .25,     1,
                                1.25,      1.75,      2,           DBL_MAX, INFINITY};
  double actual, expected;
  for (unsigned i = 0; i < sizeof(floors) / sizeof(*floors); ++i) {
    actual = po_math_floor(floors[i]);
    expected = floor(floors[i]);
    assert(actual == expected);
  }
  for (unsigned i = 0; i < sizeof(roots) / sizeof(*roots); ++i) {
    actual = po_math_sqrt(roots[i]);
    expected = sqrt(roots[i]);
    if (expected == 0.0 || isinf(expected)) {
      assert(actual == expected && !!signbit(actual) == !!signbit(expected));
    } else {
      assert(fabs(actual / expected - 1.0) <= 4 * DBL_EPSILON);
    }
  }
  for (unsigned i = 0; i < sizeof(powers) / sizeof(*powers); ++i) {
    actual = po_math_exp2(powers[i]);
    expected = exp2(powers[i]);
    if (isinf(expected)) {
      assert(actual == expected);
    } else {
      assert(fabs(actual - expected) <= 4 * DBL_EPSILON * expected + 0x1p-1074);
    }
  }
  for (unsigned i = 0; i < sizeof(logs) / sizeof(*logs); ++i) {
    actual = po_math_log(logs[i]);
    expected = log(logs[i]);
    if (isinf(expected)) {
      assert(actual == expected);
    } else {
      assert(fabs(actual - expected) <= 4 * DBL_EPSILON * fmax(1, fabs(expected)));
    }
  }

  /* These exceptional inputs are explicitly handled by the private helpers. */
  actual = po_math_nan();
  int finite = po_math_isfinite(actual);
  assert(isnan(actual) && !finite);
  actual = po_math_inf();
  finite = po_math_isfinite(actual);
  assert(isinf(actual) && actual > 0 && !finite);
  actual = po_math_floor(NAN);
  assert(isnan(actual));
  actual = po_math_sqrt(-1);
  assert(isnan(actual));
  actual = po_math_sqrt(NAN);
  assert(isnan(actual));
  actual = po_math_exp2(NAN);
  assert(isnan(actual));
  actual = po_math_log(0);
  assert(isinf(actual) && actual < 0);
  actual = po_math_log(-1);
  assert(isnan(actual));
  actual = po_math_log(NAN);
  assert(isnan(actual));
  actual = po_math_sin(INFINITY);
  assert(isnan(actual));
  actual = po_math_cos(NAN);
  assert(isnan(actual));
  actual = po_math_atan2(NAN, 1);
  assert(isnan(actual));
  actual = po_math_atan2(1, NAN);
  assert(isnan(actual));
}

static void test_angles_and_quadrants(void) {
  static const double coordinates[] = {-10, -1, -.25, 0, .25, 1, 10};
  for (unsigned y = 0; y < sizeof(coordinates) / sizeof(*coordinates); ++y) {
    for (unsigned x = 0; x < sizeof(coordinates) / sizeof(*coordinates); ++x) {
      double actual = po_math_atan2(coordinates[y], coordinates[x]);
      double expected = atan2(coordinates[y], coordinates[x]);
      assert(fabs(actual - expected) < 1e-15);
    }
  }
  for (int i = -2048; i <= 2048; ++i) {
    double angle = i * PO_MATH_PI / 256.0;
    double sine = po_math_sin(angle), cosine = po_math_cos(angle);
    assert(fabs(sine - sin(angle)) < 8e-15);
    assert(fabs(cosine - cos(angle)) < 8e-15);
  }
  for (int i = -100; i <= 100; ++i) {
    double angle = i * .01;
    double actual = po_math_tan(angle);
    assert(fabs(actual - tan(angle)) < 2e-15);
  }
}

/* Independent direct-sinc oracle: no trigonometric recurrence or private math. */
static double reference_resample(const float *pcm, size_t count, double input_rate,
                                 double output_rate, int64_t index) {
  double cutoff = fmin(1.0, output_rate / input_rate), radius = 8.0 / cutoff;
  double position = (double)index * input_rate / output_rate;
  double sum = 0.0, weights = 0.0;
  int64_t first = (int64_t)floor(position - radius) + 1;
  int64_t last = (int64_t)floor(position + radius);
  for (int64_t i = first; i <= last; ++i) {
    double z = PO_MATH_PI * (position - (double)i) * cutoff;
    double weight = fabs(z) < 1e-8 ? 1.0 : (sin(z) / z) * (sin(z / 8.0) / (z / 8.0));
    weights += weight;
    if (i >= 0 && (uint64_t)i < count)
      sum += pcm[i] * weight;
  }
  return sum / weights;
}

static void test_resampler_reference(void) {
  static const double rates[][2] = {
      {8000, 8000}, {8000, 24000}, {24000, 8000}, {44100, 24000}, {192000, 8000}};
  float pcm[257];
  for (unsigned i = 0; i < sizeof(pcm) / sizeof(*pcm); ++i)
    pcm[i] = (float)(.25 * sin(i * .23) + .125 * cos(i * .71));
  for (unsigned r = 0; r < sizeof(rates) / sizeof(*rates); ++r) {
    po_resample_kernel_t kernel;
    double input_rate = rates[r][0], output_rate = rates[r][1];
    po_resample_init(&kernel, input_rate, output_rate);
    for (int64_t i = -200; i < 1000; ++i) {
      double actual = po_resample_f32(&kernel, pcm, 257, input_rate, output_rate, i);
      double expected = reference_resample(pcm, 257, input_rate, output_rate, i);
      assert(fabs(actual - expected) < 3e-14);
    }
  }
  /* The denominator includes zero-padding, so an edge impulse loses no time alignment. */
  po_resample_kernel_t kernel;
  const float impulse[] = {1.0f};
  po_resample_init(&kernel, 8000, 24000);
  double center = po_resample_f32(&kernel, impulse, 1, 8000, 24000, 0);
  double left = po_resample_f32(&kernel, impulse, 1, 8000, 24000, -1);
  double right = po_resample_f32(&kernel, impulse, 1, 8000, 24000, 1);
  assert(fabs(center - 1.0) < 1e-14 && fabs(left - right) < 1e-14);
  assert(right > 0.8 && right < 0.9);
}

int main(void) {
  test_binary64_boundaries();
  test_angles_and_quadrants();
  test_resampler_reference();
  puts("private math and resampling tests passed");
  return 0;
}
