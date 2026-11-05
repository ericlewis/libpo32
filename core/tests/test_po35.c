#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po35.h"
#include "po32.h"
#include <assert.h>
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
  test_indexed_table_golden_records();
  test_indexed_table_byte_values();
  test_indexed_table_edit_locality();
  test_indexed_table_validation();
  test_structure_codecs();
  puts("PO-35 framing and typed-codec tests passed");
  return 0;
}
