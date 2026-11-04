#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t expected[8192];
static size_t expected_length;
static uint16_t expected_state;

/* Independent, bitwise CCITT update; does not call library CRC or whitening. */
static void reference_byte(uint8_t raw) {
  expected[expected_length++] = (uint8_t)(raw + (uint8_t)expected_state);
  expected_state ^= (uint16_t)((uint16_t)raw << 8);
  for (unsigned i = 0; i < 8; ++i)
    expected_state =
        (uint16_t)((expected_state << 1) ^ ((expected_state & 0x8000u) ? 0x1021u : 0u));
}
static void reference_packet(uint16_t tag, const uint8_t *payload, size_t n) {
  uint16_t trailer;
  reference_byte((uint8_t)tag);
  reference_byte((uint8_t)(tag >> 8));
  reference_byte((uint8_t)n);
  for (size_t i = 0; i < n; ++i)
    reference_byte(payload[i]);
  trailer = expected_state;
  reference_byte((uint8_t)trailer);
  reference_byte((uint8_t)(trailer >> 8));
}
static void reference_begin(void) {
  const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d};
  for (size_t i = 0; i < PO33_PREAMBLE_BYTES - 4; ++i)
    expected[i] = (uint8_t)i;
  memcpy(expected + PO33_PREAMBLE_BYTES - 4, sync, sizeof(sync));
  expected_length = PO33_PREAMBLE_BYTES;
  expected_state = 0x1d0f;
  reference_packet(PO33_TAG_BEGIN, NULL, 0);
}
static void test_builder(void) {
  uint8_t output[8192], payload[255];
  po33_builder_t builder;
  po33_decode_result_t result;
  size_t offset = 0, length = 0;
  const uint8_t golden[] = {0x6a, 0x9a, 0x34, 0xe2, 0x48};
  for (size_t i = 0; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(i * 17);
  reference_begin();
  assert(po33_builder_init(&builder, output, sizeof(output)) == PO32_OK);
  assert(builder.length == expected_length && builder.state == expected_state);
  assert(memcmp(output + PO33_PREAMBLE_BYTES, golden, sizeof(golden)) == 0);
  assert(po33_builder_append_packet(&builder, 0x1234, payload, sizeof(payload), &offset) ==
         PO32_OK);
  assert(offset == expected_length);
  reference_packet(0x1234, payload, sizeof(payload));
  assert(po33_builder_append_packet(&builder, 0x5678, NULL, 0, NULL) == PO32_OK);
  reference_packet(0x5678, NULL, 0);
  assert(po33_builder_finish(&builder, &length) == PO32_OK);
  reference_packet(PO33_TAG_END, NULL, 0);
  assert(length == expected_length && builder.finished);
  assert(memcmp(output, expected, length) == 0);
  assert(po33_frame_parse(output, length, NULL, NULL, &result) == PO32_OK && result.done);
  assert(result.packet_count == 4 && result.final_state == builder.state);
  assert(po33_builder_finish(&builder, NULL) == PO32_OK && builder.length == length);
  assert(po33_builder_append_packet(&builder, 0x1234, NULL, 0, NULL) == PO32_ERR_FRAME);
}
static void test_failures_are_atomic(void) {
  uint8_t buffer[8192], before[8192], payload[256] = {0};
  po33_builder_t b;
  size_t n, offset = 77;
  uint16_t state;
  memset(buffer, 0xa5, sizeof(buffer));
  memcpy(before, buffer, sizeof(buffer));
  assert(po33_builder_init(NULL, buffer, sizeof(buffer)) == PO32_ERR_INVALID_ARG);
  assert(po33_builder_init(&b, NULL, sizeof(buffer)) == PO32_ERR_INVALID_ARG);
  assert(b.length == 0);
  assert(po33_builder_init(&b, buffer, PO33_PREAMBLE_BYTES + 4) == PO32_ERR_BUFFER_TOO_SMALL);
  assert(b.length == 0 && memcmp(buffer, before, sizeof(buffer)) == 0);
  assert(po33_builder_init(&b, buffer, sizeof(buffer)) == PO32_OK);
  n = b.length;
  state = b.state;
  memcpy(before, buffer, sizeof(buffer));
  assert(po33_builder_append_packet(&b, PO33_TAG_BEGIN, NULL, 0, NULL) == PO32_ERR_INVALID_ARG);
  assert(po33_builder_append_packet(&b, PO33_TAG_END, NULL, 0, NULL) == PO32_ERR_INVALID_ARG);
  assert(po33_builder_append_packet(&b, 0x1234, NULL, 1, NULL) == PO32_ERR_INVALID_ARG);
  assert(po33_builder_append_packet(&b, 0x1234, payload, 256, NULL) == PO32_ERR_RANGE);
  b.capacity = n + 4;
  assert(po33_builder_append_packet(&b, 0x1234, NULL, 0, &offset) == PO32_ERR_BUFFER_TOO_SMALL);
  assert(offset == 77 && b.length == n && b.state == state && !b.finished);
  assert(po33_builder_finish(&b, &offset) == PO32_ERR_BUFFER_TOO_SMALL);
  assert(offset == 77 && b.length == n && b.state == state && !b.finished);
  assert(memcmp(buffer, before, sizeof(buffer)) == 0);
  b.capacity = n + 5;
  assert(po33_builder_finish(&b, &offset) == PO32_OK && offset == n + 5);
  assert(po33_builder_finish(NULL, NULL) == PO32_ERR_INVALID_ARG);
}
static int distance(int a, int b) {
  return a > b ? a - b : b - a;
}
static void test_sample_quantizer(void) {
  int levels[256];
  for (unsigned code = 0; code < 256; ++code) {
    levels[code] = po33_sample_decode((uint8_t)code);
    assert(po33_sample_encode((int16_t)levels[code]) == (code == 255 ? 0 : code));
  }
  assert(po33_sample_encode(-32768) == 128 && po33_sample_encode(32767) == 127);
  assert(po33_sample_encode(4) == 0 && po33_sample_encode(-4) == 0);
  assert(po33_sample_encode(5) == 1 && po33_sample_encode(-5) == 254);
  for (int sample = -32768; sample <= 32767; ++sample) {
    uint8_t encoded = po33_sample_encode((int16_t)sample);
    int error = distance(sample, levels[encoded]);
    for (unsigned other = 0; other < 256; ++other)
      assert(error <= distance(sample, levels[other]));
  }
}
int main(void) {
  test_builder();
  test_failures_are_atomic();
  test_sample_quantizer();
  puts("PO-33 encoder tests passed");
  return 0;
}
