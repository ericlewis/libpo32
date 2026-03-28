/*
 * Internal LUT primitives shared across po32 translation units.
 *
 * Provides sin, cos, exp2, log2, powf, expf, pow10 — all via
 * precomputed tables from po32_synth_tables.h.  No <math.h> needed.
 */

#ifndef PO32_LUT_INTERNAL_H
#define PO32_LUT_INTERNAL_H

#include "po32_synth_tables.h"
#include <stdint.h>

#define PO32_LUT_PI         3.14159265358979323846f
#define PO32_LUT_TWO_PI     6.28318530717958647692f
#define PO32_LUT_HALF_PI    1.57079632679489661923f
#define PO32_LUT_INV_TWO_PI 0.15915494309189533577f
#define PO32_LUT_LOG2_E     1.44269504088896340736f
#define PO32_LUT_LOG2_10    3.32192809488736234787f

/* ── sin / cos ──────────────────────────────────────────────────── */

static inline float po32_lut_sinf(float rad) {
  float idx = rad * ((float)PO32_SINE_TABLE_SIZE * PO32_LUT_INV_TWO_PI);
  int   i0;
  float frac;

  idx = idx - (float)PO32_SINE_TABLE_SIZE * (float)(int)(idx * (1.0f / (float)PO32_SINE_TABLE_SIZE));
  if (idx < 0.0f) idx += (float)PO32_SINE_TABLE_SIZE;

  i0   = (int)idx;
  frac = idx - (float)i0;
  i0  &= PO32_SINE_TABLE_MASK;

  return po32_sine_table[i0]
       + frac * (po32_sine_table[(i0 + 1) & PO32_SINE_TABLE_MASK] - po32_sine_table[i0]);
}

static inline float po32_lut_cosf(float rad) {
  return po32_lut_sinf(rad + PO32_LUT_HALF_PI);
}

/* ── exp2 ───────────────────────────────────────────────────────── */

static inline float po32_lut_exp2f(float x) {
  int    int_part, i0;
  float  frac_scaled, frac, a, b;
  union { float f; uint32_t u; } result;

  if (x < -126.0f) return 0.0f;
  if (x >  127.0f) return 3.4028235e+38f;

  int_part = (int)x;
  if (x < (float)int_part) int_part--;

  frac_scaled = (x - (float)int_part) * (float)PO32_EXP2_TABLE_SIZE;
  i0   = (int)frac_scaled;
  frac = frac_scaled - (float)i0;

  if (i0 >= PO32_EXP2_TABLE_SIZE - 1) {
    i0 = PO32_EXP2_TABLE_SIZE - 1;
    a = po32_exp2_frac_table[i0];
    b = 2.0f;
  } else {
    a = po32_exp2_frac_table[i0];
    b = po32_exp2_frac_table[i0 + 1];
  }

  result.u = (uint32_t)(int_part + 127) << 23;
  return (a + frac * (b - a)) * result.f;
}

/* ── log2 ───────────────────────────────────────────────────────── */

static inline float po32_lut_log2f(float x) {
  union { float f; uint32_t u; } conv;
  int   exponent, i0;
  float frac_scaled, frac, a, b;

  if (x <= 0.0f) return -126.0f;

  conv.f = x;
  exponent = (int)((conv.u >> 23) & 0xFFu) - 127;

  frac_scaled = (float)(conv.u & 0x007FFFFFu)
              * ((float)PO32_LOG2_TABLE_SIZE / 8388608.0f);

  i0   = (int)frac_scaled;
  frac = frac_scaled - (float)i0;
  if (i0 >= PO32_LOG2_TABLE_SIZE - 1) {
    i0 = PO32_LOG2_TABLE_SIZE - 1;
    return (float)exponent + po32_log2_frac_table[i0]
         + frac * (1.0f - po32_log2_frac_table[i0]);
  }

  a = po32_log2_frac_table[i0];
  b = po32_log2_frac_table[i0 + 1];

  return (float)exponent + a + frac * (b - a);
}

/* ── Derived: powf, expf, pow10f ────────────────────────────────── */

static inline float po32_lut_powf(float base, float exponent) {
  if (base <= 0.0f) return 0.0f;
  return po32_lut_exp2f(exponent * po32_lut_log2f(base));
}

static inline float po32_lut_expf(float x) {
  return po32_lut_exp2f(x * PO32_LUT_LOG2_E);
}

static inline float po32_lut_pow10f(float x) {
  return po32_lut_exp2f(x * PO32_LUT_LOG2_10);
}

/* ── logf, log10f ───────────────────────────────────────────────── */

#define PO32_LUT_LN2       0.69314718055994530942f
#define PO32_LUT_LOG10_2   0.30102999566398119521f

static inline float po32_lut_logf(float x) {
  return po32_lut_log2f(x) * PO32_LUT_LN2;
}

static inline float po32_lut_log10f(float x) {
  return po32_lut_log2f(x) * PO32_LUT_LOG10_2;
}

#endif /* PO32_LUT_INTERNAL_H */
