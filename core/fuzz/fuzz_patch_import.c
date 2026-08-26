/*
 * Fuzz harness for po32_patch_parse_mtdrum_text().
 *
 * The raw input is handed to the importer as-is (it takes an explicit
 * length, so out-of-bounds reads surface under ASan). On success every
 * parameter must land in [0, 1] and never be NaN, and the parsed patch must
 * survive the raw byte codec round trip byte-for-byte.
 */

#include "po32.h"
#include "fuzz_driver.h"

#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  po32_patch_params_t params, reparsed;
  uint8_t encoded[PO32_PARAM_COUNT * 2u];
  uint8_t reencoded[PO32_PARAM_COUNT * 2u];
  size_t encoded_len = 0u;
  size_t reencoded_len = 0u;

  if (po32_patch_parse_mtdrum_text((const char *)data, size, &params) != PO32_OK) {
    return 0;
  }

  /* Every parameter must be in [0, 1]; NaN fails both comparisons. */
#define FUZZ_CHECK_PARAM(name) fuzz_check(params.name >= 0.0f && params.name <= 1.0f);
  FUZZ_PATCH_PARAM_FIELDS(FUZZ_CHECK_PARAM)
#undef FUZZ_CHECK_PARAM

  fuzz_check(po32_encode_patch(&params, encoded, sizeof(encoded), &encoded_len) == PO32_OK);
  fuzz_check(encoded_len == sizeof(encoded));
  fuzz_check(po32_decode_patch(encoded, encoded_len, &reparsed) == PO32_OK);
  fuzz_check(po32_encode_patch(&reparsed, reencoded, sizeof(reencoded), &reencoded_len) == PO32_OK);
  fuzz_check(reencoded_len == encoded_len);
  fuzz_check(memcmp(encoded, reencoded, encoded_len) == 0);

  return 0;
}
