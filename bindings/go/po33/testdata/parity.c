/* Standalone C consumer used to check Go marshalling against the public API. */
#include "po33.h"
#include <stdio.h>

static int emit(const uint8_t *data, size_t count) {
  return fwrite(data, 1, count, stdout) == count;
}

static int emit_word(uint64_t value) {
  uint8_t bytes[8];
  for (unsigned i = 0; i < 8; ++i)
    bytes[i] = (uint8_t)(value >> (8 * i));
  return emit(bytes, sizeof(bytes));
}

static int emit_packet(const po_packet_t *packet, void *user) {
  (void)user;
  return !(emit_word(packet->tag_code) && emit_word(packet->offset) &&
           emit_word(packet->payload_len) && emit(packet->payload, packet->payload_len) &&
           emit_word(packet->trailer.lo) && emit_word(packet->trailer.hi) &&
           emit_word(packet->trailer.state) && emit_word(packet->trailer.matches_state));
}

static int emit_result(const po33_decode_result_t *result) {
  return emit_word(result->packet_count) && emit_word(result->frame_bytes) &&
         emit_word(result->done) && emit_word(result->final_state) && emit_word(result->stopped);
}

int main(void) {
  uint8_t buffer[8192], data[PO33_PATTERN_BYTES];
  po33_settings_t settings;
  po33_pattern_t pattern;
  po33_sound_header_t header;
  size_t written;
  for (size_t i = 0; i < PO33_SETTINGS_BYTES; ++i)
    data[i] = (uint8_t)(i * 17 + 11);
  data[383] = 2;
  if (po33_settings_decode(data, PO33_SETTINGS_BYTES, &settings) != PO_OK)
    return 1;
  settings.tempo = 119;
  settings.chain[127] = 42;
  if (po33_settings_encode(&settings, buffer, sizeof(buffer)) != PO_OK ||
      !emit(buffer, PO33_SETTINGS_BYTES))
    return 2;
  for (size_t i = 0; i < PO33_PATTERN_BYTES; ++i)
    data[i] = (uint8_t)(i * 29 + i / 256);
  if (po33_pattern_decode(data, PO33_PATTERN_BYTES, &pattern) != PO_OK)
    return 3;
  pattern.events[15][1].control_raw ^= 15;
  pattern.events[7][5].event_raw[2] = 0xab;
  if (po33_pattern_encode(&pattern, buffer, sizeof(buffer)) != PO_OK ||
      !emit(buffer, PO33_PATTERN_BYTES))
    return 4;
  header.range_start_raw = 0x12345678;
  header.range_end_raw = 0xffffffff;
  header.source_selector = 120;
  if (po33_sound_header_encode(&header, buffer, sizeof(buffer)) != PO_OK ||
      !emit(buffer, PO33_SOUND_HEADER_BYTES))
    return 5;
  float pcm[257];
  for (size_t i = 0; i < 257; ++i)
    pcm[i] = (float)((int)(i % 29) - 14) / 16.0f;
  const uint32_t rates[] = {23438, 44100, 8000};
  for (size_t i = 0; i < 3; ++i)
    if (po33_samples_encode_f32(pcm, 257, rates[i], buffer, sizeof(buffer), &written) != PO_OK ||
        !emit(buffer, written))
      return 6;
  const uint8_t phases[] = {0, 0, 0, 1, 0, 3, 0, 2, 1, 2, 2, 3, 3, 1};
  for (unsigned parity = 0; parity < 2; ++parity)
    if (po33_pack_symbols(phases, 7, parity, buffer, sizeof(buffer), &written) != PO_OK ||
        !emit(buffer, written))
      return 7;
  po33_builder_t builder;
  for (size_t i = 0; i < 255; ++i)
    data[i] = (uint8_t)(i * 17);
  uint8_t selector = 7;
  if (po33_builder_init(&builder, buffer, sizeof(buffer)) != PO_OK ||
      po33_builder_append_packet(&builder, 0x1234, data, 255, NULL) != PO_OK ||
      po33_builder_append_packet(&builder, PO33_TAG_SOUND_SELECT, &selector, 1, NULL) != PO_OK ||
      po33_builder_finish(&builder, &written) != PO_OK || !emit(buffer, written))
    return 8;
  po33_decode_result_t result;
  if (po33_frame_parse(buffer, written, emit_packet, NULL, &result) != PO_OK || !result.done ||
      !emit_result(&result))
    return 9;
  if (po33_frame_parse(buffer, written - 1, NULL, NULL, &result) != PO_ERR_FRAME ||
      !emit_result(&result))
    return 10;
  return 0;
}
