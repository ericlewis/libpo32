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
    union {
      uint8_t bytes[30000];
      size_t length;
    } output_or_length;
    assert(po33_pool_edit_rebuild(fixture.frame, fixture.length, 0u, samples, 3u, &plan, workspace,
                                  plan.required_workspace_bytes, output_or_length.bytes,
                                  sizeof(output_or_length.bytes),
                                  &output_or_length.length) == PO_ERR_INVALID_ARG);
  }
}

int main(void) {
  test_api_surface();
  test_pool_edit_planning();
  test_pool_edit_rejections();
  test_pool_edit_rebuild();
  puts("PO-33 pool edit plan tests passed");
  return 0;
}
