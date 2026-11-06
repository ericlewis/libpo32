#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include "po32.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t frame[PO33_PREAMBLE_BYTES + 1024];
static size_t length, received, stop_after;
static uint16_t state;
static void byte(uint8_t raw) {
  frame[length++] = po32_whiten_byte(state, raw);
  state = po32_crc_update(state, raw);
}
static void packet(uint16_t tag, size_t n) {
  uint16_t trailer;
  byte((uint8_t)tag);
  byte((uint8_t)(tag >> 8));
  byte((uint8_t)n);
  for (size_t i = 0; i < n; ++i)
    byte((uint8_t)(i ^ 0xa5));
  trailer = state;
  byte((uint8_t)trailer);
  byte((uint8_t)(trailer >> 8));
}
static void begin(void) {
  const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d};
  for (size_t i = 0; i < PO33_PREAMBLE_BYTES - 4; ++i)
    frame[i] = (uint8_t)i;
  memcpy(frame + PO33_PREAMBLE_BYTES - 4, sync, 4);
  length = PO33_PREAMBLE_BYTES;
  state = PO32_INITIAL_STATE;
  packet(PO33_TAG_BEGIN, 0);
}
static int collect(const po32_packet_t *p, void *user) {
  (void)user;
  assert(p->trailer.matches_state);
  if (p->tag_code == 0x1234) {
    assert(p->offset == PO33_PREAMBLE_BYTES + 5 && p->payload_len == 255);
    for (size_t i = 0; i < p->payload_len; ++i)
      assert(p->payload[i] == (uint8_t)(i ^ 0xa5));
  }
  return ++received == stop_after;
}
typedef po_status_t (*po33_pool_topology_requirements_fn_t)(const uint8_t *frame, size_t frame_len,
                                                            size_t *sound_record_count,
                                                            size_t *pool_count,
                                                            size_t *reference_count);
static void test_pool_topology_api_surface(void) {
  const po33_sound_record_t record = {
      .selector = 7u,
      .range_start_raw = UINT32_C(0x10203040),
      .range_end_raw = UINT32_C(0x50607080),
      .source_selector = 3u,
      .flags = PO33_SOUND_RECORD_FLAG_HEADER_ONLY | PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED,
      .selection_span = {PO33_PREAMBLE_BYTES + 5u, 6u},
      .data_span = {PO33_PREAMBLE_BYTES + 11u, 14u},
      .owned_pool_index = SIZE_MAX,
      .referenced_pool_index = SIZE_MAX,
  };
  const po33_pool_t pool = {
      .owner_record_index = 0u,
      .data_span = {PO33_PREAMBLE_BYTES + 11u, 4110u},
      .data_bytes = 4105u,
  };
  const po33_pool_reference_t reference = {
      .pool_index = 0u,
      .sound_record_index = 1u,
  };
  const unsigned int flags =
      PO33_SOUND_RECORD_FLAG_HEADER_ONLY | PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR |
      PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION | PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED |
      PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS;

  assert(offsetof(po33_frame_span_t, offset) == 0u);
  assert(offsetof(po33_sound_record_t, selector) == 0u);
  assert(offsetof(po33_sound_record_t, selection_span) < offsetof(po33_sound_record_t, data_span));
  assert(offsetof(po33_sound_record_t, data_span) <
         offsetof(po33_sound_record_t, owned_pool_index));
  assert(offsetof(po33_sound_record_t, owned_pool_index) <
         offsetof(po33_sound_record_t, referenced_pool_index));
  assert(flags == 0x1fu);
  assert(record.selection_span.offset == PO33_PREAMBLE_BYTES + 5u);
  assert(record.data_span.length == 14u);
  assert(record.owned_pool_index == SIZE_MAX && record.referenced_pool_index == SIZE_MAX);
  assert(pool.owner_record_index == 0u && pool.data_bytes == 4105u);
  assert(reference.pool_index == 0u && reference.sound_record_index == 1u);

  /* sizeof does not evaluate this fixed-size compound literal. Its initializer
   * verifies the public function-pointer signature without invoking or
   * requiring a topology parser at link or run time. */
  assert(sizeof((po33_pool_topology_requirements_fn_t){po33_pool_topology_requirements}) ==
         sizeof(po33_pool_topology_requirements_fn_t));
}
static void test_frames(void) {
  po33_decode_result_t result;
  /* First packet independently recovered from the official recording. */
  const uint8_t golden[] = {0x6a, 0x9a, 0x34, 0xe2, 0x48};
  begin();
  assert(memcmp(frame + PO33_PREAMBLE_BYTES, golden, sizeof(golden)) == 0);
  packet(0x1234, 255);
  packet(PO33_TAG_END, 0);
  assert(po33_frame_parse(frame, length, collect, NULL, &result) == PO32_OK);
  assert(result.done && !result.stopped && result.packet_count == 3 && received == 3);
  assert(result.frame_bytes == length && result.final_state == state);
  frame[length] = 0xaa;
  assert(po33_frame_parse(frame, length + 1, NULL, NULL, &result) == PO32_OK);
  assert(result.frame_bytes == length);
  received = 0;
  stop_after = 2;
  assert(po33_frame_parse(frame, length, collect, NULL, &result) == PO32_OK);
  assert(!result.done && result.stopped && result.packet_count == 2);
  for (size_t n = 0; n < length; ++n) {
    assert(po33_frame_parse(frame, n, NULL, NULL, &result) == PO32_ERR_FRAME);
    assert(!result.done);
  }
  for (size_t i = 0; i < length; ++i) {
    frame[i] ^= 1;
    assert(po33_frame_parse(frame, length, NULL, NULL, &result) == PO32_ERR_FRAME);
    assert(!result.done);
    frame[i] ^= 1;
  }
  assert(po33_frame_parse(NULL, length, NULL, NULL, &result) == PO32_ERR_INVALID_ARG);
  begin();
  packet(PO33_TAG_BEGIN, 0);
  packet(PO33_TAG_END, 0);
  assert(po33_frame_parse(frame, length, NULL, NULL, &result) == PO32_ERR_FRAME);
  begin();
  packet(PO33_TAG_END, 1);
  assert(po33_frame_parse(frame, length, NULL, NULL, &result) == PO32_ERR_FRAME);
}
static void test_packing(void) {
  const uint8_t phases[] = {0, 0, 0, 1, 0, 3, 0, 2, 1, 2, 2, 3, 3, 1};
  const uint8_t expected[] = {0x21, 0x43, 0x65};
  const uint8_t bad[] = {0, 0, 0, 4, 0, 0};
  uint8_t out[8] = {0};
  size_t n;
  assert(po33_pack_symbols(phases, 7, 0, out, sizeof(out), &n) == PO32_OK);
  assert(n == 3 && memcmp(out, expected, n) == 0);
  assert(po33_pack_symbols(phases, 7, 1, out, sizeof(out), &n) == PO32_OK);
  assert(n == 2 && out[0] == 0x32 && out[1] == 0x54);
  out[0] = 0xaa;
  assert(po33_pack_symbols(phases, 7, 0, out, 2, &n) == PO32_ERR_BUFFER_TOO_SMALL);
  assert(n == 3 && out[0] == 0xaa);
  assert(po33_pack_symbols(phases, 7, 2, out, sizeof(out), &n) == PO32_ERR_INVALID_ARG);
  assert(po33_pack_symbols(NULL, 0, 0, NULL, 0, &n) == PO32_OK && n == 0);
  assert(po33_pack_symbols(phases, 1, 0, NULL, 0, &n) == PO32_OK && n == 0);
  assert(po33_pack_symbols(bad, 3, 0, out, sizeof(out), &n) == PO32_ERR_RANGE);
}
static void test_samples(void) {
  const uint8_t input[] = {0, 1, 15, 16, 31, 32, 63, 64, 127, 128, 254, 255};
  const int16_t expected[] = {0, 8, 120, 132, 372, 396, 1884, 1980, 32124, -32124, -8, 0};
  for (size_t i = 0; i < sizeof(input); ++i)
    assert(po33_sample_decode(input[i]) == expected[i]);
  for (unsigned i = 0; i < 128; ++i) {
    assert(po33_sample_decode((uint8_t)i) == -po33_sample_decode((uint8_t)(255 - i)));
    if (i)
      assert(po33_sample_decode((uint8_t)i) > po33_sample_decode((uint8_t)(i - 1)));
  }
}
int main(void) {
  test_pool_topology_api_surface();
  test_frames();
  test_packing();
  test_samples();
  puts("PO-33 tests passed");
  return 0;
}
