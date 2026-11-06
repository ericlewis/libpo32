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
typedef po_status_t (*po33_pool_topology_build_fn_t)(const uint8_t *frame, size_t frame_len,
                                                     po33_sound_record_t *sound_records,
                                                     size_t sound_record_capacity,
                                                     po33_pool_t *pools, size_t pool_capacity,
                                                     po33_pool_reference_t *references,
                                                     size_t reference_capacity);
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
  assert(sizeof((po33_pool_topology_build_fn_t){po33_pool_topology_build}) ==
         sizeof(po33_pool_topology_build_fn_t));
}

typedef struct topology_fixture {
  uint8_t frame[PO33_PREAMBLE_BYTES + 512u];
  size_t length;
  size_t split_selection_offset;
  size_t split_data_offset;
  size_t split_last_data_offset;
  size_t extension_data_offset;
  size_t extension_last_data_offset;
} topology_fixture_t;

static void write32(uint8_t *out, uint32_t value) {
  for (unsigned int i = 0; i < 4; ++i)
    out[i] = (uint8_t)(value >> (8u * i));
}

static void topology_header(uint8_t out[PO33_SOUND_HEADER_BYTES], uint32_t start, uint32_t end,
                            uint8_t source) {
  write32(out, start);
  write32(out + 4, end);
  out[8] = source;
}

static void topology_append(po33_builder_t *builder, uint16_t tag, const uint8_t *payload,
                            size_t payload_len, size_t *packet_offset) {
  assert(po33_builder_append_packet(builder, tag, payload, payload_len, packet_offset) == PO_OK);
}

static void make_topology_fixture(topology_fixture_t *fixture) {
  po33_builder_t builder;
  uint8_t data[PO33_SOUND_HEADER_BYTES + 2u], selector;
  const uint8_t extension[] = {0x5a, 0x51, 0x50};

  assert(po33_builder_init(&builder, fixture->frame, sizeof(fixture->frame)) == PO_OK);

  selector = 7u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, &fixture->split_selection_offset);
  topology_header(data, UINT32_C(0x10203040), UINT32_C(0x50607080), 7u);
  data[9] = 0x91u;
  data[10] = 0x92u;
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, 4u, &fixture->split_data_offset);
  topology_append(&builder, PO33_TAG_SOUND_DATA, data + 4u, 7u, &fixture->split_last_data_offset);

  selector = 8u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 11u, 22u, 7u);
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, PO33_SOUND_HEADER_BYTES, NULL);

  selector = 9u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 33u, 44u, 9u);
  data[9] = 0x93u;
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, 10u, NULL);

  /* Adjacent raw selectors retain distinct owner pools; no drum mapping is inferred. */
  selector = 10u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 55u, 66u, 10u);
  data[9] = 0x94u;
  data[10] = 0x95u;
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, 3u, &fixture->extension_data_offset);
  topology_append(&builder, 0x4321u, extension, sizeof(extension), NULL);
  topology_append(&builder, PO33_TAG_SOUND_DATA, data + 3u, 8u,
                  &fixture->extension_last_data_offset);

  selector = 9u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 77u, 88u, 10u);
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, PO33_SOUND_HEADER_BYTES, NULL);

  selector = 12u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 99u, 111u, 12u);
  data[9] = 0x96u;
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, 10u, NULL);

  selector = 12u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 112u, 123u, 12u);
  data[9] = 0x97u;
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, 10u, NULL);

  selector = 13u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 124u, 135u, 12u);
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, PO33_SOUND_HEADER_BYTES, NULL);

  selector = 14u;
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_header(data, 136u, 147u, 99u);
  topology_append(&builder, PO33_TAG_SOUND_DATA, data, PO33_SOUND_HEADER_BYTES, NULL);

  assert(po33_builder_finish(&builder, &fixture->length) == PO_OK);
}

static int count_packet(const po_packet_t *packet, void *user) {
  size_t *count = (size_t *)user;
  (void)packet;
  ++*count;
  return 0;
}

static void test_pool_topology(void) {
  topology_fixture_t fixture;
  po33_sound_record_t records[10], records_before[10];
  po33_pool_t pools[6], pools_before[6];
  po33_pool_reference_t references[6], references_before[6];
  uint8_t saved[PO33_PREAMBLE_BYTES + 512u];
  po33_decode_result_t before, after;
  size_t records_required = 0u, pools_required = 0u, references_required = 0u;
  size_t before_packets = 0u, after_packets = 0u;

  make_topology_fixture(&fixture);
  memcpy(saved, fixture.frame, fixture.length);
  assert(po33_frame_parse(fixture.frame, fixture.length, count_packet, &before_packets, &before) ==
         PO_OK);
  assert(before.done);
  assert(po33_pool_topology_requirements(fixture.frame, fixture.length, &records_required,
                                         &pools_required, &references_required) == PO_OK);
  assert(records_required == 9u && pools_required == 5u && references_required == 5u);

  memset(records, 0xa5, sizeof(records));
  memset(pools, 0xa5, sizeof(pools));
  memset(references, 0xa5, sizeof(references));
  memcpy(records_before, records, sizeof(records));
  memcpy(pools_before, pools, sizeof(pools));
  memcpy(references_before, references, sizeof(references));

  assert(po33_pool_topology_build(fixture.frame, fixture.length, NULL, 0u, pools, pools_required,
                                  references, references_required) == PO_ERR_INVALID_ARG);
  assert(memcmp(records, records_before, sizeof(records)) == 0);
  assert(memcmp(pools, pools_before, sizeof(pools)) == 0);
  assert(memcmp(references, references_before, sizeof(references)) == 0);
  assert(po33_pool_topology_build(fixture.frame, fixture.length, records, records_required - 1u,
                                  pools, pools_required, references,
                                  references_required) == PO_ERR_BUFFER_TOO_SMALL);
  assert(po33_pool_topology_build(fixture.frame, fixture.length, records, records_required, pools,
                                  pools_required - 1u, references,
                                  references_required) == PO_ERR_BUFFER_TOO_SMALL);
  assert(po33_pool_topology_build(fixture.frame, fixture.length, records, records_required, pools,
                                  pools_required, references,
                                  references_required - 1u) == PO_ERR_BUFFER_TOO_SMALL);
  assert(memcmp(records, records_before, sizeof(records)) == 0);
  assert(memcmp(pools, pools_before, sizeof(pools)) == 0);
  assert(memcmp(references, references_before, sizeof(references)) == 0);

  assert(po33_pool_topology_build(fixture.frame, fixture.length, records,
                                  sizeof(records) / sizeof(*records), pools,
                                  sizeof(pools) / sizeof(*pools), references,
                                  sizeof(references) / sizeof(*references)) == PO_OK);
  assert(memcmp(records + records_required, records_before + records_required,
                sizeof(records) - records_required * sizeof(*records)) == 0);
  assert(memcmp(pools + pools_required, pools_before + pools_required,
                sizeof(pools) - pools_required * sizeof(*pools)) == 0);
  assert(memcmp(references + references_required, references_before + references_required,
                sizeof(references) - references_required * sizeof(*references)) == 0);

  assert(records[0].selector == 7u && records[0].range_start_raw == UINT32_C(0x10203040) &&
         records[0].range_end_raw == UINT32_C(0x50607080) && records[0].source_selector == 7u);
  assert(records[0].selection_span.offset == fixture.split_selection_offset &&
         records[0].selection_span.length == 6u &&
         records[0].data_span.offset == fixture.split_data_offset &&
         records[0].data_span.length ==
             fixture.split_last_data_offset + 12u - fixture.split_data_offset &&
         records[0].owned_pool_index == 0u && records[0].referenced_pool_index == 0u &&
         records[0].flags == 0u);
  assert(records[1].flags == PO33_SOUND_RECORD_FLAG_HEADER_ONLY &&
         records[1].owned_pool_index == SIZE_MAX && records[1].referenced_pool_index == 0u);
  assert(records[2].flags == PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR &&
         records[2].owned_pool_index == 1u && records[2].referenced_pool_index == 1u);
  assert(records[3].owned_pool_index == 2u && records[3].referenced_pool_index == 2u &&
         records[3].flags == PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION &&
         records[3].data_span.offset == fixture.extension_data_offset &&
         records[3].data_span.length ==
             fixture.extension_last_data_offset + 13u - fixture.extension_data_offset);
  assert(records[4].flags ==
             (PO33_SOUND_RECORD_FLAG_HEADER_ONLY | PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR) &&
         records[4].referenced_pool_index == 2u);
  assert(records[5].flags == (PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR |
                              PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS) &&
         records[5].owned_pool_index == 3u && records[5].referenced_pool_index == SIZE_MAX);
  assert(records[6].flags == (PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR |
                              PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS) &&
         records[6].owned_pool_index == 4u && records[6].referenced_pool_index == SIZE_MAX);
  assert(records[7].flags ==
             (PO33_SOUND_RECORD_FLAG_HEADER_ONLY | PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS) &&
         records[7].referenced_pool_index == SIZE_MAX);
  assert(records[8].flags ==
             (PO33_SOUND_RECORD_FLAG_HEADER_ONLY | PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED) &&
         records[8].referenced_pool_index == SIZE_MAX);

  assert(pools[0].owner_record_index == 0u && pools[0].data_bytes == 11u);
  assert(pools[1].owner_record_index == 2u && pools[1].data_bytes == 10u);
  assert(pools[2].owner_record_index == 3u && pools[2].data_bytes == 11u);
  assert(pools[3].owner_record_index == 5u && pools[3].data_bytes == 10u);
  assert(pools[4].owner_record_index == 6u && pools[4].data_bytes == 10u);
  assert(references[0].pool_index == 0u && references[0].sound_record_index == 0u);
  assert(references[1].pool_index == 0u && references[1].sound_record_index == 1u);
  assert(references[2].pool_index == 1u && references[2].sound_record_index == 2u);
  assert(references[3].pool_index == 2u && references[3].sound_record_index == 3u);
  assert(references[4].pool_index == 2u && references[4].sound_record_index == 4u);

  assert(memcmp(saved, fixture.frame, fixture.length) == 0);
  assert(po33_frame_parse(fixture.frame, fixture.length, count_packet, &after_packets, &after) ==
         PO_OK);
  assert(after.done && after_packets == before_packets && after.frame_bytes == before.frame_bytes &&
         after.final_state == before.final_state);
}

static void test_pool_topology_failures(void) {
  uint8_t empty[PO33_PREAMBLE_BYTES + 16u];
  uint8_t malformed[PO33_PREAMBLE_BYTES + 64u] = {0};
  po33_builder_t builder;
  po33_sound_record_t records[1], records_before[1];
  po33_pool_t pools[1], pools_before[1];
  po33_pool_reference_t references[1], references_before[1];
  uint8_t selector = 7u, partial_header[PO33_SOUND_HEADER_BYTES - 1u] = {0};
  size_t length, records_required = 31u, pools_required = 32u, references_required = 33u;

  assert(po33_pool_topology_requirements(NULL, 0u, &records_required, &pools_required,
                                         &references_required) == PO_ERR_INVALID_ARG);
  assert(records_required == 31u && pools_required == 32u && references_required == 33u);
  assert(po33_pool_topology_requirements(malformed, 0u, NULL, &pools_required,
                                         &references_required) == PO_ERR_INVALID_ARG);
  assert(pools_required == 32u && references_required == 33u);

  assert(po33_builder_init(&builder, empty, sizeof(empty)) == PO_OK);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  assert(po33_pool_topology_requirements(empty, length, &records_required, &pools_required,
                                         &references_required) == PO_OK);
  assert(records_required == 0u && pools_required == 0u && references_required == 0u);
  assert(po33_pool_topology_build(empty, length, NULL, 0u, NULL, 0u, NULL, 0u) == PO_OK);

  records_required = 31u;
  pools_required = 32u;
  references_required = 33u;
  assert(po33_builder_init(&builder, malformed, sizeof(malformed)) == PO_OK);
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_append(&builder, PO33_TAG_SOUND_DATA, partial_header, sizeof(partial_header), NULL);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  assert(po33_pool_topology_requirements(malformed, length, &records_required, &pools_required,
                                         &references_required) == PO_ERR_FRAME);
  assert(records_required == 31u && pools_required == 32u && references_required == 33u);
  assert(po33_builder_init(&builder, malformed, sizeof(malformed)) == PO_OK);
  topology_append(&builder, PO33_TAG_SOUND_DATA, partial_header, sizeof(partial_header), NULL);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  assert(po33_pool_topology_requirements(malformed, length, &records_required, &pools_required,
                                         &references_required) == PO_ERR_FRAME);
  assert(records_required == 31u && pools_required == 32u && references_required == 33u);
  assert(po33_builder_init(&builder, malformed, sizeof(malformed)) == PO_OK);
  topology_append(&builder, PO33_TAG_SOUND_SELECT, NULL, 0u, NULL);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  assert(po33_pool_topology_requirements(malformed, length, &records_required, &pools_required,
                                         &references_required) == PO_ERR_FRAME);
  assert(records_required == 31u && pools_required == 32u && references_required == 33u);

  assert(po33_builder_init(&builder, malformed, sizeof(malformed)) == PO_OK);
  topology_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  topology_append(&builder, PO33_TAG_SOUND_DATA, partial_header, sizeof(partial_header), NULL);
  assert(po33_builder_finish(&builder, &length) == PO_OK);

  memset(records, 0xa5, sizeof(records));
  memset(pools, 0xa5, sizeof(pools));
  memset(references, 0xa5, sizeof(references));
  memcpy(records_before, records, sizeof(records));
  memcpy(pools_before, pools, sizeof(pools));
  memcpy(references_before, references, sizeof(references));
  assert(po33_pool_topology_build(malformed, length, records, 1u, pools, 1u, references, 1u) ==
         PO_ERR_FRAME);
  assert(memcmp(records, records_before, sizeof(records)) == 0);
  assert(memcmp(pools, pools_before, sizeof(pools)) == 0);
  assert(memcmp(references, references_before, sizeof(references)) == 0);
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
  test_pool_topology();
  test_pool_topology_failures();
  test_frames();
  test_packing();
  test_samples();
  puts("PO-33 tests passed");
  return 0;
}
