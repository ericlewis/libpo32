#include "po32.h"

#ifndef PO32_ENABLE_MTDRUM_IMPORT
#define PO32_ENABLE_MTDRUM_IMPORT 1
#endif

#if PO32_ENABLE_MTDRUM_IMPORT

#include "po32_lut.h"

#include <stddef.h>

/* ── freestanding helpers ──────────────────────────────── */

static size_t po32_cstrlen(const char *s) {
  const char *p = s;
  while (*p != '\0')
    ++p;
  return (size_t)(p - s);
}

static int po32_import_memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (n-- > 0u) {
    if (*pa != *pb)
      return (int)*pa - (int)*pb;
    ++pa;
    ++pb;
  }
  return 0;
}

static void po32_import_memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  while (n-- > 0u)
    *d++ = *s++;
}

static float po32_import_strtof(const char *s, char **endptr) {
  const char *p = s;
  float result = 0.0f;
  float frac = 0.0f;
  float frac_div = 1.0f;
  int sign = 1;
  int got_digit = 0;
  int in_frac = 0;
  int exp_sign = 1;
  int exponent = 0;

  if (*p == '-') {
    sign = -1;
    ++p;
  } else if (*p == '+') {
    ++p;
  }

  while (*p >= '0' && *p <= '9') {
    result = result * 10.0f + (float)(*p - '0');
    got_digit = 1;
    ++p;
  }
  if (*p == '.') {
    ++p;
    in_frac = 1;
    while (*p >= '0' && *p <= '9') {
      frac_div *= 10.0f;
      frac += (float)(*p - '0') / frac_div;
      got_digit = 1;
      ++p;
    }
  }
  result = (float)sign * (result + frac);

  if (got_digit && (*p == 'e' || *p == 'E')) {
    ++p;
    if (*p == '-') {
      exp_sign = -1;
      ++p;
    } else if (*p == '+') {
      ++p;
    }
    while (*p >= '0' && *p <= '9') {
      exponent = exponent * 10 + (*p - '0');
      ++p;
    }
    {
      float mul = 1.0f;
      int i;
      for (i = 0; i < exponent; ++i)
        mul *= 10.0f;
      result = exp_sign > 0 ? result * mul : result / mul;
    }
  }

  if (endptr != NULL)
    *endptr = got_digit ? (char *)p : (char *)s;
  (void)in_frac;
  return result;
}

typedef struct po32_span {
  const char *begin;
  const char *end;
} po32_span_t;

static float po32_patch_import_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static po32_span_t po32_patch_import_trim(po32_span_t span) {
  while (span.begin < span.end && (*span.begin == ' ' || *span.begin == '\t' ||
                                   *span.begin == '\r' || *span.begin == '\n')) {
    span.begin += 1;
  }
  while (span.end > span.begin && (span.end[-1] == ' ' || span.end[-1] == '\t' ||
                                   span.end[-1] == '\r' || span.end[-1] == '\n')) {
    span.end -= 1;
  }
  return span;
}

static int po32_patch_import_key_equals(po32_span_t key, const char *literal) {
  const size_t literal_len = po32_cstrlen(literal);
  return (size_t)(key.end - key.begin) == literal_len &&
         po32_import_memcmp(key.begin, literal, literal_len) == 0;
}

static int po32_patch_import_starts_with(po32_span_t value, const char *literal) {
  const size_t literal_len = po32_cstrlen(literal);
  return (size_t)(value.end - value.begin) >= literal_len &&
         po32_import_memcmp(value.begin, literal, literal_len) == 0;
}

static int po32_patch_import_contains(po32_span_t value, const char *literal) {
  const size_t literal_len = po32_cstrlen(literal);
  const size_t value_len = (size_t)(value.end - value.begin);
  size_t i;

  if (literal_len == 0u || literal_len > value_len) {
    return 0;
  }

  for (i = 0u; i + literal_len <= value_len; ++i) {
    if (po32_import_memcmp(value.begin + i, literal, literal_len) == 0) {
      return 1;
    }
  }

  return 0;
}

static int po32_patch_import_is_number_char(char c) {
  return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
}

static po32_status_t po32_patch_import_parse_leading_float(po32_span_t value, float *out) {
  char buffer[64];
  char *endptr = NULL;
  size_t len = 0u;

  if (out == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  value = po32_patch_import_trim(value);
  while (value.begin + len < value.end && po32_patch_import_is_number_char(value.begin[len])) {
    len += 1u;
  }

  if (len == 0u || len >= sizeof(buffer)) {
    return PO32_ERR_PARSE;
  }

  po32_import_memcpy(buffer, value.begin, len);
  buffer[len] = '\0';
  *out = po32_import_strtof(buffer, &endptr);
  if (endptr == buffer) {
    return PO32_ERR_PARSE;
  }

  return PO32_OK;
}

static float po32_patch_import_hz_to_param(float hz) {
  return po32_patch_import_clamp01(po32_lut_logf(hz / 20.0f) / po32_lut_logf(1000.0f));
}

static float po32_patch_import_decay_ms_to_param(float ms) {
  return po32_patch_import_clamp01((po32_lut_log10f(ms / 1000.0f) + 2.0f) / 3.0f);
}

static float po32_patch_import_attack_ms_to_param(float ms) {
  float seconds;
  float lo = 0.0f;
  float hi = 1.0f;
  int i;

  if (ms <= 0.0f) {
    return 0.0f;
  }

  seconds = ms / 1000.0f;
  for (i = 0; i < 32; ++i) {
    const float mid = (lo + hi) * 0.5f;
    const float probe = mid == 0.0f ? 0.0f : mid * po32_lut_pow10f((12.0f * mid - 7.0f) / 5.0f);
    if (probe < seconds) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  return po32_patch_import_clamp01((lo + hi) * 0.5f);
}

static float po32_patch_import_filter_q_to_param(float q) {
  return po32_patch_import_clamp01(po32_lut_log10f(q / 0.1f) / 5.0f);
}

static float po32_patch_import_mod_amount_to_param(float semitones) {
  return po32_patch_import_clamp01(semitones / 192.0f + 0.5f);
}

static float po32_patch_import_eq_gain_to_param(float db) {
  return po32_patch_import_clamp01((db + 40.0f) / 80.0f);
}

static float po32_patch_import_percent_to_param(float percent) {
  return po32_patch_import_clamp01(percent / 200.0f);
}

static float po32_patch_import_level_db_to_param(float db) {
  float lo;
  float hi;
  int i;

  if (db <= -500.0f) {
    return 0.0f;
  }

  lo = 0.0001f;
  hi = 1.0f;
  for (i = 0; i < 40; ++i) {
    const float mid = (lo + hi) * 0.5f;
    const float probe = 60.0f * mid - 49.0f - 1.0f / mid;
    if (probe < db) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  return po32_patch_import_clamp01((lo + hi) * 0.5f);
}

static po32_status_t po32_patch_import_parse_line(po32_span_t key, po32_span_t value,
                                                  int *mod_mode_hint, int *recognized_fields,
                                                  po32_patch_params_t *out) {
  float raw = 0.0f;
  po32_status_t status;

  if (mod_mode_hint == NULL || recognized_fields == NULL || out == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  if (po32_patch_import_key_equals(key, "OscWave")) {
    *recognized_fields += 1;
    if (po32_patch_import_starts_with(value, "Sine")) {
      out->OscWave = 0.0f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "Triangle")) {
      out->OscWave = 0.5f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "Saw")) {
      out->OscWave = 1.0f;
      return PO32_OK;
    }
    return PO32_ERR_PARSE;
  }

  if (po32_patch_import_key_equals(key, "OscFreq")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK || raw <= 0.0f) {
      return PO32_ERR_PARSE;
    }
    out->OscFreq = po32_patch_import_hz_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "OscAtk")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->OscAtk = po32_patch_import_attack_ms_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "OscDcy")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK || raw <= 0.0f) {
      return PO32_ERR_PARSE;
    }
    out->OscDcy = po32_patch_import_decay_ms_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "ModMode")) {
    *recognized_fields += 1;
    if (po32_patch_import_starts_with(value, "Decay")) {
      *mod_mode_hint = 1;
      out->ModMode = 0.0f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "Sine")) {
      *mod_mode_hint = 2;
      out->ModMode = 0.5f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "Noise")) {
      *mod_mode_hint = 3;
      out->ModMode = 1.0f;
      return PO32_OK;
    }
    return PO32_ERR_PARSE;
  }

  if (po32_patch_import_key_equals(key, "ModRate")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    if (po32_patch_import_contains(value, "ms") || *mod_mode_hint == 1) {
      if (raw <= 0.0f) {
        return PO32_ERR_PARSE;
      }
      out->ModRate = po32_patch_import_decay_ms_to_param(raw);
    } else if (*mod_mode_hint == 2) {
      out->ModRate = po32_patch_import_clamp01(raw / 2000.0f);
    } else {
      if (raw <= 0.0f) {
        return PO32_ERR_PARSE;
      }
      out->ModRate = po32_patch_import_hz_to_param(raw);
    }
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "ModAmt")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->ModAmt = po32_patch_import_mod_amount_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "NFilMod")) {
    *recognized_fields += 1;
    if (po32_patch_import_starts_with(value, "LP")) {
      out->NFilMod = 0.0f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "BP")) {
      out->NFilMod = 0.5f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "HP")) {
      out->NFilMod = 1.0f;
      return PO32_OK;
    }
    return PO32_ERR_PARSE;
  }

  if (po32_patch_import_key_equals(key, "NFilFrq")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK || raw <= 0.0f) {
      return PO32_ERR_PARSE;
    }
    out->NFilFrq = po32_patch_import_hz_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "NFilQ")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK || raw <= 0.0f) {
      return PO32_ERR_PARSE;
    }
    out->NFilQ = po32_patch_import_filter_q_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "NEnvMod")) {
    *recognized_fields += 1;
    if (po32_patch_import_starts_with(value, "Exp")) {
      out->NEnvMod = 0.0f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "Lin")) {
      out->NEnvMod = 0.5f;
      return PO32_OK;
    }
    if (po32_patch_import_starts_with(value, "Mod")) {
      out->NEnvMod = 1.0f;
      return PO32_OK;
    }
    return PO32_ERR_PARSE;
  }

  if (po32_patch_import_key_equals(key, "NEnvAtk")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->NEnvAtk = po32_patch_import_attack_ms_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "NEnvDcy")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK || raw <= 0.0f) {
      return PO32_ERR_PARSE;
    }
    out->NEnvDcy = po32_patch_import_decay_ms_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "Mix")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->Mix = po32_patch_import_clamp01(1.0f - raw / 100.0f);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "DistAmt")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->DistAmt = po32_patch_import_clamp01(raw / 100.0f);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "EQFreq")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK || raw <= 0.0f) {
      return PO32_ERR_PARSE;
    }
    out->EQFreq = po32_patch_import_hz_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "EQGain")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->EQGain = po32_patch_import_eq_gain_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "Level")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->Level = po32_patch_import_level_db_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "OscVel")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->OscVel = po32_patch_import_percent_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "NVel")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->NVel = po32_patch_import_percent_to_param(raw);
    return PO32_OK;
  }

  if (po32_patch_import_key_equals(key, "ModVel")) {
    *recognized_fields += 1;
    status = po32_patch_import_parse_leading_float(value, &raw);
    if (status != PO32_OK) {
      return status;
    }
    out->ModVel = po32_patch_import_percent_to_param(raw);
    return PO32_OK;
  }

  return PO32_OK;
}

po32_status_t po32_patch_parse_mtdrum_text(const char *text, size_t text_len,
                                           po32_patch_params_t *out) {
  const char *cursor;
  const char *end;
  int mod_mode_hint = 0;
  int recognized_fields = 0;

  if (text == NULL || out == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  po32_patch_params_zero(out);
  cursor = text;
  end = text + text_len;

  while (cursor < end) {
    const char *line_end = cursor;
    const char *colon;
    po32_span_t line;

    while (line_end < end && *line_end != '\n') {
      line_end += 1;
    }

    line.begin = cursor;
    line.end = line_end;
    line = po32_patch_import_trim(line);

    colon = line.begin;
    while (colon < line.end && *colon != ':') {
      colon += 1;
    }

    if (colon < line.end) {
      po32_span_t key;
      po32_span_t value;
      po32_status_t status;

      key.begin = line.begin;
      key.end = colon;
      key = po32_patch_import_trim(key);

      value.begin = colon + 1;
      value.end = line.end;
      value = po32_patch_import_trim(value);

      status = po32_patch_import_parse_line(key, value, &mod_mode_hint, &recognized_fields, out);
      if (status != PO32_OK) {
        po32_patch_params_zero(out);
        return status;
      }
    }

    cursor = line_end;
    if (cursor < end && *cursor == '\n') {
      cursor += 1;
    }
  }

  if (recognized_fields == 0) {
    po32_patch_params_zero(out);
    return PO32_ERR_PARSE;
  }

  return PO32_OK;
}

#else

po32_status_t po32_patch_parse_mtdrum_text(const char *text, size_t text_len,
                                           po32_patch_params_t *out) {
  (void)text;
  (void)text_len;

  if (text == NULL || out == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  po32_patch_params_zero(out);
  return PO32_ERR_PARSE;
}

#endif
