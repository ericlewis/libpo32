#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <string.h>

#include "po32.h"

void po32_patch_params_zero(po32_patch_params_t *params) {
  if (params != NULL) {
    memset(params, 0, sizeof(*params));
  }
}

#include "../src/po32_patch_import.c"

static float abs_diff(float a, float b) {
  float d = a - b;
  return d < 0.0f ? -d : d;
}

static po32_span_t span_from_cstr(const char *text) {
  po32_span_t span;
  span.begin = text;
  span.end = text + strlen(text);
  return span;
}

static void test_import_strtof_edges(void) {
  char *endptr = NULL;
  float value;

  value = po32_import_strtof("+12.5", &endptr);
  assert(abs_diff(value, 12.5f) < 0.0001f);
  assert(*endptr == '\0');

  value = po32_import_strtof("-12.5e-1", &endptr);
  assert(abs_diff(value, -1.25f) < 0.0001f);
  assert(*endptr == '\0');

  value = po32_import_strtof("+12.5E+1", &endptr);
  assert(abs_diff(value, 125.0f) < 0.001f);
  assert(*endptr == '\0');

  value = po32_import_strtof("+", &endptr);
  assert(value == 0.0f);
  assert(endptr != NULL && *endptr == '+');

  value = po32_import_strtof("2.5e+", &endptr);
  assert(abs_diff(value, 2.5f) < 0.0001f);
  assert(*endptr == '\0');

  value = po32_import_strtof("1e9999999999", &endptr);
  assert(value > 1.0e30f);
  assert(*endptr == '\0');

  value = po32_import_strtof("1e-9999999999", &endptr);
  assert(value == 0.0f);
  assert(*endptr == '\0');
}

static void test_trim_and_match_helpers(void) {
  po32_span_t span;
  po32_status_t status;
  float parsed = 0.0f;
  int result;

  span = po32_patch_import_trim(span_from_cstr(" \t\" value \" ,\r\n"));
  assert((size_t)(span.end - span.begin) == 5u);
  assert(strncmp(span.begin, "value", 5u) == 0);

  result = po32_patch_import_starts_with_nocase(span_from_cstr("inf ms"), "INF");
  assert(result == 1);
  result = po32_patch_import_starts_with_nocase(span_from_cstr("x"), "INF");
  assert(result == 0);
  result = po32_patch_import_contains(span_from_cstr("Noise FM"), "");
  assert(result == 0);
  result = po32_patch_import_contains(span_from_cstr("No"), "Noise");
  assert(result == 0);

  status = po32_patch_import_parse_leading_float(span_from_cstr("+"), &parsed);
  assert(status == PO32_ERR_PARSE);
  status = po32_patch_import_parse_leading_float(span_from_cstr("2.5e+"), &parsed);
  assert(status == PO32_OK);
  assert(abs_diff(parsed, 2.5f) < 0.0001f);
  status = po32_patch_import_parse_leading_float(span_from_cstr("123"), NULL);
  assert(status == PO32_ERR_INVALID_ARG);
}

static void test_parse_line_invalid_args(void) {
  int mod_mode_hint = 0;
  int nenv_mode_hint = 0;
  int recognized_fields = 0;
  po32_patch_params_t params;
  po32_status_t status;

  po32_patch_params_zero(&params);

  status = po32_patch_import_parse_line(span_from_cstr("OscWave"), span_from_cstr("Sine"), NULL,
                                        &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_INVALID_ARG);

  status = po32_patch_import_parse_line(span_from_cstr("OscWave"), span_from_cstr("Sine"),
                                        &mod_mode_hint, NULL, &recognized_fields, &params);
  assert(status == PO32_ERR_INVALID_ARG);

  status = po32_patch_import_parse_line(span_from_cstr("OscWave"), span_from_cstr("Sine"),
                                        &mod_mode_hint, &nenv_mode_hint, NULL, &params);
  assert(status == PO32_ERR_INVALID_ARG);

  status = po32_patch_import_parse_line(span_from_cstr("OscWave"), span_from_cstr("Sine"),
                                        &mod_mode_hint, &nenv_mode_hint, &recognized_fields, NULL);
  assert(status == PO32_ERR_INVALID_ARG);
}

int main(void) {
  test_import_strtof_edges();
  test_trim_and_match_helpers();
  test_parse_line_invalid_args();
  return 0;
}
