/*
 * Fuzz harness for po32_packet_decode() / po32_packet_encode().
 *
 * The first input byte picks the tag (biased toward the known tags, with an
 * escape hatch for arbitrary 16-bit tags). Whenever a payload decodes, the
 * result is re-encoded and decoded again: the second decode must succeed and
 * produce exactly the same typed packet (decode∘encode must be idempotent).
 */

#include "po32.h"
#include "fuzz_driver.h"

#include <string.h>

static const uint16_t fuzz_known_tags[] = {
    PO32_TAG_PATCH, PO32_TAG_KNOB, PO32_TAG_RESET, PO32_TAG_STATE, PO32_TAG_PATTERN,
};

#define FUZZ_KNOWN_TAG_COUNT (sizeof(fuzz_known_tags) / sizeof(fuzz_known_tags[0]))

typedef union {
  po32_patch_packet_t patch;
  po32_knob_packet_t knob;
  po32_reset_packet_t reset;
  po32_state_packet_t state;
  po32_pattern_packet_t pattern;
} fuzz_typed_packet_t;

static void fuzz_check_float01(float value) {
  fuzz_check(value >= 0.0f && value <= 1.0f);
}

static void fuzz_compare_patch(const po32_patch_packet_t *a, const po32_patch_packet_t *b) {
  fuzz_check(a->instrument >= 1u && a->instrument <= 16u);
  fuzz_check(a->instrument == b->instrument);
  fuzz_check(a->side == b->side);

#define FUZZ_CHECK_PARAM(name)                                                                     \
  fuzz_check_float01(a->params.name);                                                              \
  fuzz_check(a->params.name == b->params.name);
  FUZZ_PATCH_PARAM_FIELDS(FUZZ_CHECK_PARAM)
#undef FUZZ_CHECK_PARAM
}

static void fuzz_compare_knob(const po32_knob_packet_t *a, const po32_knob_packet_t *b) {
  fuzz_check(a->instrument >= 1u && a->instrument <= 16u);
  fuzz_check(a->instrument == b->instrument);
  fuzz_check(a->kind == b->kind);
  fuzz_check(a->value == b->value);
}

static void fuzz_compare_state(const po32_state_packet_t *a, const po32_state_packet_t *b) {
  fuzz_check(a->pattern_count <= PO32_PATTERN_STEP_COUNT);
  fuzz_check(a->pattern_count == b->pattern_count);
  fuzz_check(a->tempo == b->tempo);
  fuzz_check(a->swing_times_12 == b->swing_times_12);
  for (size_t i = 0u; i < PO32_STATE_MORPH_PAIR_COUNT; ++i) {
    fuzz_check(a->morph_pairs[i].flag == b->morph_pairs[i].flag);
    fuzz_check(a->morph_pairs[i].morph == b->morph_pairs[i].morph);
  }
  for (size_t i = 0u; i < a->pattern_count; ++i) {
    fuzz_check(a->pattern_numbers[i] == b->pattern_numbers[i]);
  }
}

static void fuzz_compare_pattern(const po32_pattern_packet_t *a, const po32_pattern_packet_t *b) {
  fuzz_check(a->pattern_number == b->pattern_number);
  fuzz_check(a->accent_bits == b->accent_bits);
  for (size_t i = 0u; i < (size_t)PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT; ++i) {
    if (a->steps[i].instrument != 0u) {
      fuzz_check(a->steps[i].instrument >= 1u && a->steps[i].instrument <= 16u);
      fuzz_check(a->steps[i].fill_rate >= 1u && a->steps[i].fill_rate <= 15u);
    }
    fuzz_check(a->steps[i].instrument == b->steps[i].instrument);
    fuzz_check(a->steps[i].fill_rate == b->steps[i].fill_rate);
    fuzz_check(a->steps[i].accent == b->steps[i].accent);
    fuzz_check(a->morph_lanes[i].flag == b->morph_lanes[i].flag);
    fuzz_check(a->morph_lanes[i].morph == b->morph_lanes[i].morph);
  }
  for (size_t i = 0u; i < PO32_PATTERN_RESERVED_COUNT; ++i) {
    fuzz_check(a->reserved[i] == b->reserved[i]);
  }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  fuzz_typed_packet_t first, second;
  po32_packet_t wire;
  uint16_t tag;

  if (size < 3u) {
    return 0;
  }

  if (data[0] < 0xF0u) {
    tag = fuzz_known_tags[data[0] % FUZZ_KNOWN_TAG_COUNT];
  } else {
    tag = (uint16_t)data[1] | (uint16_t)((uint16_t)data[2] << 8);
  }
  data += 3u;
  size -= 3u;

  memset(&first, 0, sizeof(first));
  if (po32_packet_decode(tag, data, size, &first) != PO32_OK) {
    return 0;
  }

  fuzz_check(po32_packet_encode(tag, &first, &wire) == PO32_OK);
  fuzz_check(wire.tag_code == tag);
  fuzz_check(wire.payload_len <= PO32_MAX_PAYLOAD);

  memset(&second, 0, sizeof(second));
  fuzz_check(po32_packet_decode(tag, wire.payload, wire.payload_len, &second) == PO32_OK);

  switch (tag) {
  case PO32_TAG_PATCH:
    fuzz_compare_patch(&first.patch, &second.patch);
    break;
  case PO32_TAG_KNOB:
    fuzz_compare_knob(&first.knob, &second.knob);
    break;
  case PO32_TAG_RESET:
    fuzz_check(first.reset.instrument >= 1u && first.reset.instrument <= 16u);
    fuzz_check(first.reset.instrument == second.reset.instrument);
    break;
  case PO32_TAG_STATE:
    fuzz_compare_state(&first.state, &second.state);
    break;
  case PO32_TAG_PATTERN:
    fuzz_compare_pattern(&first.pattern, &second.pattern);
    break;
  default:
    /* Unknown tags must have been rejected by the first decode. */
    fuzz_check(0);
    break;
  }

  return 0;
}
