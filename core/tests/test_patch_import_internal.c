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

static void test_strtof_overflow_underflow(void) {
  char *endptr = NULL;
  float value;

  /* positive exponent > 38 but < saturation → clamps to FLT_MAX */
  value = po32_import_strtof("1.5e+39", &endptr);
  assert(value >= 3.4028235e+38f);
  assert(*endptr == '\0');

  value = po32_import_strtof("-1.5e+39", &endptr);
  assert(value <= -3.4028235e+38f);
  assert(*endptr == '\0');

  /* negative exponent > 45 but < saturation → underflows to 0 */
  value = po32_import_strtof("1.5e-46", &endptr);
  assert(value == 0.0f);
  assert(*endptr == '\0');
}

static void test_starts_with_nocase_uppercase_literal(void) {
  int result;

  /* value shorter than literal */
  result = po32_patch_import_starts_with_nocase(span_from_cstr("in"), "INF");
  assert(result == 0);

  /* value matches with uppercase literal bytes → lowercase conversion path */
  result = po32_patch_import_starts_with_nocase(span_from_cstr("INF ms"), "INF");
  assert(result == 1);
}

static void test_contains_edge_cases(void) {
  int result;

  /* literal longer than value */
  result = po32_patch_import_contains(span_from_cstr("ab"), "abcdef");
  assert(result == 0);

  /* empty literal */
  result = po32_patch_import_contains(span_from_cstr("test"), "");
  assert(result == 0);
}

static void test_parse_leading_float_non_number(void) {
  float parsed = 99.0f;
  po32_status_t status;

  /* completely non-numeric input */
  status = po32_patch_import_parse_leading_float(span_from_cstr("abc"), &parsed);
  assert(status == PO32_ERR_PARSE);
}

static void test_parse_line_field_errors(void) {
  int mod_mode_hint = 0;
  int nenv_mode_hint = 0;
  int recognized_fields = 0;
  po32_patch_params_t params;
  po32_status_t status;

  po32_patch_params_zero(&params);

  /* ModAmt unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("ModAmt"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NFilMod invalid */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NFilMod"), span_from_cstr("XX"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NFilFrq negative */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NFilFrq"), span_from_cstr("-100 Hz"),
                                   &mod_mode_hint, &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NFilQ negative */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NFilQ"), span_from_cstr("-10"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NEnvMod invalid */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NEnvMod"), span_from_cstr("Chaos"),
                                   &mod_mode_hint, &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NEnvAtk unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NEnvAtk"), span_from_cstr("nope"),
                                   &mod_mode_hint, &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NEnvDcy negative */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NEnvDcy"), span_from_cstr("-100 ms"),
                                   &mod_mode_hint, &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* Mix unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("Mix"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* DistAmt unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("DistAmt"), span_from_cstr("nope"),
                                   &mod_mode_hint, &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* EQFreq negative */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("EQFreq"), span_from_cstr("-100 Hz"),
                                   &mod_mode_hint, &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* EQGain unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("EQGain"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* Level unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("Level"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* OscVel unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("OscVel"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* NVel unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("NVel"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);

  /* ModVel unparseable */
  recognized_fields = 0;
  status =
      po32_patch_import_parse_line(span_from_cstr("ModVel"), span_from_cstr("nope"), &mod_mode_hint,
                                   &nenv_mode_hint, &recognized_fields, &params);
  assert(status == PO32_ERR_PARSE);
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
  test_strtof_overflow_underflow();
  test_trim_and_match_helpers();
  test_starts_with_nocase_uppercase_literal();
  test_contains_edge_cases();
  test_parse_leading_float_non_number();
  test_parse_line_field_errors();
  test_parse_line_invalid_args();
  return 0;
}
