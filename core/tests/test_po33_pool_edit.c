#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "po33.h"

#define PO33_TEST_PACKET_OVERHEAD_BYTES 5u

enum pool_edit_fixture_flags {
  POOL_EDIT_FIXTURE_ALIAS = 0x01u,
  POOL_EDIT_FIXTURE_HEADER_ONLY_TARGET = 0x02u,
  POOL_EDIT_FIXTURE_UNKNOWN_EXTENSION = 0x04u,
  POOL_EDIT_FIXTURE_DUPLICATE_TARGET = 0x08u,
  POOL_EDIT_FIXTURE_AMBIGUOUS_TARGET_SOURCE = 0x10u
};

typedef struct pool_edit_fixture {
  uint8_t frame[30000];
  size_t length;
  po33_sound_record_t records[8];
  po33_pool_t pools[6];
  po33_pool_reference_t references[8];
  size_t record_count;
  size_t pool_count;
  size_t reference_count;
} pool_edit_fixture_t;

typedef po_status_t (*po33_pool_edit_plan_fn_t)(const uint8_t *frame, size_t frame_len,
                                                size_t target_sound_record_index,
                                                const uint8_t *samples, size_t sample_bytes,
                                                po33_pool_edit_plan_t *plan);
typedef po_status_t (*po33_pool_edit_rebuild_fn_t)(const uint8_t *frame, size_t frame_len,
                                                   size_t target_sound_record_index,
                                                   const uint8_t *samples, size_t sample_bytes,
                                                   const po33_pool_edit_plan_t *plan,
                                                   uint8_t *workspace, size_t workspace_bytes,
                                                   uint8_t *out_frame, size_t out_capacity,
                                                   size_t *out_frame_bytes);

typedef struct pool_edit_packet_list {
  po_packet_t items[512];
  size_t count;
} pool_edit_packet_list_t;
static void pool_edit_write_u32le(uint8_t *out, uint32_t value) {
  for (unsigned int index = 0u; index < 4u; ++index)
    out[index] = (uint8_t)(value >> (8u * index));
}

static void pool_edit_append(po33_builder_t *builder, uint16_t tag_code, const uint8_t *payload,
                             size_t payload_bytes) {
  po_status_t status = po33_builder_append_packet(builder, tag_code, payload, payload_bytes, NULL);
  assert(status == PO_OK);
}

static void pool_edit_append_sound(po33_builder_t *builder, uint8_t selector, uint8_t source,
                                   size_t allocation, int split_header, int unknown_extension) {
  uint8_t header[PO33_SOUND_HEADER_BYTES];
  uint8_t payload[255] = {0};
  uint8_t unknown = 0xa5u;
  size_t logical_offset = 0u;
  size_t total = PO33_SOUND_HEADER_BYTES + allocation;

  assert(allocation <= UINT32_MAX);
  pool_edit_write_u32le(header, 0u);
  pool_edit_write_u32le(header + 4u, (uint32_t)allocation);
  header[8] = source;
  pool_edit_append(builder, PO33_TAG_SOUND_SELECT, &selector, 1u);
  while (logical_offset < total) {
    size_t payload_bytes = total - logical_offset;
    if (split_header && logical_offset == 0u)
      payload_bytes = 4u;
    else if (split_header && logical_offset == 4u)
      payload_bytes = 5u;
    else if (payload_bytes > sizeof(payload))
      payload_bytes = sizeof(payload);
    for (size_t index = 0u; index < payload_bytes; ++index) {
      size_t logical_index = logical_offset + index;
      payload[index] = logical_index < PO33_SOUND_HEADER_BYTES
                           ? header[logical_index]
                           : (uint8_t)(logical_index - PO33_SOUND_HEADER_BYTES);
    }
    pool_edit_append(builder, PO33_TAG_SOUND_DATA, payload, payload_bytes);
    logical_offset += payload_bytes;
    if (unknown_extension && logical_offset != total) {
      pool_edit_append(builder, 0x4321u, &unknown, 1u);
      unknown_extension = 0;
    }
  }
}

static void pool_edit_make_fixture(pool_edit_fixture_t *fixture, unsigned int flags,
                                   size_t target_allocation) {
  po33_builder_t builder;
  const uint8_t raw[] = {0x11u, 0x22u, 0x33u};
  uint8_t target_source = flags & POOL_EDIT_FIXTURE_AMBIGUOUS_TARGET_SOURCE ? 9u : 7u;

  memset(fixture, 0, sizeof(*fixture));
  assert(po33_builder_init(&builder, fixture->frame, sizeof(fixture->frame)) == PO_OK);
  pool_edit_append(&builder, PO33_TAG_SETTINGS, raw, sizeof(raw));
  pool_edit_append(&builder, 0x7badu, raw, sizeof(raw));
  pool_edit_append_sound(&builder, 7u, target_source,
                         flags & POOL_EDIT_FIXTURE_HEADER_ONLY_TARGET ? 0u : target_allocation, 1,
                         (flags & POOL_EDIT_FIXTURE_UNKNOWN_EXTENSION) != 0u);
  if (flags & POOL_EDIT_FIXTURE_ALIAS)
    pool_edit_append_sound(&builder, 8u, 7u, 0u, 0, 0);
  if (flags & POOL_EDIT_FIXTURE_DUPLICATE_TARGET)
    pool_edit_append_sound(&builder, 7u, 7u, PO33_POOL_ALLOCATION_BLOCK_BYTES, 0, 0);
  if (flags & POOL_EDIT_FIXTURE_AMBIGUOUS_TARGET_SOURCE) {
    pool_edit_append_sound(&builder, 9u, 9u, PO33_POOL_ALLOCATION_BLOCK_BYTES, 0, 0);
    pool_edit_append_sound(&builder, 9u, 9u, PO33_POOL_ALLOCATION_BLOCK_BYTES, 0, 0);
  }
  assert(po33_builder_finish(&builder, &fixture->length) == PO_OK);
  assert(po33_pool_topology_requirements(fixture->frame, fixture->length, &fixture->record_count,
                                         &fixture->pool_count, &fixture->reference_count) == PO_OK);
  assert(fixture->record_count <= sizeof(fixture->records) / sizeof(fixture->records[0]) &&
         fixture->pool_count <= sizeof(fixture->pools) / sizeof(fixture->pools[0]) &&
         fixture->reference_count <= sizeof(fixture->references) / sizeof(fixture->references[0]));
  assert(po33_pool_topology_build(fixture->frame, fixture->length, fixture->records,
                                  fixture->record_count, fixture->pools, fixture->pool_count,
                                  fixture->references, fixture->reference_count) == PO_OK);
}

static size_t pool_edit_make_malformed_frame(uint8_t *frame, size_t capacity) {
  po33_builder_t builder;
  uint8_t selector = 7u;
  uint8_t partial_header[PO33_SOUND_HEADER_BYTES - 1u] = {0};
  size_t length = 0u;
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  pool_edit_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u);
  pool_edit_append(&builder, PO33_TAG_SOUND_DATA, partial_header, sizeof(partial_header));
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static void pool_edit_assert_replacement_equal(const po33_pool_replacement_plan_t *left,
                                               const po33_pool_replacement_plan_t *right) {
  assert(left->target_pool_index == right->target_pool_index &&
         left->requested_sample_bytes == right->requested_sample_bytes &&
         left->existing_allocation_bytes == right->existing_allocation_bytes &&
         left->required_allocation_bytes == right->required_allocation_bytes &&
         left->required_growth_bytes == right->required_growth_bytes &&
         left->reference_count == right->reference_count &&
         left->affected_sound_record_count == right->affected_sound_record_count &&
         left->existing_allocation_fits == right->existing_allocation_fits &&
         left->growth_required == right->growth_required &&
         left->growth_structurally_eligible == right->growth_structurally_eligible &&
         left->reason == right->reason);
}

static void pool_edit_assert_neutral(const po33_pool_edit_plan_t *plan) {
  assert(plan->required_workspace_bytes == 0u && plan->required_frame_bytes == 0u &&
         plan->target_sound_record_index == SIZE_MAX && plan->target_data_span.offset == 0u &&
         plan->target_data_span.length == 0u && plan->target_selector == UINT8_MAX &&
         plan->replacement.target_pool_index == SIZE_MAX &&
         plan->replacement.requested_sample_bytes == 0u &&
         plan->replacement.reason == PO33_POOL_REPLACEMENT_REASON_NONE);
}

static void pool_edit_assert_plan_parity(pool_edit_fixture_t *fixture, const uint8_t *samples,
                                         size_t sample_bytes, po_status_t expected_status) {
  po33_pool_edit_plan_t actual;
  po33_pool_replacement_plan_t expected;
  assert(po33_pool_replacement_plan(fixture->records, fixture->record_count, fixture->pools,
                                    fixture->pool_count, 0u, sample_bytes,
                                    &expected) == expected_status);
  assert(po33_pool_edit_plan(fixture->frame, fixture->length, 0u, samples, sample_bytes, &actual) ==
         expected_status);
  pool_edit_assert_replacement_equal(&actual.replacement, &expected);
}

static void test_api_surface(void) {
  assert(offsetof(po33_pool_edit_plan_t, required_workspace_bytes) == 0u);
  assert(offsetof(po33_pool_edit_plan_t, required_frame_bytes) >
         offsetof(po33_pool_edit_plan_t, required_workspace_bytes));
  assert(offsetof(po33_pool_edit_plan_t, target_sound_record_index) >
         offsetof(po33_pool_edit_plan_t, required_frame_bytes));
  assert(offsetof(po33_pool_edit_plan_t, target_data_span) >
         offsetof(po33_pool_edit_plan_t, target_sound_record_index));
  assert(offsetof(po33_pool_edit_plan_t, replacement) >
         offsetof(po33_pool_edit_plan_t, target_data_span));
  assert(offsetof(po33_pool_edit_plan_t, target_selector) >
         offsetof(po33_pool_edit_plan_t, replacement));
  assert(sizeof((po33_pool_edit_plan_fn_t){po33_pool_edit_plan}) ==
         sizeof(po33_pool_edit_plan_fn_t));
  assert(sizeof((po33_pool_edit_rebuild_fn_t){po33_pool_edit_rebuild}) ==
         sizeof(po33_pool_edit_rebuild_fn_t));
}

static void test_pool_edit_planning(void) {
  pool_edit_fixture_t fixture;
  po33_pool_edit_plan_t plan;
  po33_pool_replacement_plan_t expected;
  uint8_t malformed[PO33_PREAMBLE_BYTES + 32u];
  uint8_t samples[PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u] = {0};
  size_t malformed_length;

  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_ALIAS, PO33_POOL_ALLOCATION_BLOCK_BYTES);
  assert(po33_pool_replacement_plan(fixture.records, fixture.record_count, fixture.pools,
                                    fixture.pool_count, 0u, 3u, &expected) == PO_OK);
  memset(&plan, 0xa5, sizeof(plan));
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples, 3u, &plan) == PO_OK);
  assert(plan.required_workspace_bytes == fixture.length &&
         plan.required_frame_bytes == fixture.length && plan.target_sound_record_index == 0u &&
         plan.target_data_span.offset == fixture.records[0].data_span.offset &&
         plan.target_data_span.length == fixture.records[0].data_span.length &&
         plan.target_selector == 7u);
  pool_edit_assert_replacement_equal(&plan.replacement, &expected);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, NULL, 0u, &plan) == PO_OK);
  assert(plan.replacement.requested_sample_bytes == 0u &&
         plan.replacement.required_allocation_bytes == PO33_POOL_ALLOCATION_BLOCK_BYTES);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples,
                             PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u, &plan) == PO_OK);
  assert(plan.replacement.growth_required &&
         plan.replacement.required_growth_bytes == PO33_POOL_ALLOCATION_BLOCK_BYTES &&
         plan.required_frame_bytes == fixture.length + PO33_POOL_ALLOCATION_BLOCK_BYTES +
                                          17u * PO33_TEST_PACKET_OVERHEAD_BYTES);

  memset(&plan, 0xa5, sizeof(plan));
  assert(po33_pool_edit_plan(fixture.frame, fixture.length + 1u, 0u, samples, 3u, &plan) ==
         PO_ERR_FRAME);
  pool_edit_assert_neutral(&plan);

  memset(&plan, 0xa5, sizeof(plan));
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, fixture.record_count, samples, 3u,
                             &plan) == PO_ERR_RANGE);
  assert(plan.replacement.reason == PO33_POOL_REPLACEMENT_REASON_TARGET_RECORD_OUT_OF_RANGE);

  memset(&plan, 0xa5, sizeof(plan));
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, NULL, 1u, &plan) ==
         PO_ERR_INVALID_ARG);
  pool_edit_assert_neutral(&plan);

  malformed_length = pool_edit_make_malformed_frame(malformed, sizeof(malformed));
  memset(&plan, 0xa5, sizeof(plan));
  assert(po33_pool_edit_plan(malformed, malformed_length, 0u, samples, 3u, &plan) == PO_ERR_FRAME);
  pool_edit_assert_neutral(&plan);

  {
    union {
      uint8_t frame[30000];
      po33_pool_edit_plan_t plan;
    } frame_or_plan;
    uint8_t before[30000];
    memcpy(frame_or_plan.frame, fixture.frame, fixture.length);
    memcpy(before, frame_or_plan.frame, fixture.length);
    assert(po33_pool_edit_plan(frame_or_plan.frame, fixture.length, 0u, samples, 3u,
                               &frame_or_plan.plan) == PO_ERR_INVALID_ARG);
    assert(memcmp(frame_or_plan.frame, before, fixture.length) == 0);
  }
  {
    union {
      uint8_t bytes[sizeof(po33_pool_edit_plan_t) + 8u];
      po33_pool_edit_plan_t plan;
    } samples_or_plan;
    uint8_t before[sizeof(samples_or_plan.bytes)];
    memset(samples_or_plan.bytes, 0xa5, sizeof(samples_or_plan.bytes));
    memcpy(before, samples_or_plan.bytes, sizeof(before));
    assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples_or_plan.bytes, 3u,
                               &samples_or_plan.plan) == PO_ERR_INVALID_ARG);
    assert(memcmp(samples_or_plan.bytes, before, sizeof(before)) == 0);
  }
}

static void test_pool_edit_rejections(void) {
  pool_edit_fixture_t fixture;
  po33_pool_edit_plan_t plan;
  uint8_t sample[8] = {0};

  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_HEADER_ONLY_TARGET, 0u);
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, sample, sizeof(sample), &plan) ==
         PO_ERR_RANGE);
  assert(plan.replacement.reason == PO33_POOL_REPLACEMENT_REASON_TARGET_NOT_OWNED);
  pool_edit_assert_plan_parity(&fixture, sample, sizeof(sample), PO_ERR_RANGE);

  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_UNKNOWN_EXTENSION,
                         PO33_POOL_ALLOCATION_BLOCK_BYTES);
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, sample, sizeof(sample), &plan) ==
         PO_ERR_PARSE);
  assert(plan.replacement.reason == PO33_POOL_REPLACEMENT_REASON_UNKNOWN_TARGET_EXTENSION);
  pool_edit_assert_plan_parity(&fixture, sample, sizeof(sample), PO_ERR_PARSE);

  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_DUPLICATE_TARGET,
                         PO33_POOL_ALLOCATION_BLOCK_BYTES);
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, sample, sizeof(sample), &plan) ==
         PO_ERR_PARSE);
  assert(plan.replacement.reason == PO33_POOL_REPLACEMENT_REASON_DUPLICATE_TARGET_SELECTOR);
  pool_edit_assert_plan_parity(&fixture, sample, sizeof(sample), PO_ERR_PARSE);

  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_AMBIGUOUS_TARGET_SOURCE,
                         PO33_POOL_ALLOCATION_BLOCK_BYTES);
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, sample, sizeof(sample), &plan) ==
         PO_ERR_PARSE);
  assert(plan.replacement.reason == PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR);
  pool_edit_assert_plan_parity(&fixture, sample, sizeof(sample), PO_ERR_PARSE);

  pool_edit_make_fixture(&fixture, 0u, 7u);
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, sample, sizeof(sample), &plan) ==
         PO_ERR_RANGE);
  assert(plan.replacement.reason == PO33_POOL_REPLACEMENT_REASON_EXISTING_ALLOCATION_UNALIGNED);
  pool_edit_assert_plan_parity(&fixture, sample, sizeof(sample), PO_ERR_RANGE);
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, sample, 7u, &plan) == PO_OK);
}

static int pool_edit_collect_packet(const po_packet_t *packet, void *user) {
  pool_edit_packet_list_t *list = (pool_edit_packet_list_t *)user;
  assert(list->count < sizeof(list->items) / sizeof(list->items[0]));
  list->items[list->count++] = *packet;
  return 0;
}

static void pool_edit_collect_packets(const uint8_t *frame, size_t frame_bytes,
                                      pool_edit_packet_list_t *list) {
  po33_decode_result_t result;
  po_status_t status;
  list->count = 0u;
  status = po33_frame_parse(frame, frame_bytes, pool_edit_collect_packet, list, &result);
  assert(status == PO_OK);
  assert(result.done && !result.stopped && result.frame_bytes == frame_bytes);
}

static int pool_edit_packets_equal(const po_packet_t *left, const po_packet_t *right) {
  if (left->tag_code != right->tag_code || left->payload_len != right->payload_len)
    return 0;
  for (size_t index = 0u; index < left->payload_len; ++index)
    if (left->payload[index] != right->payload[index])
      return 0;
  return 1;
}

static size_t pool_edit_find_sound_start(const pool_edit_packet_list_t *list, uint8_t selector) {
  for (size_t index = 0u; index < list->count; ++index)
    if (list->items[index].tag_code == PO33_TAG_SOUND_SELECT &&
        list->items[index].payload_len == 1u && list->items[index].payload[0] == selector)
      return index;
  return SIZE_MAX;
}

static size_t pool_edit_find_sound_end(const pool_edit_packet_list_t *list, size_t start) {
  for (size_t index = start + 1u; index < list->count; ++index)
    if (list->items[index].tag_code == PO33_TAG_SOUND_SELECT ||
        list->items[index].tag_code == PO33_TAG_END)
      return index;
  return SIZE_MAX;
}

static size_t pool_edit_collect_sound_data(const pool_edit_packet_list_t *list, size_t start,
                                           size_t end, uint8_t *out, size_t capacity) {
  size_t count = 0u;
  for (size_t index = start + 1u; index < end; ++index) {
    const po_packet_t *packet = list->items + index;
    if (packet->tag_code != PO33_TAG_SOUND_DATA)
      continue;
    assert(packet->payload_len <= capacity - count);
    for (size_t byte = 0u; byte < packet->payload_len; ++byte)
      out[count + byte] = packet->payload[byte];
    count += packet->payload_len;
  }
  return count;
}

static void pool_edit_assert_preserved_outside_target(const pool_edit_packet_list_t *before,
                                                      const pool_edit_packet_list_t *after,
                                                      size_t target_start, size_t target_end,
                                                      size_t inserted_packets) {
  assert(after->count == before->count + inserted_packets);
  for (size_t index = 0u; index <= target_start; ++index)
    assert(pool_edit_packets_equal(before->items + index, after->items + index));
  for (size_t index = target_end; index < before->count; ++index)
    assert(pool_edit_packets_equal(before->items + index, after->items + index + inserted_packets));
  for (size_t index = target_start + 1u; index < target_end; ++index)
    assert(before->items[index].tag_code == after->items[index].tag_code &&
           before->items[index].payload_len == after->items[index].payload_len);
}

static void pool_edit_assert_target_body(const uint8_t *original, const uint8_t *edited,
                                         size_t allocation, const uint8_t *samples,
                                         size_t sample_bytes) {
  assert(!memcmp(original, edited, PO33_SOUND_HEADER_BYTES));
  for (size_t index = 0u; index < allocation; ++index) {
    uint8_t expected = 0u;
    if (index < sample_bytes) {
      assert(samples != NULL);
      expected = samples[index];
    }
    assert(edited[PO33_SOUND_HEADER_BYTES + index] == expected);
  }
}

static void test_pool_edit_rebuild(void) {
  pool_edit_fixture_t fixture;
  po33_pool_edit_plan_t plan;
  pool_edit_packet_list_t before, after;
  uint8_t samples[2u * PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u];
  uint8_t malformed[PO33_PREAMBLE_BYTES + 32u];
  uint8_t original_data[3u * PO33_POOL_ALLOCATION_BLOCK_BYTES + PO33_SOUND_HEADER_BYTES];
  uint8_t edited_data[3u * PO33_POOL_ALLOCATION_BLOCK_BYTES + PO33_SOUND_HEADER_BYTES];
  uint8_t workspace[30000], output[30000], output_before[30000];
  size_t output_bytes = SIZE_MAX;
  size_t malformed_length;
  size_t target_start, target_end;

  for (size_t index = 0u; index < sizeof(samples); ++index)
    samples[index] = (uint8_t)(index * 17u + 3u);
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_ALIAS, PO33_POOL_ALLOCATION_BLOCK_BYTES);
  pool_edit_collect_packets(fixture.frame, fixture.length, &before);
  target_start = pool_edit_find_sound_start(&before, 7u);
  target_end = pool_edit_find_sound_end(&before, target_start);
  assert(target_start != SIZE_MAX && target_end != SIZE_MAX);
  assert(pool_edit_collect_sound_data(&before, target_start, target_end, original_data,
                                      sizeof(original_data)) ==
         PO33_SOUND_HEADER_BYTES + PO33_POOL_ALLOCATION_BLOCK_BYTES);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples, 3u, &plan) == PO_OK);
  memset(output, 0xa5, sizeof(output));
  memcpy(output_before, output, sizeof(output));
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_OK);
  assert(output_bytes == fixture.length);
  pool_edit_collect_packets(output, output_bytes, &after);
  pool_edit_assert_preserved_outside_target(&before, &after, target_start, target_end, 0u);
  assert(pool_edit_collect_sound_data(&after, target_start, target_end, edited_data,
                                      sizeof(edited_data)) ==
         PO33_SOUND_HEADER_BYTES + PO33_POOL_ALLOCATION_BLOCK_BYTES);
  pool_edit_assert_target_body(original_data, edited_data, PO33_POOL_ALLOCATION_BLOCK_BYTES,
                               samples, 3u);
  assert(memcmp(output, output_before, output_bytes) != 0);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples,
                             PO33_POOL_ALLOCATION_BLOCK_BYTES, &plan) == PO_OK);
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples,
                                PO33_POOL_ALLOCATION_BLOCK_BYTES, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_OK);
  pool_edit_collect_packets(output, output_bytes, &after);
  assert(pool_edit_collect_sound_data(&after, target_start, target_end, edited_data,
                                      sizeof(edited_data)) ==
         PO33_SOUND_HEADER_BYTES + PO33_POOL_ALLOCATION_BLOCK_BYTES);
  pool_edit_assert_target_body(original_data, edited_data, PO33_POOL_ALLOCATION_BLOCK_BYTES,
                               samples, PO33_POOL_ALLOCATION_BLOCK_BYTES);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, NULL, 0u, &plan) == PO_OK);
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, NULL, 0u, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_OK);
  pool_edit_collect_packets(output, output_bytes, &after);
  assert(pool_edit_collect_sound_data(&after, target_start, target_end, edited_data,
                                      sizeof(edited_data)) ==
         PO33_SOUND_HEADER_BYTES + PO33_POOL_ALLOCATION_BLOCK_BYTES);
  pool_edit_assert_target_body(original_data, edited_data, PO33_POOL_ALLOCATION_BLOCK_BYTES, NULL,
                               0u);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples,
                             PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u, &plan) == PO_OK);
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples,
                                PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_OK);
  assert(output_bytes ==
         fixture.length + PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO33_TEST_PACKET_OVERHEAD_BYTES);
  pool_edit_collect_packets(output, output_bytes, &after);
  pool_edit_assert_preserved_outside_target(&before, &after, target_start, target_end, 17u);
  for (size_t index = 0u; index < 17u; ++index) {
    const po_packet_t *packet = after.items + target_end + index;
    assert(packet->tag_code == PO33_TAG_SOUND_DATA &&
           packet->payload_len == (index < 16u ? 255u : 16u));
  }
  assert(pool_edit_collect_sound_data(&after, target_start, target_end + 17u, edited_data,
                                      sizeof(edited_data)) ==
         PO33_SOUND_HEADER_BYTES + 2u * PO33_POOL_ALLOCATION_BLOCK_BYTES);
  pool_edit_assert_target_body(original_data, edited_data, 2u * PO33_POOL_ALLOCATION_BLOCK_BYTES,
                               samples, PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples,
                             2u * PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u, &plan) == PO_OK);
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples,
                                2u * PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_OK);
  assert(output_bytes == fixture.length + 2u * (PO33_POOL_ALLOCATION_BLOCK_BYTES +
                                                17u * PO33_TEST_PACKET_OVERHEAD_BYTES));
  pool_edit_collect_packets(output, output_bytes, &after);
  pool_edit_assert_preserved_outside_target(&before, &after, target_start, target_end, 34u);
  for (size_t index = 0u; index < 34u; ++index) {
    const po_packet_t *packet = after.items + target_end + index;
    assert(packet->tag_code == PO33_TAG_SOUND_DATA &&
           packet->payload_len == (index % 17u < 16u ? 255u : 16u));
  }
  assert(pool_edit_collect_sound_data(&after, target_start, target_end + 34u, edited_data,
                                      sizeof(edited_data)) ==
         PO33_SOUND_HEADER_BYTES + 3u * PO33_POOL_ALLOCATION_BLOCK_BYTES);
  pool_edit_assert_target_body(original_data, edited_data, 3u * PO33_POOL_ALLOCATION_BLOCK_BYTES,
                               samples, 2u * PO33_POOL_ALLOCATION_BLOCK_BYTES + 1u);

  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples, 3u, &plan) == PO_OK);
  memset(output, 0xa5, sizeof(output));
  memcpy(output_before, output, sizeof(output));
  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 4u, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_ERR_INVALID_ARG);
  assert(output_bytes == 0u && memcmp(output, output_before, sizeof(output)) == 0);

  malformed_length = pool_edit_make_malformed_frame(malformed, sizeof(malformed));
  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(malformed, malformed_length, 0u, samples, 3u, &plan, workspace,
                                plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_ERR_INVALID_ARG);
  assert(output_bytes == 0u && memcmp(output, output_before, sizeof(output)) == 0);

  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                plan.required_workspace_bytes - 1u, output, sizeof(output),
                                &output_bytes) == PO_ERR_BUFFER_TOO_SMALL);
  assert(output_bytes == 0u && memcmp(output, output_before, sizeof(output)) == 0);

  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                plan.required_workspace_bytes, output,
                                plan.required_frame_bytes - 1u,
                                &output_bytes) == PO_ERR_BUFFER_TOO_SMALL);
  assert(output_bytes == 0u && memcmp(output, output_before, sizeof(output)) == 0);

  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length + 1u, 0u, samples, 3u, &plan,
                                workspace, plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_ERR_INVALID_ARG);
  assert(output_bytes == 0u && memcmp(output, output_before, sizeof(output)) == 0);

  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, fixture.frame, 3u, &plan,
                                workspace, plan.required_workspace_bytes, output, sizeof(output),
                                &output_bytes) == PO_OK);

  output_bytes = SIZE_MAX;
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan,
                                (uint8_t *)&plan, plan.required_workspace_bytes, output,
                                sizeof(output), &output_bytes) == PO_ERR_INVALID_ARG);
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                plan.required_workspace_bytes, workspace, sizeof(workspace),
                                &output_bytes) == PO_ERR_INVALID_ARG);
  assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                plan.required_workspace_bytes, (uint8_t *)&plan, sizeof(plan),
                                &output_bytes) == PO_ERR_INVALID_ARG);
  {
    uint8_t frame_before[sizeof(fixture.frame)];
    memcpy(frame_before, fixture.frame, sizeof(frame_before));
    assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan,
                                  fixture.frame, sizeof(fixture.frame), output, sizeof(output),
                                  &output_bytes) == PO_ERR_INVALID_ARG);
    assert(memcmp(frame_before, fixture.frame, sizeof(frame_before)) == 0);
    assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                  plan.required_workspace_bytes, fixture.frame,
                                  sizeof(fixture.frame), &output_bytes) == PO_ERR_INVALID_ARG);
    assert(memcmp(frame_before, fixture.frame, sizeof(frame_before)) == 0);
  }
  {
    uint8_t samples_before[sizeof(samples)];
    memcpy(samples_before, samples, sizeof(samples_before));
    assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                  plan.required_workspace_bytes, samples, sizeof(samples),
                                  &output_bytes) == PO_ERR_INVALID_ARG);
    assert(memcmp(samples_before, samples, sizeof(samples_before)) == 0);
  }
  {
    union {
      uint8_t bytes[30000];
      size_t length;
    } output_or_length;
    assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                  plan.required_workspace_bytes, output_or_length.bytes,
                                  sizeof(output_or_length.bytes),
                                  &output_or_length.length) == PO_ERR_INVALID_ARG);
  }
  {
    union {
      uint8_t bytes[30000];
      size_t length;
    } frame_or_length;
    uint8_t frame_before[sizeof(frame_or_length.bytes)];
    memcpy(frame_or_length.bytes, fixture.frame, sizeof(frame_or_length.bytes));
    memcpy(frame_before, frame_or_length.bytes, sizeof(frame_before));
    assert(po33_pool_edit_rebuild(frame_or_length.bytes, fixture.length, 0u, samples, 3u, &plan,
                                  workspace, plan.required_workspace_bytes, output, sizeof(output),
                                  &frame_or_length.length) == PO_ERR_INVALID_ARG);
    assert(memcmp(frame_before, frame_or_length.bytes, sizeof(frame_before)) == 0);
  }
}

static void range_assert_preservation(const pool_edit_fixture_t *fixture, const uint8_t *output,
                                      size_t output_bytes, const po33_sound_range_plan_t *plan) {
  pool_edit_packet_list_t before, after;
  size_t header_index = 0u;
  pool_edit_collect_packets(fixture->frame, fixture->length, &before);
  pool_edit_collect_packets(output, output_bytes, &after);
  assert(before.count == after.count && output_bytes == fixture->length);
  for (size_t index = 0u; index < before.count; ++index) {
    const po_packet_t *old = before.items + index, *now = after.items + index;
    int target = old->tag_code == PO33_TAG_SOUND_DATA &&
                 old->offset >= plan->target_data_span.offset &&
                 old->offset - plan->target_data_span.offset < plan->target_data_span.length;
    assert(old->tag_code == now->tag_code && old->payload_len == now->payload_len);
    for (size_t byte = 0u; byte < old->payload_len; ++byte) {
      uint8_t expected = old->payload[byte];
      if (target && header_index < 8u) {
        uint32_t value = header_index < 4u ? plan->range_start_raw : plan->range_end_raw;
        expected = (uint8_t)(value >> (8u * (header_index++ % 4u)));
      }
      assert(now->payload[byte] == expected);
    }
  }
  assert(header_index == 8u);
}

static void test_sound_ranges(void) {
  pool_edit_fixture_t fixture;
  po33_sound_range_plan_t plan, stale;
  uint8_t output[30000], workspace[30000], before[30000];
  size_t written;
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_ALIAS, 4096u);
  for (size_t target = 0u; target < 2u; ++target) {
    assert(po33_sound_range_plan(fixture.frame, fixture.length, target, 17u, 3000u, &plan) ==
           PO_OK);
    assert(plan.required_frame_bytes == fixture.length &&
           plan.required_workspace_bytes == fixture.length && plan.referenced_pool_index == 0u &&
           plan.referenced_allocation_bytes == 4096u && plan.source_selector == 7u &&
           plan.reason == PO33_SOUND_RANGE_REASON_NONE);
    assert(po33_sound_range_rebuild(fixture.frame, fixture.length, target, 17u, 3000u, &plan,
                                    workspace, sizeof(workspace), output, sizeof(output),
                                    &written) == PO_OK);
    range_assert_preservation(&fixture, output, written, &plan);
  }
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 1u, 0u, 4095u, &plan) == PO_OK);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 1u, 0u, 4096u, &plan) ==
         PO_ERR_RANGE);
  assert(plan.reason == PO33_SOUND_RANGE_REASON_INVALID_RANGE && plan.required_frame_bytes == 0u &&
         plan.target_sound_record_index == SIZE_MAX);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 12u, 12u, &plan) == PO_ERR_RANGE);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 13u, 12u, &plan) == PO_ERR_RANGE);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 0u, UINT32_MAX, &plan) ==
         PO_ERR_RANGE);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 9u, 0u, 1u, &plan) == PO_ERR_RANGE);
  assert(plan.reason == PO33_SOUND_RANGE_REASON_TARGET_OUT_OF_RANGE);
  assert(po33_sound_range_plan(fixture.frame, fixture.length + 1u, 0u, 0u, 1u, &plan) ==
         PO_ERR_FRAME);
  assert(plan.required_frame_bytes == 0u && plan.reason == PO33_SOUND_RANGE_REASON_NONE);
  assert(po33_sound_range_plan(NULL, 0u, 0u, 0u, 1u, &plan) == PO_ERR_INVALID_ARG);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 0u, 1u, NULL) ==
         PO_ERR_INVALID_ARG);

  assert(po33_sound_range_plan(fixture.frame, fixture.length, 1u, 0u, 4095u, &plan) == PO_OK);
  memset(output, 0xa5, sizeof(output));
  memcpy(before, output, sizeof(before));
  written = SIZE_MAX;
  assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 1u, 0u, 4095u, &plan, workspace,
                                  fixture.length - 1u, output, sizeof(output),
                                  &written) == PO_ERR_BUFFER_TOO_SMALL);
  assert(written == 0u && memcmp(output, before, sizeof(output)) == 0);
  written = SIZE_MAX;
  assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 1u, 0u, 4095u, &plan, workspace,
                                  sizeof(workspace), output, fixture.length - 1u,
                                  &written) == PO_ERR_BUFFER_TOO_SMALL);
  assert(written == 0u && memcmp(output, before, sizeof(output)) == 0);
  stale = plan;
  ++stale.referenced_allocation_bytes;
  written = SIZE_MAX;
  assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 1u, 0u, 4095u, &stale, workspace,
                                  sizeof(workspace), output, sizeof(output),
                                  &written) == PO_ERR_INVALID_ARG);
  assert(written == 0u && memcmp(output, before, sizeof(output)) == 0);
  assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 1u, 0u, 4095u, &plan, output,
                                  sizeof(output), output, sizeof(output),
                                  &written) == PO_ERR_INVALID_ARG);
  {
    union {
      uint8_t bytes[30000];
      po33_sound_range_plan_t plan;
      size_t length;
    } alias;
    memcpy(alias.bytes, fixture.frame, fixture.length);
    assert(po33_sound_range_plan(alias.bytes, fixture.length, 0u, 0u, 1u, &alias.plan) ==
           PO_ERR_INVALID_ARG);
    assert(memcmp(alias.bytes, fixture.frame, fixture.length) == 0);
    assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 1u, 0u, 4095u, &plan, workspace,
                                    sizeof(workspace), alias.bytes, sizeof(alias.bytes),
                                    &alias.length) == PO_ERR_INVALID_ARG);
  }
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_DUPLICATE_TARGET, 4096u);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 0u, 1u, &plan) == PO_ERR_PARSE);
  assert(plan.reason == PO33_SOUND_RANGE_REASON_DUPLICATE_TARGET);
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_UNKNOWN_EXTENSION, 4096u);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 0u, 1u, &plan) == PO_ERR_PARSE);
  assert(plan.reason == PO33_SOUND_RANGE_REASON_UNKNOWN_TARGET_EXTENSION);
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_AMBIGUOUS_TARGET_SOURCE, 4096u);
  assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 0u, 1u, &plan) == PO_ERR_PARSE);
  assert(plan.reason == PO33_SOUND_RANGE_REASON_SOURCE_AMBIGUOUS);
  {
    po33_builder_t builder;
    uint8_t selector = 7u, header[9] = {0}, samples[255] = {0};
    size_t remaining = 4096u;
    pool_edit_write_u32le(header, 100u);
    pool_edit_write_u32le(header + 4u, 200u);
    header[8] = selector;
    assert(po33_builder_init(&builder, fixture.frame, sizeof(fixture.frame)) == PO_OK);
    pool_edit_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u);
    pool_edit_append(&builder, PO33_TAG_SOUND_DATA, header, 4u);
    pool_edit_append(&builder, PO33_TAG_SOUND_DATA, header + 4u, 5u);
    while (remaining != 0u) {
      size_t bytes = remaining < sizeof(samples) ? remaining : sizeof(samples);
      pool_edit_append(&builder, PO33_TAG_SOUND_DATA, samples, bytes);
      remaining -= bytes;
    }
    pool_edit_append_sound(&builder, 8u, 7u, 0u, 1, 0);
    assert(po33_builder_finish(&builder, &fixture.length) == PO_OK);
    /* Alias coordinates may extend beyond the owner's trim, within its pool. */
    assert(po33_sound_range_plan(fixture.frame, fixture.length, 1u, 500u, 3000u, &plan) == PO_OK);
    assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 1u, 500u, 3000u, &plan,
                                    workspace, sizeof(workspace), output, sizeof(output),
                                    &written) == PO_OK);
    range_assert_preservation(&fixture, output, written, &plan);
  }
  {
    po33_builder_t builder;
    assert(po33_builder_init(&builder, fixture.frame, sizeof(fixture.frame)) == PO_OK);
    pool_edit_append_sound(&builder, 7u, 99u, 4096u, 1, 0);
    assert(po33_builder_finish(&builder, &fixture.length) == PO_OK);
    assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 0u, 1u, &plan) == PO_ERR_PARSE);
    assert(plan.reason == PO33_SOUND_RANGE_REASON_SOURCE_UNRESOLVED);
  }
}

static void test_full_sample_edit(void) {
  static const size_t lengths[] = {1u, 1000u, 4095u, 4096u, 4097u, 8192u};
  pool_edit_fixture_t fixture;
  po33_sample_edit_plan_t plan, bad;
  po33_pool_edit_plan_t legacy;
  pool_edit_packet_list_t before, after;
  uint8_t samples[8192], workspace[30000], output[30000], body[13000];
  size_t written, start, end;
  for (size_t i = 0; i < sizeof(samples); ++i)
    samples[i] = (uint8_t)(1u + i % 254u);
  pool_edit_make_fixture(&fixture, 0u, 4096u);
  {
    po33_sound_range_plan_t trim;
    assert(po33_sound_range_plan(fixture.frame, fixture.length, 0u, 100u, 200u, &trim) == PO_OK);
    assert(po33_sound_range_rebuild(fixture.frame, fixture.length, 0u, 100u, 200u, &trim, workspace,
                                    sizeof(workspace), output, sizeof(output), &written) == PO_OK);
    memcpy(fixture.frame, output, written);
    fixture.length = written;
  }
  pool_edit_collect_packets(fixture.frame, fixture.length, &before);
  start = pool_edit_find_sound_start(&before, 7u);
  end = pool_edit_find_sound_end(&before, start);
  for (size_t test = 0; test < sizeof(lengths) / sizeof(lengths[0]); ++test) {
    size_t n = lengths[test], allocation = ((n + 4096u) / 4096u) * 4096u;
    po33_sound_header_t header;
    assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, samples, n, &plan) == PO_OK);
    assert(plan.pool.replacement.requested_sample_bytes == n);
    assert(plan.pool.replacement.required_allocation_bytes == allocation);
    assert(plan.pool.replacement.reference_count == 1u);
    assert(po33_sample_edit_rebuild(fixture.frame, fixture.length, 0u, samples, n, &plan, workspace,
                                    sizeof(workspace), output, sizeof(output), &written) == PO_OK);
    pool_edit_collect_packets(output, written, &after);
    pool_edit_assert_preserved_outside_target(&before, &after, start, end,
                                              (allocation - 4096u) / 4096u * 17u);
    assert(pool_edit_collect_sound_data(&after, start, pool_edit_find_sound_end(&after, start),
                                        body, sizeof(body)) == allocation + 9u);
    assert(po33_sound_header_decode(body, 9u, &header) == PO_OK);
    assert(header.range_start_raw == 0u && header.range_end_raw == n &&
           header.source_selector == 7u);
    assert(memcmp(body + 9u, samples, n) == 0);
    for (size_t i = n; i < allocation; ++i)
      assert(body[9u + i] == 0u);
    memset(output, 0xa5, sizeof(output));
    written = 99u;
    assert(po33_sample_edit_rebuild(fixture.frame, fixture.length, 0u, samples, n, &plan, workspace,
                                    plan.pool.required_workspace_bytes - 1u, output, sizeof(output),
                                    &written) == PO_ERR_BUFFER_TOO_SMALL);
    assert(written == 0u);
    for (size_t i = 0; i < sizeof(output); ++i)
      assert(output[i] == 0xa5);
    bad = plan;
    bad.policy = 0u;
    assert(po33_sample_edit_rebuild(fixture.frame, fixture.length, 0u, samples, n, &bad, workspace,
                                    sizeof(workspace), output, sizeof(output),
                                    &written) == PO_ERR_INVALID_ARG);
    assert(written == 0u && output[0] == 0xa5);
    bad = plan;
    ++bad.pool.replacement.requested_sample_bytes;
    assert(po33_sample_edit_rebuild(fixture.frame, fixture.length, 0u, samples, n, &bad, workspace,
                                    sizeof(workspace), output, sizeof(output),
                                    &written) == PO_ERR_INVALID_ARG);
    assert(written == 0u && output[0] == 0xa5);
  }
  assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, NULL, 0u, &plan) == PO_ERR_RANGE);
  pool_edit_assert_neutral(&plan.pool);
  assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, NULL, 1u, &plan) ==
         PO_ERR_INVALID_ARG);
  assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, samples, 1u, NULL) ==
         PO_ERR_INVALID_ARG);
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_ALIAS, 4096u);
  assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, samples, 100u, &plan) ==
         PO_ERR_PARSE);
  assert(plan.pool.replacement.reason == PO33_POOL_REPLACEMENT_REASON_NOT_EXCLUSIVE);
  assert(plan.pool.required_frame_bytes == 0u);
  /* The explicit shared-pool operation keeps its existing contract. */
  assert(po33_pool_edit_plan(fixture.frame, fixture.length, 0u, samples, 100u, &legacy) == PO_OK);
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_UNKNOWN_EXTENSION, 4096u);
  assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, samples, 100u, &plan) ==
         PO_ERR_PARSE);
  assert(plan.pool.replacement.reason == PO33_POOL_REPLACEMENT_REASON_UNKNOWN_TARGET_EXTENSION);
  pool_edit_make_fixture(&fixture, POOL_EDIT_FIXTURE_HEADER_ONLY_TARGET, 4096u);
  assert(po33_sample_edit_plan(fixture.frame, fixture.length, 0u, samples, 100u, &plan) ==
         PO_ERR_RANGE);
  assert(plan.pool.replacement.reason == PO33_POOL_REPLACEMENT_REASON_TARGET_NOT_OWNED);
}

static void insert_fixture(pool_edit_fixture_t *f, unsigned flags) {
  po33_builder_t b;
  uint8_t slot = 1u, raw[3] = {1, 2, 3}, header[9] = {0};
  assert(po33_builder_init(&b, f->frame, sizeof(f->frame)) == PO_OK);
  pool_edit_append(&b, PO33_TAG_SETTINGS, raw, sizeof(raw));
  if (!(flags & 16u)) {
    pool_edit_append(&b, PO33_TAG_SOUND_SELECT, &slot, 1u);
    if (flags & 1u) {
      header[8] = slot;
      pool_edit_append(&b, PO33_TAG_SOUND_DATA, header, sizeof(header));
    }
    if (flags & 2u)
      pool_edit_append(&b, PO33_TAG_SOUND_SELECT, &slot, 1u);
    if (flags & 8u)
      pool_edit_append(&b, 0x7777u, raw, sizeof(raw));
  }
  pool_edit_append_sound(&b, 7u, flags & 4u ? 1u : 7u, 4096u, 1, 0);
  assert(po33_builder_finish(&b, &f->length) == PO_OK);
}

static void test_sample_insertion(void) {
  const size_t sizes[] = {1, 246, 4095, 4096, 4097, 8192, 14063, 16384};
  pool_edit_fixture_t f;
  po33_sample_insert_plan_t plan, bad;
  pool_edit_packet_list_t before, after;
  uint8_t samples[16384], workspace[40000], out[40000], body[25000];
  size_t written, start, end;
  for (size_t i = 0; i < sizeof(samples); ++i)
    samples[i] = (uint8_t)(1 + i % 254);
  insert_fixture(&f, 0);
  pool_edit_collect_packets(f.frame, f.length, &before);
  start = pool_edit_find_sound_start(&before, 1u);
  end = pool_edit_find_sound_end(&before, start);
  for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
    size_t n = sizes[k], blocks = n / 4096u + 1u;
    po33_sound_header_t header;
    assert(po33_sample_insert_plan(f.frame, f.length, 1u, samples, n, &plan) == PO_OK);
    assert(plan.allocation_bytes == blocks * 4096u && plan.sample_bytes == n);
    assert(plan.required_frame_bytes == f.length + 9u + blocks * (4096u + 85u));
    assert(po33_sample_insert_rebuild(f.frame, f.length, 1u, samples, n, &plan, workspace,
                                      sizeof(workspace), out, sizeof(out), &written) == PO_OK);
    pool_edit_collect_packets(out, written, &after);
    pool_edit_assert_preserved_outside_target(&before, &after, start, end, blocks * 17u);
    assert(pool_edit_collect_sound_data(&after, start, pool_edit_find_sound_end(&after, start),
                                        body, sizeof(body)) == 9u + blocks * 4096u);
    assert(po33_sound_header_decode(body, 9u, &header) == PO_OK);
    assert(header.range_start_raw == 0 && header.range_end_raw == n && header.source_selector == 1);
    assert(!memcmp(body + 9, samples, n));
    for (size_t i = n; i < blocks * 4096u; ++i)
      assert(body[9 + i] == 0);
    /* Regression from real readback: terminate each block, including header
     * in the first block. Flat 255-byte rechunking corrupted hardware samples. */
    for (size_t i = 0; i < blocks * 17u; ++i)
      assert(after.items[start + 1 + i].payload_len ==
             (i % 17u == 16u ? (i == 16u ? 25u : 16u) : 255u));
    memset(out, 0xa5, sizeof(out));
    written = 7;
    assert(po33_sample_insert_rebuild(f.frame, f.length, 1u, samples, n, &plan, workspace,
                                      sizeof(workspace), out, plan.required_frame_bytes - 1u,
                                      &written) == PO_ERR_BUFFER_TOO_SMALL);
    assert(written == 0);
    for (size_t i = 0; i < sizeof(out); ++i)
      assert(out[i] == 0xa5);
    bad = plan;
    bad.selector_offset++;
    assert(po33_sample_insert_rebuild(f.frame, f.length, 1u, samples, n, &bad, workspace,
                                      sizeof(workspace), out, sizeof(out),
                                      &written) == PO_ERR_INVALID_ARG);
    assert(written == 0 && out[0] == 0xa5);
    assert(po33_sample_insert_rebuild(f.frame, f.length, 1u, samples, n, &plan, workspace,
                                      sizeof(workspace), workspace, sizeof(workspace),
                                      &written) == PO_ERR_INVALID_ARG);
    bad = plan;
    assert(po33_sample_insert_rebuild(f.frame, f.length, 1u, samples, n, &plan, workspace,
                                      sizeof(workspace), out, sizeof(out),
                                      &plan.required_frame_bytes) == PO_ERR_INVALID_ARG);
    assert(plan.required_frame_bytes == bad.required_frame_bytes);
  }
  for (unsigned i = 0; i < 5; ++i) {
    const po33_sample_insert_reason_t reasons[] = {
        PO33_SAMPLE_INSERT_REASON_OCCUPIED, PO33_SAMPLE_INSERT_REASON_DUPLICATE_SELECTOR,
        PO33_SAMPLE_INSERT_REASON_REFERENCED, PO33_SAMPLE_INSERT_REASON_UNKNOWN_EXTENSION,
        PO33_SAMPLE_INSERT_REASON_MISSING_SELECTOR};
    insert_fixture(&f, 1u << i);
    assert(po33_sample_insert_plan(f.frame, f.length, 1u, samples, 100u, &plan) ==
           (i == 4 ? PO_ERR_RANGE : PO_ERR_PARSE));
    assert(plan.reason == reasons[i] && plan.required_frame_bytes == 0 && plan.slot == UINT8_MAX);
  }
  insert_fixture(&f, 0);
  assert(po33_sample_insert_plan(f.frame, f.length, 8u, samples, 1u, &plan) == PO_ERR_RANGE);
  assert(plan.reason == PO33_SAMPLE_INSERT_REASON_SLOT_OUT_OF_RANGE);
  assert(po33_sample_insert_plan(f.frame, f.length, 1u, samples, 0u, &plan) == PO_ERR_RANGE);
  assert(po33_sample_insert_plan(f.frame, f.length, 1u, NULL, 1u, &plan) == PO_ERR_INVALID_ARG);
  assert(po33_sample_insert_plan(NULL, f.length, 1u, samples, 1u, &plan) == PO_ERR_INVALID_ARG);
  assert(po33_sample_insert_plan(f.frame, f.length, 1u, samples, 1u, NULL) == PO_ERR_INVALID_ARG);
  assert(po33_sample_insert_plan(f.frame, f.length - 1u, 1u, samples, 1u, &plan) == PO_ERR_FRAME);
  f.frame[f.length] = 0;
  assert(po33_sample_insert_plan(f.frame, f.length + 1u, 1u, samples, 1u, &plan) == PO_ERR_FRAME);
  {
    union {
      po33_sample_insert_plan_t plan;
      uint8_t data[40000];
    } aliased;
    memcpy(aliased.data, f.frame, f.length);
    assert(po33_sample_insert_plan(aliased.data, f.length, 1u, samples, 1u, &aliased.plan) ==
           PO_ERR_INVALID_ARG);
    assert(!memcmp(aliased.data, f.frame, f.length));
  }
}

static void test_insert_empty_bank_positions(void) {
  uint8_t frame[5000], workspace[10000], output[10000], sample = 37;
  po33_builder_t builder;
  po33_sample_insert_plan_t plan;
  pool_edit_packet_list_t before, after;
  size_t length, written;
  assert(po33_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  for (uint8_t slot = 0; slot < 8; ++slot)
    pool_edit_append(&builder, PO33_TAG_SOUND_SELECT, &slot, 1);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  pool_edit_collect_packets(frame, length, &before);
  for (uint8_t slot = 0; slot < 8; ++slot) {
    assert(po33_sample_insert_plan(frame, length, slot, &sample, 1, &plan) == PO_OK);
    assert(po33_sample_insert_rebuild(frame, length, slot, &sample, 1, &plan, workspace,
                                      sizeof(workspace), output, sizeof(output),
                                      &written) == PO_OK);
    pool_edit_collect_packets(output, written, &after);
    size_t start = pool_edit_find_sound_start(&before, slot);
    pool_edit_assert_preserved_outside_target(&before, &after, start,
                                              pool_edit_find_sound_end(&before, start), 17);
    memset(output, 0xa5, sizeof(output));
    assert(po33_sample_insert_rebuild(frame, length, slot, &sample, 1, &plan, workspace,
                                      plan.required_workspace_bytes - 1, output, sizeof(output),
                                      &written) == PO_ERR_BUFFER_TOO_SMALL);
    assert(written == 0 && output[0] == 0xa5);
    assert(po33_sample_insert_rebuild(frame, length, slot, &sample, 1, NULL, workspace,
                                      sizeof(workspace), output, sizeof(output),
                                      &written) == PO_ERR_INVALID_ARG);
    assert(written == 0 && output[0] == 0xa5);
  }
}

int main(void) {
  test_api_surface();
  test_pool_edit_planning();
  test_pool_edit_rejections();
  test_pool_edit_rebuild();
  test_sound_ranges();
  test_full_sample_edit();
  test_sample_insertion();
  test_insert_empty_bank_positions();
  puts("PO-33 pool edit plan tests passed");
  return 0;
}
