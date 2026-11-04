#ifndef PO_MATH_INTERNAL_H
#define PO_MATH_INTERNAL_H

/* Private binary64 DSP math. No libm, allocation, errno or floating environment.
 * Finite inputs are expected except where explicitly handled. Trigonometric
 * callers reduce oscillator phase before forming large angles. These helpers
 * favor accuracy over throughput; they are not replacements for general libm.
 */
#include <stdint.h>

#define PO_MATH_PI  3.14159265358979323846264338327950288
#define PO_MATH_LN2 0.69314718055994530941723212145817657

static inline int po_math_isfinite(double x) {
  union {
    double value;
    uint64_t bits;
  } number;
  number.value = x;
  return (number.bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}

static inline double po_math_nan(void) {
  union {
    uint64_t bits;
    double value;
  } result;
  result.bits = UINT64_C(0x7ff8000000000000);
  return result.value;
}

static inline double po_math_inf(void) {
  union {
    uint64_t bits;
    double value;
  } result;
  result.bits = UINT64_C(0x7ff0000000000000);
  return result.value;
}

static inline double po_math_floor(double x) {
  int64_t integral;
  if (!(x < 4503599627370496.0 && x > -4503599627370496.0))
    return x;
  integral = (int64_t)x;
  return (double)integral - ((double)integral > x ? 1.0 : 0.0);
}

static inline double po_math_sqrt(double x) {
  union {
    uint64_t bits;
    double value;
  } guess;
  double scale = 1.0;
  unsigned i;
  if (x == 0.0 || x == po_math_inf())
    return x;
  if (!(x > 0.0))
    return po_math_nan();
  if (x < 2.2250738585072014e-308) {
    x *= 18014398509481984.0;
    scale = 0.000000007450580596923828125;
  }
  guess.value = x;
  guess.bits = (guess.bits >> 1) + UINT64_C(0x1ff8000000000000);
  for (i = 0; i < 6; ++i)
    guess.value = 0.5 * (guess.value + x / guess.value);
  return guess.value * scale;
}

static inline double po_math_exp2(double x) {
  union {
    uint64_t bits;
    double value;
  } scale;
  double fractional, term, result;
  int exponent;
  unsigned i;
  if (x != x)
    return x;
  if (x >= 1024.0)
    return po_math_inf();
  if (x < -1075.0)
    return 0.0;
  exponent = (int)po_math_floor(x);
  fractional = (x - (double)exponent) * PO_MATH_LN2;
  result = term = 1.0;
  for (i = 1; i <= 18; ++i) {
    term *= fractional / (double)i;
    result += term;
  }
  if (exponent < -1022) {
    scale.bits = (uint64_t)(exponent + 1077) << 52;
    return (result * scale.value) * 5.5511151231257827021181583404541015625e-17;
  }
  scale.bits = (uint64_t)(exponent + 1023) << 52;
  return result * scale.value;
}

static inline double po_math_log(double x) {
  union {
    uint64_t bits;
    double value;
  } part;
  int exponent, adjustment = 0;
  double y, y2, term, sum;
  unsigned i;
  if (x == 0.0)
    return -po_math_inf();
  if (!(x > 0.0))
    return po_math_nan();
  if (!po_math_isfinite(x))
    return x;
  if (x < 2.2250738585072014e-308) {
    x *= 18014398509481984.0;
    adjustment = -54;
  }
  part.value = x;
  exponent = (int)((part.bits >> 52) & 2047u) - 1023 + adjustment;
  part.bits = (part.bits & UINT64_C(0x000fffffffffffff)) | UINT64_C(0x3ff0000000000000);
  if (part.value > 1.4142135623730950488) {
    part.value *= 0.5;
    ++exponent;
  }
  y = (part.value - 1.0) / (part.value + 1.0);
  y2 = y * y;
  sum = term = y;
  for (i = 3; i <= 25; i += 2) {
    term *= y2;
    sum += term / (double)i;
  }
  return (double)exponent * PO_MATH_LN2 + 2.0 * sum;
}

static inline double po_math_reduce_angle(double x) {
  double turns;
  if (!po_math_isfinite(x))
    return po_math_nan();
  turns = po_math_floor(x / (2.0 * PO_MATH_PI) + 0.5);
  /* Split 2*pi so subtraction retains small-angle accuracy. */
  return (x - turns * 6.28318530717958623200) - turns * 2.44929359829470641435e-16;
}

static inline double po_math_sin(double x) {
  double term, sum, square;
  unsigned i;
  x = po_math_reduce_angle(x);
  if (x > 0.5 * PO_MATH_PI)
    x = PO_MATH_PI - x;
  else if (x < -0.5 * PO_MATH_PI)
    x = -PO_MATH_PI - x;
  square = x * x;
  sum = term = x;
  for (i = 1; i <= 10; ++i) {
    term *= -square / ((double)(2 * i) * (double)(2 * i + 1));
    sum += term;
  }
  return sum;
}

static inline double po_math_cos(double x) {
  double term, sum, square, sign = 1.0;
  unsigned i;
  x = po_math_reduce_angle(x);
  if (x > 0.5 * PO_MATH_PI) {
    x = PO_MATH_PI - x;
    sign = -1.0;
  } else if (x < -0.5 * PO_MATH_PI) {
    x = -PO_MATH_PI - x;
    sign = -1.0;
  }
  square = x * x;
  sum = term = 1.0;
  for (i = 1; i <= 10; ++i) {
    term *= -square / ((double)(2 * i - 1) * (double)(2 * i));
    sum += term;
  }
  return sign * sum;
}

static inline double po_math_tan(double x) {
  return po_math_sin(x) / po_math_cos(x);
}

static inline double po_math_atan_unit(double x) {
  double offset = 0.0, square, term, sum;
  unsigned i;
  if (x > 0.4142135623730950488) {
    x = (x - 1.0) / (x + 1.0);
    offset = PO_MATH_PI * 0.25;
  }
  square = x * x;
  sum = term = x;
  for (i = 3; i <= 45; i += 2) {
    term *= -square;
    sum += term / (double)i;
  }
  return offset + sum;
}

static inline double po_math_atan2(double y, double x) {
  double ax = x < 0.0 ? -x : x;
  double ay = y < 0.0 ? -y : y;
  double result;
  if (x != x || y != y)
    return po_math_nan();
  if (ax == 0.0 && ay == 0.0)
    return 0.0;
  if (ax > ay)
    result = po_math_atan_unit(ay / ax);
  else
    result = 0.5 * PO_MATH_PI - po_math_atan_unit(ax / ay);
  if (x < 0.0)
    result = PO_MATH_PI - result;
  return y < 0.0 ? -result : result;
}

#endif
