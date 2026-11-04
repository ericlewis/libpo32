/* Normalised voice-control maps shared by synthesis and text import.
 * Measurements and reference points are documented in PATCH_PARAMETERS.md. */
#ifndef PO32_PARAM_MAPS_H
#define PO32_PARAM_MAPS_H
#include "po32_lut.h"
static inline float po32_param_clamp01(float x) {
  return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}
static inline float po32_param_attack_seconds(float x) {
  return x <= 0.0f ? 0.0f : x * 0.03981071706f * po32_lut_pow10f(2.4f * x);
}
static inline float po32_param_mod_semitones(float x, float mode) {
  float bipolar = 2.0f * x - 1.0f;
  float magnitude = bipolar < 0.0f ? -bipolar : bipolar;
  return bipolar * magnitude * (mode < (1.0f / 3.0f) ? 96.0f : 48.0f);
}
static inline float po32_param_mod_from_semitones(float semitones, int decay) {
  float limit = decay ? 96.0f : 48.0f;
  float magnitude = po32_param_clamp01((semitones < 0.0f ? -semitones : semitones) / limit);
  float bipolar = po32_lut_powf(magnitude, 0.5f);
  return 0.5f + (semitones < 0.0f ? -0.5f : 0.5f) * bipolar;
}
static inline float po32_param_mod_decay_coefficient(float rate, float inverse_sr) {
  /* Rate zero holds the initial pitch indefinitely; avoid dividing by zero. */
  return rate <= 0.0f ? 1.0f : po32_lut_powf(0.001f, inverse_sr * 3.0f * rate / (1.03f - rate));
}
static inline float po32_param_mod_from_ms(float ms) {
  return po32_param_clamp01(1.03f / (1.0f + 0.003f * ms));
}
static inline float po32_param_mod_hz(float rate, int sine) {
  return (sine ? 2000.0f : 20000.0f) * rate * po32_lut_pow10f((sine ? 3.5f : 4.0f) * (rate - 1.0f));
}
static inline float po32_param_mod_from_hz(float hz, int sine) {
  float lo = 0.0f, hi = 1.0f;
  int i;
  if (hz <= 0.0f)
    return 0.0f;
  if (hz >= (sine ? 2000.0f : 20000.0f))
    return 1.0f;
  for (i = 0; i < 32; ++i) {
    float mid = (lo + hi) * 0.5f;
    if (po32_param_mod_hz(mid, sine) < hz)
      lo = mid;
    else
      hi = mid;
  }
  return (lo + hi) * 0.5f;
}
#endif
