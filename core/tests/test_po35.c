#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po35.h"
#include "po32.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const uint8_t unknown_and_tail[] = {0x43, 0x5d, 0xe6, 0xfc, 0x4e, 0xd1, 0xd3,
                                           0x54, 0x98, 0xeb, 0x00, 0x6e, 0xba, 0xdb};
static const uint8_t empty_tail[] = {0xd2, 0x24, 0x71, 0x86, 0xd4};
static const uint8_t payload[] = {0, 127, 128, 255};
static uint8_t frame[PO35_PREAMBLE_BYTES + 1024];

typedef struct collector {
  size_t count;
  int stop;
  po_packet_t packet;
} collector_t;

static int collect(const po_packet_t *packet, void *user) {
  collector_t *collector = (collector_t *)user;
  collector->packet = *packet;
  ++collector->count;
  return collector->stop;
}

static void fixture(void) {
  static const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d};
  for (size_t i = 0; i < PO35_PREAMBLE_BYTES - 4; ++i)
    frame[i] = (uint8_t)i;
  memcpy(frame + PO35_PREAMBLE_BYTES - 4, sync, 4);
  memcpy(frame + PO35_PREAMBLE_BYTES, unknown_and_tail, sizeof(unknown_and_tail));
}

static void test_frames(void) {
  const size_t length = PO35_PREAMBLE_BYTES + sizeof(unknown_and_tail);
  po35_decode_result_t result;
  collector_t collector = {0};
  po_status_t status;
  fixture();
  status = po35_frame_parse(frame, length, collect, &collector, &result);
  assert(status == PO_OK);
  assert(result.done && !result.stopped && result.packet_count == 1);
  assert(result.frame_bytes == length && result.final_state == 0xe36d);
  assert(collector.count == 1 && collector.packet.tag_code == 0x1234);
  assert(collector.packet.offset == PO35_PREAMBLE_BYTES && collector.packet.payload_len == 4);
  assert(collector.packet.trailer.matches_state);
  assert(memcmp(collector.packet.payload, payload, sizeof(payload)) == 0);
  frame[length] = 0xaa;
  assert(po35_frame_parse(frame, length + 1, NULL, NULL, &result) == PO_OK);
  assert(result.done && result.frame_bytes == length);
  collector.stop = 1;
  status = po35_frame_parse(frame, length, collect, &collector, &result);
  assert(status == PO_OK);
  assert(result.stopped && !result.done && result.packet_count == 1);
  assert(result.frame_bytes == length - 5 && result.final_state == 0xaa28);
  for (size_t n = 0; n < length; ++n) {
    assert(po35_frame_parse(frame, n, NULL, NULL, &result) == PO_ERR_FRAME);
    assert(!result.done && !result.stopped);
  }
  for (size_t i = 0; i < length; ++i) {
    frame[i] ^= 1;
    assert(po35_frame_parse(frame, length, NULL, NULL, &result) == PO_ERR_FRAME);
    assert(!result.done);
    frame[i] ^= 1;
  }
  assert(po35_frame_parse(NULL, length, NULL, NULL, &result) == PO_ERR_INVALID_ARG);
  memcpy(frame + PO35_PREAMBLE_BYTES, empty_tail, sizeof(empty_tail));
  assert(po35_frame_parse(frame, PO35_PREAMBLE_BYTES + 5, NULL, NULL, &result) == PO_OK);
  assert(result.done && result.packet_count == 0 && result.final_state == 0x5132);
}

static void test_builder(void) {
  po35_builder_t builder, before;
  po35_decode_result_t result;
  uint8_t saved[sizeof(frame)], maximum[255];
  size_t length, offset = 123;
  memset(frame, 0xa5, sizeof(frame));
  assert(po35_builder_init(&builder, frame, PO35_PREAMBLE_BYTES - 1) == PO_ERR_BUFFER_TOO_SMALL);
  assert(builder.buffer == NULL && builder.length == 0 && frame[0] == 0xa5);
  assert(po35_builder_init(&builder, frame, PO35_PREAMBLE_BYTES) == PO_OK);
  before = builder;
  assert(po35_builder_finish(&builder, &offset) == PO_ERR_BUFFER_TOO_SMALL);
  assert(memcmp(&builder, &before, sizeof(builder)) == 0 && offset == 123);
  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  assert(builder.length == PO35_PREAMBLE_BYTES);
  assert(po35_builder_append_packet(&builder, 0x1234, payload, 4, &offset) == PO_OK);
  assert(offset == PO35_PREAMBLE_BYTES);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  assert(length == PO35_PREAMBLE_BYTES + sizeof(unknown_and_tail));
  assert(memcmp(frame + PO35_PREAMBLE_BYTES, unknown_and_tail, sizeof(unknown_and_tail)) == 0);
  before = builder;
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  assert(memcmp(&builder, &before, sizeof(builder)) == 0);
  assert(po35_builder_append_packet(&builder, 0x1234, NULL, 0, NULL) == PO_ERR_FRAME);
  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  before = builder;
  memcpy(saved, frame, sizeof(frame));
  assert(po35_builder_append_packet(&builder, 0x71c3, NULL, 0, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_builder_append_packet(&builder, 0x1234, NULL, 1, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_builder_append_packet(&builder, 0x1234, payload, (size_t)-1, NULL) == PO_ERR_RANGE);
  assert(memcmp(&builder, &before, sizeof(builder)) == 0);
  assert(memcmp(frame, saved, sizeof(frame)) == 0);
  builder.capacity = builder.length + 8;
  before = builder;
  offset = 123;
  assert(po35_builder_append_packet(&builder, 0x1234, payload, 4, &offset) ==
         PO_ERR_BUFFER_TOO_SMALL);
  assert(memcmp(&builder, &before, sizeof(builder)) == 0 && offset == 123);
  assert(memcmp(frame, saved, sizeof(frame)) == 0);
  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  for (size_t i = 0; i < sizeof(maximum); ++i)
    maximum[i] = (uint8_t)i;
  assert(po35_builder_append_packet(&builder, 0xffff, maximum, sizeof(maximum), NULL) == PO_OK);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  assert(po35_frame_parse(frame, length, NULL, NULL, &result) == PO_OK && result.done);
  assert(po35_builder_init(NULL, frame, sizeof(frame)) == PO_ERR_INVALID_ARG);
  assert(po35_builder_init(&builder, NULL, sizeof(frame)) == PO_ERR_INVALID_ARG);
}

static void test_speech_codecs(void) {
  const uint8_t header_bytes[] = {14, 0x12, 0x34, 80, 140, 1, 0x80, 0xff, 42};
  po35_speech_header_t header;
  po35_speech_record_t record;
  uint8_t output[44], input[44];
  assert(po35_speech_header_decode(header_bytes, sizeof(header_bytes), &header) == PO_OK);
  assert(header.slot == 14 && header.record_count == 0x1234 && header.records_per_second == 80);
  assert(memcmp(header.metadata, header_bytes + 4, 5) == 0);
  assert(po35_speech_header_encode(&header, output, sizeof(output)) == PO_OK);
  assert(memcmp(output, header_bytes, sizeof(header_bytes)) == 0);
  assert(po35_speech_header_decode(header_bytes, 8, &header) == PO_ERR_PARSE);
  memset(output, 0xa5, sizeof(output));
  assert(po35_speech_header_encode(&header, output, 8) == PO_ERR_BUFFER_TOO_SMALL);
  assert(output[0] == 0xa5);
  for (size_t i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(128 + i);
  input[0] = 0;
  input[1] = 0x80;
  assert(po35_speech_record_decode(input, sizeof(input), &record) == PO_OK);
  assert(record.pitch == -32768 && record.gain == 130 && record.unvoiced == 131);
  assert(record.low_lar_raw[0] == -124 && record.high_lar_raw[29] == -85);
  assert(po35_speech_record_encode(&record, output, sizeof(output)) == PO_OK);
  assert(memcmp(output, input, sizeof(input)) == 0);
  assert(po35_speech_record_decode(input, 43, &record) == PO_ERR_PARSE);
  assert(po35_speech_record_decode(NULL, 44, &record) == PO_ERR_INVALID_ARG);
  assert(po35_speech_record_encode(&record, output, 43) == PO_ERR_BUFFER_TOO_SMALL);
}

static void test_speech_edit_api_surface(void) {
  static const uint8_t records[PO35_SPEECH_RECORD_BYTES] = {0};
  const po35_speech_edit_t edit = {
      PO35_SPEECH_SLOT_COUNT - 1u,
      80u,
      records,
      sizeof(records),
  };
  const po35_speech_edit_t empty = {
      0u,
      20u,
      NULL,
      0u,
  };
  po35_speech_edit_plan_t plan = {
      1u, 1u, 1u, SIZE_MAX, 1u, UINT8_MAX, PO35_SPEECH_EDIT_REASON_NONE,
  };

  assert(sizeof(edit.slot) == sizeof(uint8_t));
  assert(sizeof(edit.records_per_second) == sizeof(uint8_t));
  assert(sizeof(edit.records) == sizeof(const uint8_t *));
  assert(sizeof(edit.record_bytes) == sizeof(size_t));
  assert(offsetof(po35_speech_edit_t, slot) == 0u);
  assert(offsetof(po35_speech_edit_t, records_per_second) > offsetof(po35_speech_edit_t, slot));
  assert(offsetof(po35_speech_edit_t, records) > offsetof(po35_speech_edit_t, records_per_second));
  assert(offsetof(po35_speech_edit_t, record_bytes) > offsetof(po35_speech_edit_t, records));
  assert(edit.slot == PO35_SPEECH_SLOT_COUNT - 1u);
  assert(edit.records_per_second == 80u);
  assert(edit.records == records && edit.record_bytes == sizeof(records));
  assert(empty.slot == 0u && empty.records_per_second == 20u);
  assert(empty.records == NULL && empty.record_bytes == 0u);
  assert(sizeof(plan.required_workspace_bytes) == sizeof(size_t));
  assert(sizeof(plan.required_frame_bytes) == sizeof(size_t));
  assert(sizeof(plan.affected_slot_count) == sizeof(size_t));
  assert(sizeof(plan.failed_edit_index) == sizeof(size_t));
  assert(sizeof(plan.selected_slot_mask) == sizeof(uint16_t));
  assert(sizeof(plan.failed_slot) == sizeof(uint8_t));
  assert(offsetof(po35_speech_edit_plan_t, required_workspace_bytes) == 0u);
  assert(offsetof(po35_speech_edit_plan_t, required_frame_bytes) >
         offsetof(po35_speech_edit_plan_t, required_workspace_bytes));
  assert(offsetof(po35_speech_edit_plan_t, affected_slot_count) >
         offsetof(po35_speech_edit_plan_t, required_frame_bytes));
  assert(offsetof(po35_speech_edit_plan_t, failed_edit_index) >
         offsetof(po35_speech_edit_plan_t, affected_slot_count));
  assert(offsetof(po35_speech_edit_plan_t, selected_slot_mask) >
         offsetof(po35_speech_edit_plan_t, failed_edit_index));
  assert(offsetof(po35_speech_edit_plan_t, failed_slot) >
         offsetof(po35_speech_edit_plan_t, selected_slot_mask));
  assert(offsetof(po35_speech_edit_plan_t, reason) >
         offsetof(po35_speech_edit_plan_t, failed_slot));
  assert(plan.reason == PO35_SPEECH_EDIT_REASON_NONE);
  assert(plan.selected_slot_mask == 1u && plan.affected_slot_count == 1u);
  assert(plan.failed_edit_index == SIZE_MAX && plan.failed_slot == UINT8_MAX);
  assert(PO35_SPEECH_EDIT_REASON_NONE == 0);
  assert(PO35_SPEECH_EDIT_REASON_DUPLICATE_SLOT == 1);
  assert(PO35_SPEECH_EDIT_REASON_SLOT_OUT_OF_RANGE == 2);
  assert(PO35_SPEECH_EDIT_REASON_INVALID_RECORD_SIZE == 3);
  assert(PO35_SPEECH_EDIT_REASON_INVALID_RECORD_COUNT == 4);
  assert(PO35_SPEECH_EDIT_REASON_ABSENT_SLOT == 5);
  assert(PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP == 6);
  assert(PO35_SPEECH_EDIT_REASON_UNKNOWN_SELECTED_GROUP_EXTENSION == 7);
}

static void speech_plan_append(po35_builder_t *builder, uint16_t tag, const uint8_t *payload,
                               size_t payload_len) {
  assert(po35_builder_append_packet(builder, tag, payload, payload_len, NULL) == PO_OK);
}

static void speech_plan_append_header(po35_builder_t *builder, uint8_t slot, size_t record_count) {
  uint8_t header[PO35_SPEECH_HEADER_BYTES] = {slot, 0, 0, 60, 140, 1, 2, 3, 4};
  assert(record_count <= UINT16_MAX);
  header[1] = (uint8_t)(record_count >> 8);
  header[2] = (uint8_t)record_count;
  speech_plan_append(builder, PO35_TAG_SPEECH_HEADER, header, sizeof(header));
}

static void speech_plan_append_group(po35_builder_t *builder, uint8_t slot, size_t record_count,
                                     const uint8_t *records, const size_t *segments,
                                     size_t segment_count, int opaque_extension) {
  static const uint8_t extension[] = {3, 1, 4, 1, 5};
  size_t offset = 0;
  speech_plan_append_header(builder, slot, record_count);
  for (size_t i = 0; i < segment_count; ++i) {
    assert(segments[i] != 0u);
    assert(segments[i] <= record_count * PO35_SPEECH_RECORD_BYTES - offset);
    speech_plan_append(builder, PO35_TAG_SPEECH_DATA, records + offset, segments[i]);
    offset += segments[i];
  }
  assert(offset == record_count * PO35_SPEECH_RECORD_BYTES);
  if (opaque_extension)
    speech_plan_append(builder, 0x5678u, extension, sizeof(extension));
  speech_plan_append(builder, PO35_TAG_SPEECH_END, NULL, 0u);
}

static void speech_plan_finish(po35_builder_t *builder, size_t *frame_len) {
  assert(po35_builder_finish(builder, frame_len) == PO_OK);
}

static void assert_speech_plan_neutral(const po35_speech_edit_plan_t *plan) {
  assert(plan->required_workspace_bytes == 0u);
  assert(plan->required_frame_bytes == 0u);
  assert(plan->affected_slot_count == 0u);
  assert(plan->failed_edit_index == SIZE_MAX);
  assert(plan->selected_slot_mask == 0u);
  assert(plan->failed_slot == UINT8_MAX);
  assert(plan->reason == PO35_SPEECH_EDIT_REASON_NONE);
}

static void assert_speech_plan_failure(const po35_speech_edit_plan_t *plan, size_t edit_index,
                                       uint8_t slot, po35_speech_edit_reason_t reason) {
  assert(plan->required_workspace_bytes == 0u);
  assert(plan->required_frame_bytes == 0u);
  assert(plan->affected_slot_count == 0u);
  assert(plan->failed_edit_index == edit_index);
  assert(plan->selected_slot_mask == 0u);
  assert(plan->failed_slot == slot);
  assert(plan->reason == reason);
}

static void test_speech_edit_plan_measurement(void) {
  uint8_t source[PO35_PREAMBLE_BYTES + 2048];
  uint8_t source_records[4 * PO35_SPEECH_RECORD_BYTES];
  uint8_t replacement_records[6 * PO35_SPEECH_RECORD_BYTES];
  const size_t first_segments[] = {13u, 31u};
  const size_t second_segments[] = {44u, 88u, 44u};
  const size_t third_segments[] = {44u};
  const po35_speech_edit_t edits[] = {
      {2u, 80u, replacement_records, sizeof(replacement_records)},
      {5u, 20u, replacement_records, 2u * PO35_SPEECH_RECORD_BYTES},
  };
  po35_builder_t builder;
  po35_speech_edit_plan_t plan;
  size_t source_len;
  for (size_t i = 0; i < sizeof(source_records); ++i)
    source_records[i] = (uint8_t)(i + 1u);
  for (size_t i = 0; i < sizeof(replacement_records); ++i)
    replacement_records[i] = (uint8_t)(0xa0u + i);
  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_group(&builder, 2u, 1u, source_records, first_segments,
                           sizeof(first_segments) / sizeof(first_segments[0]), 0);
  speech_plan_append_group(&builder, 5u, 4u, source_records, second_segments,
                           sizeof(second_segments) / sizeof(second_segments[0]), 0);
  speech_plan_append_group(&builder, 7u, 1u, source_records, third_segments,
                           sizeof(third_segments) / sizeof(third_segments[0]), 1);
  speech_plan_finish(&builder, &source_len);

  memset(&plan, 0xa5, sizeof(plan));
  assert(po35_speech_edit_plan(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                               &plan) == PO_OK);
  assert(plan.required_workspace_bytes == plan.required_frame_bytes);
  /* Remove split source data (54 + 191 wire bytes), then add 6- and
   * 2-record replacements (274 + 93 wire bytes). */
  assert(plan.required_frame_bytes == source_len + 122u);
  assert(plan.affected_slot_count == 2u);
  assert(plan.selected_slot_mask == ((uint16_t)(1u << 2) | (uint16_t)(1u << 5)));
  assert(plan.failed_edit_index == SIZE_MAX);
  assert(plan.failed_slot == UINT8_MAX);
  assert(plan.reason == PO35_SPEECH_EDIT_REASON_NONE);
  assert(po35_speech_edit_plan(source, source_len, NULL, 0u, &plan) == PO_OK);
  assert(plan.required_workspace_bytes == source_len);
  assert(plan.required_frame_bytes == source_len);
  assert(plan.affected_slot_count == 0u);
  assert(plan.selected_slot_mask == 0u);
  assert(plan.failed_edit_index == SIZE_MAX);
  assert(plan.failed_slot == UINT8_MAX);
  assert(plan.reason == PO35_SPEECH_EDIT_REASON_NONE);
}

static void test_speech_edit_plan_descriptor_failures(void) {
  static uint8_t too_many_records[((size_t)UINT16_MAX + 1u) * PO35_SPEECH_RECORD_BYTES];
  uint8_t source[PO35_PREAMBLE_BYTES + 128];
  uint8_t records[PO35_SPEECH_RECORD_BYTES] = {0};
  union {
    uint8_t frame[PO35_PREAMBLE_BYTES + 128];
    po35_speech_edit_plan_t plan;
  } alias;
  union {
    uint8_t records[PO35_SPEECH_RECORD_BYTES];
    po35_speech_edit_plan_t plan;
  } plan_record_alias;
  po35_builder_t builder;
  po35_speech_edit_plan_t plan;
  po35_speech_edit_t edit = {2u, 80u, records, sizeof(records)};
  po35_speech_edit_t duplicate[] = {
      {2u, 80u, records, sizeof(records)},
      {2u, 40u, records, sizeof(records)},
  };
  size_t source_len;
  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, 2u, 0u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  speech_plan_finish(&builder, &source_len);

  edit.records = NULL;
  edit.record_bytes = 0u;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_OK);
  assert(plan.required_workspace_bytes == source_len);
  assert(plan.required_frame_bytes == source_len);
  assert(plan.affected_slot_count == 1u);
  assert(plan.selected_slot_mask == (uint16_t)(1u << 2));
  assert(plan.reason == PO35_SPEECH_EDIT_REASON_NONE);
  edit.records = records;
  edit.record_bytes = sizeof(records);

  memset(&plan, 0xa5, sizeof(plan));
  assert(po35_speech_edit_plan(NULL, source_len, &edit, 1u, &plan) == PO_ERR_INVALID_ARG);
  assert_speech_plan_neutral(&plan);
  memset(&plan, 0xa5, sizeof(plan));
  assert(po35_speech_edit_plan(source, source_len, NULL, 1u, &plan) == PO_ERR_INVALID_ARG);
  assert_speech_plan_neutral(&plan);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, NULL) == PO_ERR_INVALID_ARG);

  memset(&plan_record_alias, 0xa5, sizeof(plan_record_alias));
  edit.records = plan_record_alias.records;
  edit.record_bytes = PO35_SPEECH_RECORD_BYTES - 1u;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan_record_alias.plan) ==
         PO_ERR_INVALID_ARG);
  edit.records = records;
  edit.record_bytes = sizeof(records);

  memset(&plan, 0xa5, sizeof(plan));
  assert(po35_speech_edit_plan(source, source_len, &edit, SIZE_MAX / sizeof(edit) + 1u, &plan) ==
         PO_ERR_RANGE);
  assert_speech_plan_neutral(&plan);

  assert(po35_speech_edit_plan(source, source_len, duplicate, 2u, &plan) == PO_ERR_RANGE);
  assert_speech_plan_failure(&plan, 1u, 2u, PO35_SPEECH_EDIT_REASON_DUPLICATE_SLOT);
  edit.slot = PO35_SPEECH_SLOT_COUNT;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_RANGE);
  assert_speech_plan_failure(&plan, 0u, PO35_SPEECH_SLOT_COUNT,
                             PO35_SPEECH_EDIT_REASON_SLOT_OUT_OF_RANGE);
  edit.slot = 2u;
  edit.record_bytes = sizeof(records) - 1u;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_RANGE);
  assert_speech_plan_failure(&plan, 0u, 2u, PO35_SPEECH_EDIT_REASON_INVALID_RECORD_SIZE);
  edit.records = too_many_records;
  edit.record_bytes = ((size_t)UINT16_MAX + 1u) * PO35_SPEECH_RECORD_BYTES;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_RANGE);
  assert_speech_plan_failure(&plan, 0u, 2u, PO35_SPEECH_EDIT_REASON_INVALID_RECORD_COUNT);
  edit.records = records;
  edit.record_bytes = sizeof(records);
  edit.records = NULL;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_INVALID_ARG);
  assert_speech_plan_neutral(&plan);

  edit.records = records;
  assert(po35_builder_init(&builder, alias.frame, sizeof(alias.frame)) == PO_OK);
  speech_plan_append_header(&builder, 2u, 0u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(alias.frame, source_len, &edit, 1u, &alias.plan) ==
         PO_ERR_INVALID_ARG);
}

static void test_speech_edit_plan_group_failures(void) {
  uint8_t source[PO35_PREAMBLE_BYTES + 512];
  uint8_t records[PO35_SPEECH_RECORD_BYTES] = {0};
  const size_t segment[] = {PO35_SPEECH_RECORD_BYTES};
  const po35_speech_edit_t edit = {2u, 80u, records, sizeof(records)};
  po35_builder_t builder;
  po35_speech_edit_plan_t plan;
  size_t source_len;

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, 3u, 0u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_failure(&plan, 0u, 2u, PO35_SPEECH_EDIT_REASON_ABSENT_SLOT);

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, PO35_SPEECH_SLOT_COUNT, 0u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_neutral(&plan);

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, 2u, 1u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_failure(&plan, 0u, 2u, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, 2u, 1u);
  speech_plan_append_header(&builder, 3u, 0u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_failure(&plan, 0u, 2u, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, 2u, 1u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_DATA, records, sizeof(records));
  speech_plan_append(&builder, 0x5678u, records, 1u);
  speech_plan_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_failure(&plan, 0u, 2u,
                             PO35_SPEECH_EDIT_REASON_UNKNOWN_SELECTED_GROUP_EXTENSION);

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_header(&builder, 2u, 1u);
  speech_plan_append(&builder, PO35_TAG_SETTINGS, records, 1u);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_failure(&plan, 0u, 2u, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);
  source[source_len - 1u] ^= 1u;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_FRAME);
  assert_speech_plan_neutral(&plan);
  source[source_len - 1u] ^= 1u;

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append(&builder, PO35_TAG_SPEECH_DATA, records, sizeof(records));
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_neutral(&plan);

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_group(&builder, 2u, 1u, records, segment, 1u, 0);
  speech_plan_finish(&builder, &source_len);
  source[source_len] = 0xa5u;
  assert(po35_speech_edit_plan(source, source_len + 1u, &edit, 1u, &plan) == PO_ERR_FRAME);
  assert_speech_plan_neutral(&plan);
  source[0] ^= 1u;
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_FRAME);
  assert_speech_plan_neutral(&plan);
  source[0] ^= 1u;

  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append_group(&builder, 2u, 1u, records, segment, 1u, 0);
  speech_plan_append_group(&builder, 2u, 1u, records, segment, 1u, 0);
  speech_plan_finish(&builder, &source_len);
  assert(po35_speech_edit_plan(source, source_len, &edit, 1u, &plan) == PO_ERR_PARSE);
  assert_speech_plan_neutral(&plan);
}

typedef struct speech_rebuild_packets {
  po_packet_t packets[64];
  size_t count;
} speech_rebuild_packets_t;

static int speech_rebuild_collect_packet(const po_packet_t *packet, void *user) {
  speech_rebuild_packets_t *packets = (speech_rebuild_packets_t *)user;
  assert(packets->count < sizeof(packets->packets) / sizeof(packets->packets[0]));
  packets->packets[packets->count++] = *packet;
  return 0;
}

static void speech_rebuild_collect_frame(const uint8_t *frame, size_t frame_len,
                                         speech_rebuild_packets_t *packets) {
  po35_decode_result_t result;
  po_status_t status;
  *packets = (speech_rebuild_packets_t){0};
  status = po35_frame_parse(frame, frame_len, speech_rebuild_collect_packet, packets, &result);
  assert(status == PO_OK);
  assert(result.done && !result.stopped && result.frame_bytes == frame_len);
}

static const po_packet_t *speech_rebuild_find_header(const speech_rebuild_packets_t *packets,
                                                     uint8_t slot) {
  for (size_t i = 0; i < packets->count; ++i)
    if (packets->packets[i].tag_code == PO35_TAG_SPEECH_HEADER &&
        packets->packets[i].payload[0] == slot)
      return packets->packets + i;
  return NULL;
}

static void test_speech_edit_rebuild(void) {
  uint8_t source[PO35_PREAMBLE_BYTES + 4096];
  uint8_t malformed[sizeof(source)];
  uint8_t source_before[sizeof(source)];
  uint8_t source_records[3 * PO35_SPEECH_RECORD_BYTES];
  uint8_t replacement_a[6 * PO35_SPEECH_RECORD_BYTES];
  uint8_t replacement_b[PO35_SPEECH_RECORD_BYTES];
  uint8_t workspace[PO35_PREAMBLE_BYTES + 4096];
  uint8_t output[PO35_PREAMBLE_BYTES + 4096];
  uint8_t settings[PO35_SETTINGS_BASE_BYTES] = {0};
  uint8_t raw[] = {0x12u, 0x34u};
  const size_t split[] = {13u, 31u};
  const size_t whole[] = {PO35_SPEECH_RECORD_BYTES};
  const po35_speech_edit_t edits[] = {
      {2u, 80u, replacement_a, sizeof(replacement_a)},
      {5u, 20u, NULL, 0u},
  };
  po35_speech_edit_plan_t plan;
  po35_speech_edit_plan_t mismatched_plan;
  po35_builder_t builder;
  speech_rebuild_packets_t source_packets;
  speech_rebuild_packets_t output_packets;
  const po_packet_t *source_header;
  const po_packet_t *output_header;
  size_t source_len;
  size_t output_len = 99u;

  for (size_t i = 0; i < sizeof(source_records); ++i)
    source_records[i] = (uint8_t)(0x10u + i);
  for (size_t i = 0; i < sizeof(replacement_a); ++i)
    replacement_a[i] = (uint8_t)(0xa0u + i);
  for (size_t i = 0; i < sizeof(replacement_b); ++i)
    replacement_b[i] = (uint8_t)(0x50u + i);
  assert(po35_builder_init(&builder, source, sizeof(source)) == PO_OK);
  speech_plan_append(&builder, PO35_TAG_SETTINGS, settings, sizeof(settings));
  speech_plan_append(&builder, 0x4321u, raw, sizeof(raw));
  speech_plan_append_group(&builder, 2u, 1u, source_records, split,
                           sizeof(split) / sizeof(split[0]), 0);
  speech_plan_append_group(&builder, 5u, 1u, source_records + PO35_SPEECH_RECORD_BYTES, whole,
                           sizeof(whole) / sizeof(whole[0]), 0);
  speech_plan_append_group(&builder, 7u, 1u, source_records + 2u * PO35_SPEECH_RECORD_BYTES, whole,
                           sizeof(whole) / sizeof(whole[0]), 1);
  speech_plan_finish(&builder, &source_len);

  assert(po35_speech_edit_plan(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                               &plan) == PO_OK);
  assert(plan.required_workspace_bytes == plan.required_frame_bytes);
  assert(plan.required_frame_bytes <= sizeof(workspace));
  speech_rebuild_collect_frame(source, source_len, &source_packets);
  source_header = speech_rebuild_find_header(&source_packets, 2u);
  assert(source_header != NULL);

  memset(output, 0xa5, sizeof(output));
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes, output,
                                  sizeof(output), &output_len) == PO_OK);
  assert(output_len == plan.required_frame_bytes);
  speech_rebuild_collect_frame(output, output_len, &output_packets);
  output_header = speech_rebuild_find_header(&output_packets, 2u);
  assert(output_header != NULL && output_header->payload_len == PO35_SPEECH_HEADER_BYTES);
  assert(output_header->payload[1] == 0u && output_header->payload[2] == 6u &&
         output_header->payload[3] == 80u &&
         memcmp(output_header->payload + 4u, source_header->payload + 4u, 5u) == 0);
  output_header = speech_rebuild_find_header(&output_packets, 5u);
  assert(output_header != NULL && output_header->payload[1] == 0u &&
         output_header->payload[2] == 0u && output_header->payload[3] == 20u);
  assert(speech_rebuild_find_header(&output_packets, 7u) != NULL);

  {
    size_t slot2_data_packets = 0u;
    size_t slot2_data_bytes = 0u;
    size_t slot5_data_packets = 0u;
    int active_slot = -1;
    int found_raw = 0;
    int found_extension = 0;
    for (size_t i = 0; i < output_packets.count; ++i) {
      const po_packet_t *packet = output_packets.packets + i;
      if (packet->tag_code == PO35_TAG_SPEECH_HEADER)
        active_slot = packet->payload[0];
      else if (packet->tag_code == PO35_TAG_SPEECH_END)
        active_slot = -1;
      else if (packet->tag_code == PO35_TAG_SPEECH_DATA && active_slot == 2) {
        ++slot2_data_packets;
        slot2_data_bytes += packet->payload_len;
      } else if (packet->tag_code == PO35_TAG_SPEECH_DATA && active_slot == 5) {
        ++slot5_data_packets;
      } else if (packet->tag_code == 0x4321u) {
        found_raw =
            packet->payload_len == sizeof(raw) && !memcmp(packet->payload, raw, sizeof(raw));
      } else if (packet->tag_code == 0x5678u && active_slot == 7) {
        found_extension = 1;
      }
    }
    assert(slot2_data_packets == 2u && slot2_data_bytes == sizeof(replacement_a));
    assert(slot5_data_packets == 0u && found_raw && found_extension);
  }

  assert(po35_speech_edit_plan(source, source_len, NULL, 0u, &plan) == PO_OK);
  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, NULL, 0u, &plan, workspace,
                                  plan.required_workspace_bytes, output, sizeof(output),
                                  &output_len) == PO_OK);
  assert(output_len == source_len && memcmp(output, source, source_len) == 0);

  assert(po35_speech_edit_plan(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                               &plan) == PO_OK);

  replacement_a[0] ^= 0xffu;
  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes, output,
                                  sizeof(output), &output_len) == PO_OK);
  speech_rebuild_collect_frame(output, output_len, &output_packets);
  {
    int active_slot = -1;
    int found_current_records = 0;
    for (size_t i = 0; i < output_packets.count; ++i) {
      const po_packet_t *packet = output_packets.packets + i;
      if (packet->tag_code == PO35_TAG_SPEECH_HEADER)
        active_slot = packet->payload[0];
      else if (packet->tag_code == PO35_TAG_SPEECH_END)
        active_slot = -1;
      else if (packet->tag_code == PO35_TAG_SPEECH_DATA && active_slot == 2) {
        assert(packet->payload[0] == replacement_a[0]);
        found_current_records = 1;
        break;
      }
    }
    assert(found_current_records);
  }

  mismatched_plan = plan;
  ++mismatched_plan.required_frame_bytes;
  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &mismatched_plan, workspace, plan.required_workspace_bytes,
                                  output, sizeof(output), &output_len) == PO_ERR_INVALID_ARG);
  assert(output_len == 0u);
  for (size_t i = 0; i < sizeof(output); ++i)
    assert(output[i] == 0xa5u);

  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes - 1u, output,
                                  sizeof(output), &output_len) == PO_ERR_BUFFER_TOO_SMALL);
  assert(output_len == 0u);
  for (size_t i = 0; i < sizeof(output); ++i)
    assert(output[i] == 0xa5u);

  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes, output,
                                  plan.required_frame_bytes - 1u,
                                  &output_len) == PO_ERR_BUFFER_TOO_SMALL);
  assert(output_len == 0u);
  for (size_t i = 0; i < sizeof(output); ++i)
    assert(output[i] == 0xa5u);

  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(NULL, source_len, edits, sizeof(edits) / sizeof(edits[0]), &plan,
                                  workspace, plan.required_workspace_bytes, output, sizeof(output),
                                  &output_len) == PO_ERR_INVALID_ARG);
  assert(output_len == 0u);
  for (size_t i = 0; i < sizeof(output); ++i)
    assert(output[i] == 0xa5u);

  memcpy(source_before, source, sizeof(source));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes, source,
                                  sizeof(source), &output_len) == PO_ERR_INVALID_ARG);
  assert(output_len == 99u && memcmp(source, source_before, sizeof(source)) == 0);

  output_len = 99u;
  assert(po35_speech_edit_rebuild(source, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes, workspace,
                                  sizeof(workspace), &output_len) == PO_ERR_INVALID_ARG);
  assert(output_len == 99u);

  memcpy(malformed, source, sizeof(malformed));
  ++malformed[0];
  memset(output, 0xa5, sizeof(output));
  output_len = 99u;
  assert(po35_speech_edit_rebuild(malformed, source_len, edits, sizeof(edits) / sizeof(edits[0]),
                                  &plan, workspace, plan.required_workspace_bytes, output,
                                  sizeof(output), &output_len) == PO_ERR_INVALID_ARG);
  assert(output_len == 0u);
  for (size_t i = 0; i < sizeof(output); ++i)
    assert(output[i] == 0xa5u);

  assert(replacement_b[0] == 0x50u);
}

static const uint8_t table_7be7_bytes[][9] = {
    {0x03, 0x00, 0x7f, 0x80, 0xff, 0x12, 0x34, 0x56, 0x78},
    {0xfe, 0xe1, 0xc2, 0xa3, 0x84, 0x65, 0x46, 0x27, 0x08},
};
static const uint8_t table_1c7f_bytes[][13] = {
    {0x07, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x00, 0x7f, 0x80, 0xff},
    {0xff, 0xe3, 0xc7, 0xab, 0x8f, 0x73, 0x57, 0x3b, 0x1f, 0x02, 0x24, 0x46, 0x68},
};

static void test_indexed_table_golden_records(void) {
  const po35_table_7be7_t values7[] = {
      {0x03, {0x00, 0x7f, 0x80, 0xff, 0x12, 0x34, 0x56, 0x78}},
      {0xfe, {0xe1, 0xc2, 0xa3, 0x84, 0x65, 0x46, 0x27, 0x08}},
  };
  const po35_table_1c7f_t values1[] = {
      {0x07, {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x00, 0x7f, 0x80, 0xff}},
      {0xff, {0xe3, 0xc7, 0xab, 0x8f, 0x73, 0x57, 0x3b, 0x1f, 0x02, 0x24, 0x46, 0x68}},
  };
  const uint8_t tail[] = {0xa5, 0xa5, 0xa5};
  po35_table_7be7_t value7;
  po35_table_1c7f_t value1;
  uint8_t output7[PO35_TABLE_7BE7_BYTES + sizeof(tail)];
  uint8_t output1[PO35_TABLE_1C7F_BYTES + sizeof(tail)];
  for (size_t i = 0; i < 2; ++i) {
    assert(po35_table_7be7_decode(table_7be7_bytes[i], sizeof(table_7be7_bytes[i]), &value7) ==
           PO_OK);
    assert(value7.index == values7[i].index);
    assert(memcmp(value7.data_raw, values7[i].data_raw, sizeof(value7.data_raw)) == 0);
    memset(output7, 0xa5, sizeof(output7));
    assert(po35_table_7be7_encode(&values7[i], output7, sizeof(output7)) == PO_OK);
    assert(memcmp(output7, table_7be7_bytes[i], sizeof(table_7be7_bytes[i])) == 0);
    assert(memcmp(output7 + PO35_TABLE_7BE7_BYTES, tail, sizeof(tail)) == 0);

    assert(po35_table_1c7f_decode(table_1c7f_bytes[i], sizeof(table_1c7f_bytes[i]), &value1) ==
           PO_OK);
    assert(value1.index == values1[i].index);
    assert(memcmp(value1.data_raw, values1[i].data_raw, sizeof(value1.data_raw)) == 0);
    memset(output1, 0xa5, sizeof(output1));
    assert(po35_table_1c7f_encode(&values1[i], output1, sizeof(output1)) == PO_OK);
    assert(memcmp(output1, table_1c7f_bytes[i], sizeof(table_1c7f_bytes[i])) == 0);
    assert(memcmp(output1 + PO35_TABLE_1C7F_BYTES, tail, sizeof(tail)) == 0);
  }
}

static void test_indexed_table_byte_values(void) {
  po35_table_7be7_t value7;
  po35_table_1c7f_t value1;
  uint8_t input7[PO35_TABLE_7BE7_BYTES], output7[PO35_TABLE_7BE7_BYTES];
  uint8_t input1[PO35_TABLE_1C7F_BYTES], output1[PO35_TABLE_1C7F_BYTES];
  for (size_t pos = 0; pos < sizeof(input7); ++pos) {
    for (unsigned byte = 0; byte <= 255; ++byte) {
      memcpy(input7, table_7be7_bytes[0], sizeof(input7));
      input7[pos] = (uint8_t)byte;
      assert(po35_table_7be7_decode(input7, sizeof(input7), &value7) == PO_OK);
      assert(value7.index == input7[0]);
      assert(memcmp(value7.data_raw, input7 + 1, sizeof(value7.data_raw)) == 0);
      assert(po35_table_7be7_encode(&value7, output7, sizeof(output7)) == PO_OK);
      assert(memcmp(output7, input7, sizeof(input7)) == 0);
    }
  }
  for (size_t pos = 0; pos < sizeof(input1); ++pos) {
    for (unsigned byte = 0; byte <= 255; ++byte) {
      memcpy(input1, table_1c7f_bytes[0], sizeof(input1));
      input1[pos] = (uint8_t)byte;
      assert(po35_table_1c7f_decode(input1, sizeof(input1), &value1) == PO_OK);
      assert(value1.index == input1[0]);
      assert(memcmp(value1.data_raw, input1 + 1, sizeof(value1.data_raw)) == 0);
      assert(po35_table_1c7f_encode(&value1, output1, sizeof(output1)) == PO_OK);
      assert(memcmp(output1, input1, sizeof(input1)) == 0);
    }
  }
}

static void test_indexed_table_edit_locality(void) {
  po35_table_7be7_t value7, before7;
  po35_table_1c7f_t value1, before1;
  uint8_t expected7[PO35_TABLE_7BE7_BYTES], output7[PO35_TABLE_7BE7_BYTES];
  uint8_t expected1[PO35_TABLE_1C7F_BYTES], output1[PO35_TABLE_1C7F_BYTES];
  for (size_t pos = 0; pos < sizeof(expected7); ++pos) {
    assert(po35_table_7be7_decode(table_7be7_bytes[0], sizeof(table_7be7_bytes[0]), &value7) ==
           PO_OK);
    memcpy(expected7, table_7be7_bytes[0], sizeof(expected7));
    expected7[pos] ^= 0xff;
    if (pos == 0)
      value7.index ^= 0xff;
    else
      value7.data_raw[pos - 1] ^= 0xff;
    memcpy(&before7, &value7, sizeof(before7));
    assert(po35_table_7be7_encode(&value7, output7, sizeof(output7)) == PO_OK);
    assert(memcmp(output7, expected7, sizeof(expected7)) == 0);
    assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
  }
  for (size_t pos = 0; pos < sizeof(expected1); ++pos) {
    assert(po35_table_1c7f_decode(table_1c7f_bytes[0], sizeof(table_1c7f_bytes[0]), &value1) ==
           PO_OK);
    memcpy(expected1, table_1c7f_bytes[0], sizeof(expected1));
    expected1[pos] ^= 0xff;
    if (pos == 0)
      value1.index ^= 0xff;
    else
      value1.data_raw[pos - 1] ^= 0xff;
    memcpy(&before1, &value1, sizeof(before1));
    assert(po35_table_1c7f_encode(&value1, output1, sizeof(output1)) == PO_OK);
    assert(memcmp(output1, expected1, sizeof(expected1)) == 0);
    assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
  }
}

static void test_indexed_table_validation(void) {
  const uint8_t input[PO_MAX_PAYLOAD] = {0};
  po35_table_7be7_t value7, before7;
  po35_table_1c7f_t value1, before1;
  uint8_t output[PO35_TABLE_1C7F_BYTES + 3], before[sizeof(output)];
  memset(&value7, 0xa5, sizeof(value7));
  memset(&value1, 0xa5, sizeof(value1));
  memcpy(&before7, &value7, sizeof(before7));
  memcpy(&before1, &value1, sizeof(before1));
  memset(output, 0x5a, sizeof(output));
  memcpy(before, output, sizeof(before));
  for (size_t len = 0; len <= sizeof(input); ++len) {
    if (len != PO35_TABLE_7BE7_BYTES) {
      assert(po35_table_7be7_decode(input, len, &value7) == PO_ERR_PARSE);
      assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
    }
    if (len != PO35_TABLE_1C7F_BYTES) {
      assert(po35_table_1c7f_decode(input, len, &value1) == PO_ERR_PARSE);
      assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
    }
  }
  assert(po35_table_7be7_decode(input, SIZE_MAX, &value7) == PO_ERR_PARSE);
  assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
  assert(po35_table_1c7f_decode(input, SIZE_MAX, &value1) == PO_ERR_PARSE);
  assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
  for (size_t capacity = 0; capacity < PO35_TABLE_7BE7_BYTES; ++capacity) {
    assert(po35_table_7be7_encode(&value7, output, capacity) == PO_ERR_BUFFER_TOO_SMALL);
    assert(memcmp(output, before, sizeof(output)) == 0);
    assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
  }
  for (size_t capacity = 0; capacity < PO35_TABLE_1C7F_BYTES; ++capacity) {
    assert(po35_table_1c7f_encode(&value1, output, capacity) == PO_ERR_BUFFER_TOO_SMALL);
    assert(memcmp(output, before, sizeof(output)) == 0);
    assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
  }
  assert(po35_table_7be7_decode(NULL, PO35_TABLE_7BE7_BYTES, &value7) == PO_ERR_INVALID_ARG);
  assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
  assert(po35_table_7be7_decode(NULL, 0, &value7) == PO_ERR_INVALID_ARG);
  assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
  assert(po35_table_7be7_decode(input, PO35_TABLE_7BE7_BYTES, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_table_7be7_decode(input, 0, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_table_7be7_decode(NULL, 0, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_table_1c7f_decode(NULL, PO35_TABLE_1C7F_BYTES, &value1) == PO_ERR_INVALID_ARG);
  assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
  assert(po35_table_1c7f_decode(NULL, 0, &value1) == PO_ERR_INVALID_ARG);
  assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
  assert(po35_table_1c7f_decode(input, PO35_TABLE_1C7F_BYTES, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_table_1c7f_decode(input, 0, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_table_1c7f_decode(NULL, 0, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_table_7be7_encode(NULL, output, sizeof(output)) == PO_ERR_INVALID_ARG);
  assert(memcmp(output, before, sizeof(output)) == 0);
  assert(po35_table_7be7_encode(NULL, output, 0) == PO_ERR_INVALID_ARG);
  assert(memcmp(output, before, sizeof(output)) == 0);
  assert(po35_table_7be7_encode(&value7, NULL, PO35_TABLE_7BE7_BYTES) == PO_ERR_INVALID_ARG);
  assert(po35_table_7be7_encode(&value7, NULL, 0) == PO_ERR_INVALID_ARG);
  assert(po35_table_7be7_encode(NULL, NULL, 0) == PO_ERR_INVALID_ARG);
  assert(memcmp(&value7, &before7, sizeof(value7)) == 0);
  assert(po35_table_1c7f_encode(NULL, output, sizeof(output)) == PO_ERR_INVALID_ARG);
  assert(memcmp(output, before, sizeof(output)) == 0);
  assert(po35_table_1c7f_encode(NULL, output, 0) == PO_ERR_INVALID_ARG);
  assert(memcmp(output, before, sizeof(output)) == 0);
  assert(po35_table_1c7f_encode(&value1, NULL, PO35_TABLE_1C7F_BYTES) == PO_ERR_INVALID_ARG);
  assert(po35_table_1c7f_encode(&value1, NULL, 0) == PO_ERR_INVALID_ARG);
  assert(po35_table_1c7f_encode(NULL, NULL, 0) == PO_ERR_INVALID_ARG);
  assert(memcmp(&value1, &before1, sizeof(value1)) == 0);
}

static void test_structure_codecs(void) {
  uint8_t input[255], output[255];
  po35_settings_t settings;
  po35_pattern_t pattern;
  size_t length = 0;
  for (size_t i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(i * 71);
  input[155] = 99;
  assert(po35_settings_decode(input, sizeof(input), &settings) == PO_OK);
  assert(settings.chain_count == 99 && settings.tempo == input[152]);
  assert(settings.drums[15].pitch == input[150] && settings.drums[15].morph == input[151]);
  assert(po35_settings_encode(&settings, output, sizeof(output), &length) == PO_OK);
  assert(length == 255 && memcmp(input, output, length) == 0);
  assert(po35_settings_decode(input, 254, &settings) == PO_ERR_PARSE);
  settings.chain_count = 100;
  assert(po35_settings_encode(&settings, output, sizeof(output), &length) == PO_ERR_RANGE);
  settings.chain_count = 32;
  assert(po35_settings_encode(&settings, output, 187, &length) == PO_ERR_BUFFER_TOO_SMALL);
  assert(po35_settings_encode(&settings, output, 188, &length) == PO_OK && length == 188);
  input[155] = 0;
  assert(po35_settings_decode(input, 156, &settings) == PO_OK && settings.chain_count == 0);
  assert(po35_pattern_decode(input, 183, &pattern) == PO_OK);
  assert(pattern.pattern_index == input[0]);
  assert(pattern.drums[15].morph == input[144]);
  assert(pattern.derived_mask == (uint16_t)(input[181] | (input[182] << 8)));
  assert(po35_pattern_encode(&pattern, output, sizeof(output)) == PO_OK);
  assert(memcmp(input, output, 183) == 0);
  assert(po35_pattern_decode(input, 182, &pattern) == PO_ERR_PARSE);
  assert(po35_pattern_encode(&pattern, output, 182) == PO_ERR_BUFFER_TOO_SMALL);
}

int main(void) {
  test_frames();
  test_builder();
  test_speech_codecs();
  test_speech_edit_api_surface();
  test_speech_edit_plan_measurement();
  test_speech_edit_plan_descriptor_failures();
  test_speech_edit_plan_group_failures();
  test_speech_edit_rebuild();
  test_indexed_table_golden_records();
  test_indexed_table_byte_values();
  test_indexed_table_edit_locality();
  test_indexed_table_validation();
  test_structure_codecs();
  puts("PO-35 framing and typed-codec tests passed");
  return 0;
}
