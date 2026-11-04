#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void test_pattern(void) {
  uint8_t wire[PO33_PATTERN_BYTES], copy[PO33_PATTERN_BYTES + 1];
  po33_pattern_t p;
  for (size_t i = 0; i < sizeof(wire); ++i)
    wire[i] = (uint8_t)(i * 29 + i / 256);
  assert(po33_pattern_decode(wire, sizeof(wire), &p) == PO32_OK);
  assert(p.events[15][1].event_raw[0] == wire[723] && p.events[15][1].control_raw == wire[1009]);
  assert(memcmp(p.footer_raw, wire + 1024, 8) == 0);
  memset(copy, 0xa5, sizeof(copy));
  assert(po33_pattern_encode(&p, copy, sizeof(copy)) == PO32_OK);
  assert(memcmp(wire, copy, sizeof(wire)) == 0 && copy[sizeof(wire)] == 0xa5);
  p.events[15][1].control_raw ^= 15;
  assert(po33_pattern_encode(&p, copy, sizeof(copy)) == PO32_OK);
  for (size_t i = 0; i < sizeof(wire); ++i)
    assert(copy[i] == (uint8_t)(wire[i] ^ (i == 1009 ? 15 : 0)));
  assert(po33_pattern_decode(wire, sizeof(wire) - 1, &p) == PO32_ERR_FRAME);
  assert(po33_pattern_decode(NULL, sizeof(wire), &p) == PO32_ERR_INVALID_ARG);
  memset(copy, 0xa5, sizeof(copy));
  assert(po33_pattern_encode(&p, copy, sizeof(wire) - 1) == PO32_ERR_BUFFER_TOO_SMALL);
  for (size_t i = 0; i < sizeof(copy); ++i)
    assert(copy[i] == 0xa5);
}
static void test_settings(void) {
  uint8_t wire[PO33_SETTINGS_BYTES], copy[PO33_SETTINGS_BYTES];
  po33_settings_t s, before;
  for (size_t i = 0; i < sizeof(wire); ++i)
    wire[i] = (uint8_t)i;
  wire[383] = 2;
  wire[384] = 140;
  wire[385] = 18;
  assert(po33_settings_decode(wire, sizeof(wire), &s) == PO32_OK);
  assert(s.chain_length == 2 && s.tempo == 140 && s.swing_raw == 18 && s.chain[127] == wire[382]);
  assert(s.state_raw[0] == wire[386] && s.suffix_raw[0] == wire[402]);
  assert(po33_settings_encode(&s, copy, sizeof(copy)) == PO32_OK &&
         memcmp(wire, copy, sizeof(wire)) == 0);
  before = s;
  wire[383] = 129;
  assert(po33_settings_decode(wire, sizeof(wire), &s) == PO32_ERR_RANGE &&
         memcmp(&s, &before, sizeof(s)) == 0);
  s.chain_length = 129;
  memset(copy, 0xa5, sizeof(copy));
  assert(po33_settings_encode(&s, copy, sizeof(copy)) == PO32_ERR_RANGE);
  for (size_t i = 0; i < sizeof(copy); ++i)
    assert(copy[i] == 0xa5);
}
static void test_sound(void) {
  const uint8_t wire[] = {0x78, 0x56, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff, 120};
  uint8_t copy[9];
  po33_sound_header_t h;
  assert(po33_sound_header_decode(wire, sizeof(wire), &h) == PO32_OK);
  assert(h.range_start_raw == 0x12345678u && h.range_end_raw == 0xffffffffu &&
         h.source_selector == 120);
  assert(po33_sound_header_encode(&h, copy, sizeof(copy)) == PO32_OK && memcmp(wire, copy, 9) == 0);
}
int main(void) {
  test_pattern();
  test_settings();
  test_sound();
  puts("PO-33 logical record tests passed");
  return 0;
}
