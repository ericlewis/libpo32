#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po32.h"
#include "po33.h"
#include "po35.h"
#include "../src/po_wire_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_po33_record_errors(void) {
  uint8_t bytes[PO33_PATTERN_BYTES] = {0}, out[PO33_PATTERN_BYTES];
  po33_settings_t settings = {0};
  po33_pattern_t pattern = {0};
  po33_sound_header_t header = {0};
  po_status_t s;
  memset(out, 0xa5, sizeof(out));
  s = po33_settings_decode(NULL, PO33_SETTINGS_BYTES, &settings);
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_settings_decode(bytes, PO33_SETTINGS_BYTES, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_settings_decode(bytes, PO33_SETTINGS_BYTES - 1, &settings);
  assert(s == PO_ERR_FRAME);
  s = po33_settings_encode(NULL, out, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_settings_encode(&settings, NULL, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_settings_encode(&settings, out, PO33_SETTINGS_BYTES - 1);
  assert(s == PO_ERR_BUFFER_TOO_SMALL);
  s = po33_pattern_decode(bytes, sizeof(bytes), NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_pattern_encode(NULL, out, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_pattern_encode(&pattern, NULL, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_sound_header_decode(NULL, 9, &header);
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_sound_header_decode(bytes, 9, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_sound_header_decode(bytes, 8, &header);
  assert(s == PO_ERR_FRAME);
  s = po33_sound_header_encode(NULL, out, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_sound_header_encode(&header, NULL, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_sound_header_encode(&header, out, 8);
  assert(s == PO_ERR_BUFFER_TOO_SMALL);
  for (size_t i = 0; i < sizeof(out); ++i)
    assert(out[i] == 0xa5);
}

static void test_po35_record_errors(void) {
  uint8_t bytes[256] = {0}, out[256];
  po35_speech_header_t header = {0};
  po35_speech_record_t record = {0};
  po35_settings_t settings = {0};
  po35_pattern_t pattern = {0};
  po_status_t s;
  size_t n = 99;
  memset(out, 0xa5, sizeof(out));
  s = po35_speech_header_decode(NULL, 9, &header);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_header_decode(bytes, 9, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_header_decode(bytes, 8, &header);
  assert(s == PO_ERR_PARSE);
  s = po35_speech_header_encode(NULL, out, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_header_encode(&header, NULL, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_header_encode(&header, out, 8);
  assert(s == PO_ERR_BUFFER_TOO_SMALL);
  s = po35_speech_record_decode(NULL, 44, &record);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_record_decode(bytes, 44, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_record_decode(bytes, 43, &record);
  assert(s == PO_ERR_PARSE);
  s = po35_speech_record_encode(NULL, out, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_record_encode(&record, NULL, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_speech_record_encode(&record, out, 43);
  assert(s == PO_ERR_BUFFER_TOO_SMALL);
  s = po35_settings_decode(NULL, 156, &settings);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_settings_decode(bytes, 156, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_settings_decode(bytes, 155, &settings);
  assert(s == PO_ERR_PARSE);
  s = po35_settings_decode(bytes, 256, &settings);
  assert(s == PO_ERR_PARSE);
  s = po35_settings_decode(bytes, 157, &settings);
  assert(s == PO_ERR_PARSE);
  s = po35_settings_encode(NULL, out, sizeof(out), &n);
  assert(s == PO_ERR_INVALID_ARG && n == 99);
  s = po35_settings_encode(&settings, NULL, sizeof(out), &n);
  assert(s == PO_ERR_INVALID_ARG && n == 99);
  s = po35_settings_encode(&settings, out, 155, &n);
  assert(s == PO_ERR_BUFFER_TOO_SMALL && n == 99);
  s = po35_pattern_decode(NULL, 183, &pattern);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_pattern_decode(bytes, 183, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_pattern_decode(bytes, 182, &pattern);
  assert(s == PO_ERR_PARSE);
  s = po35_pattern_encode(NULL, out, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_pattern_encode(&pattern, NULL, sizeof(out));
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_pattern_encode(&pattern, out, 182);
  assert(s == PO_ERR_BUFFER_TOO_SMALL);
  for (size_t i = 0; i < sizeof(out); ++i)
    assert(out[i] == 0xa5);
  s = po35_settings_encode(&settings, out, sizeof(out), NULL);
  assert(s == PO_OK);
}

static void test_builders_and_parsers(void) {
  uint8_t frame[8192] = {0}, phases[6] = {0};
  po33_builder_t a = {0};
  po35_builder_t b = {0};
  po33_decode_result_t ar;
  po35_decode_result_t br;
  size_t n = 0;
  po_status_t s;
  s = po33_frame_parse(NULL, 0, NULL, NULL, &ar);
  assert(s == PO_ERR_INVALID_ARG && !ar.done);
  s = po35_frame_parse(NULL, 0, NULL, NULL, &br);
  assert(s == PO_ERR_INVALID_ARG && !br.done);
  s = po35_frame_parse(frame, 0, NULL, NULL, NULL);
  assert(s == PO_ERR_FRAME);
  s = po33_pack_symbols(phases, SIZE_MAX, 0, frame, sizeof(frame), &n);
  assert(s == PO_ERR_RANGE);
  s = po33_pack_symbols(phases, 3, 0, NULL, sizeof(frame), &n);
  assert(s == PO_ERR_INVALID_ARG && n == 1);
  s = po33_builder_append_packet(NULL, 0x1234, NULL, 0, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po33_builder_finish(&a, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_builder_append_packet(&b, 0x1234, NULL, 0, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_builder_finish(NULL, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_builder_finish(&b, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po35_builder_init(&b, frame, sizeof(frame));
  assert(s == PO_OK);
  s = po35_builder_append_packet(&b, 0x1234, frame, 256, NULL);
  assert(s == PO_ERR_RANGE);
  s = po35_builder_finish(&b, NULL);
  assert(s == PO_OK);
  s = po35_frame_parse(frame, b.length, NULL, NULL, NULL);
  assert(s == PO_OK);
  s = po35_builder_finish(&b, &n);
  assert(s == PO_OK && n == b.length);
  s = po33_builder_init(&a, frame, sizeof(frame));
  assert(s == PO_OK);
  s = po33_builder_finish(&a, NULL);
  assert(s == PO_OK);
  s = po33_frame_parse(frame, a.length, NULL, NULL, NULL);
  assert(s == PO_OK);
  /* Public builder entry points validate the caller-owned state's span before
   * accessing it, even when a caller supplies inconsistent lengths. */
  a.length = a.capacity + 1;
  s = po33_builder_finish(&a, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  a.length = 1;
  s = po33_builder_finish(&a, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  b.length = b.capacity + 1;
  s = po35_builder_finish(&b, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  b.length = 1;
  s = po35_builder_finish(&b, NULL);
  assert(s == PO_ERR_INVALID_ARG);
}

static void test_checked_wire_helpers(void) {
  uint8_t bytes[32] = {0}, counter[4096];
  uint16_t state = PO_INITIAL_STATE, tag = 99;
  size_t used = 99, length = 99, pos = 0;
  po_packet_t packet = {0};
  po_status_t s;
  assert(po32_crc_mix_state(0x1d0f, 0xa5) == po_wire_crc_mix_state(0x1d0f, 0xa5));
  s = po_wire_packet_header(NULL, 3, state, &tag, &length);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_packet_header(bytes, 3, state, NULL, &length);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_packet_header(bytes, 3, state, &tag, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_packet_header(bytes, 2, state, &tag, &length);
  assert(s == PO_ERR_INVALID_ARG);
  assert(tag == 99 && length == 99);
  s = po_wire_read_packet(NULL, 5, &state, &used, &packet);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_read_packet(bytes, 5, NULL, &used, &packet);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_read_packet(bytes, 5, &state, NULL, &packet);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_read_packet(bytes, 5, &state, &used, NULL);
  assert(s == PO_ERR_INVALID_ARG);
  s = po_wire_read_packet(bytes, 4, &state, &used, &packet);
  assert(s == PO_ERR_FRAME);
  assert(state == PO_INITIAL_STATE && used == 99);
  assert(!po_wire_counter_matches(NULL, 4096));
  assert(!po_wire_counter_matches(counter, 0));
  po_wire_counter_write(counter);
  counter[3] ^= 1;
  assert(!po_wire_counter_matches(counter, sizeof(counter)));
  po_wire_counter_write(counter);
  counter[4095] ^= 1;
  assert(!po_wire_counter_matches(counter, sizeof(counter)));
  po_wire_write_packet(bytes, &pos, &state, 0x1234, NULL, 0);
  state = PO_INITIAL_STATE;
  s = po_wire_read_packet(bytes, pos, &state, &used, &packet);
  assert(s == PO_OK && used == 5 && packet.tag_code == 0x1234);
}

int main(void) {
  test_po33_record_errors();
  test_po35_record_errors();
  test_builders_and_parsers();
  test_checked_wire_helpers();
  puts("Family contract tests passed");
  return 0;
}
