#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void assert_filled(const uint8_t *data, size_t len, uint8_t value) {
  for (size_t i = 0; i < len; ++i)
    assert(data[i] == value);
}
static void test_pattern_control_roundtrip(void) {
  for (unsigned int raw = 0; raw <= 255; ++raw) {
    po33_pattern_control_t control = {0xa5, 0xa5};
    uint8_t encoded = 0xa5;
    assert(po33_pattern_control_decode((uint8_t)raw, &control) == PO_OK);
    assert(control.note_raw == raw % 16 && control.timing_raw == raw / 16);
    assert(po33_pattern_control_encode(&control, &encoded) == PO_OK);
    assert(encoded == raw);
  }
}
static void test_pattern_control_errors(void) {
  po33_pattern_control_t control = {3, 12};
  uint8_t encoded = 0xa5;
  assert(po33_pattern_control_decode(0xc3, NULL) == PO_ERR_INVALID_ARG);
  assert(po33_pattern_control_encode(NULL, &encoded) == PO_ERR_INVALID_ARG);
  assert(encoded == 0xa5);
  assert(po33_pattern_control_encode(&control, NULL) == PO_ERR_INVALID_ARG);
  assert(po33_pattern_control_encode(NULL, NULL) == PO_ERR_INVALID_ARG);
  const uint8_t invalid[] = {16, 255};
  for (size_t i = 0; i < sizeof(invalid); ++i) {
    control.note_raw = invalid[i];
    assert(po33_pattern_control_encode(&control, &encoded) == PO_ERR_RANGE);
    assert(encoded == 0xa5);
    control.note_raw = 3;
    control.timing_raw = invalid[i];
    assert(po33_pattern_control_encode(&control, &encoded) == PO_ERR_RANGE);
    assert(encoded == 0xa5);
    assert(po33_pattern_control_encode(&control, NULL) == PO_ERR_INVALID_ARG);
    control.timing_raw = 12;
  }
}
static void test_pattern_fx_order_and_preservation(void) {
  const uint8_t wire[PO33_PATTERN_FX_BYTES] = {0x6f, 0x01, 0x23, 0x45, 0x78, 0x9a, 0xbc, 0xde};
  const po33_pattern_fx_t expected = {
      {PO33_FX_OCTAVE_UP, PO33_FX_BASELINE, 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14}};
  po33_pattern_fx_t fx;
  uint8_t encoded[PO33_PATTERN_FX_BYTES + 1];
  assert(po33_pattern_fx_decode(wire, sizeof(wire), &fx) == PO_OK);
  assert(memcmp(fx.code_raw, expected.code_raw, sizeof(fx.code_raw)) == 0);
  memset(encoded, 0xa5, sizeof(encoded));
  assert(po33_pattern_fx_encode(&expected, encoded, sizeof(encoded)) == PO_OK);
  assert(memcmp(encoded, wire, sizeof(wire)) == 0);
  assert(encoded[sizeof(wire)] == 0xa5);
  memset(encoded, 0xa5, sizeof(encoded));
  assert(po33_pattern_fx_encode(&fx, encoded, PO33_PATTERN_FX_BYTES) == PO_OK);
  assert(memcmp(encoded, wire, sizeof(wire)) == 0);
  assert(encoded[sizeof(wire)] == 0xa5);
}
static void test_pattern_fx_errors(void) {
  const uint8_t wire[PO33_PATTERN_FX_BYTES] = {0};
  const size_t invalid_lengths[] = {0, PO33_PATTERN_FX_BYTES - 1, PO33_PATTERN_FX_BYTES + 1,
                                    (size_t)-1};
  po33_pattern_fx_t fx;
  uint8_t encoded[PO33_PATTERN_FX_BYTES + 1];
  memset(&fx, 0xa5, sizeof(fx));
  assert(po33_pattern_fx_decode(NULL, sizeof(wire), &fx) == PO_ERR_INVALID_ARG);
  assert_filled(fx.code_raw, sizeof(fx.code_raw), 0xa5);
  assert(po33_pattern_fx_decode(wire, sizeof(wire), NULL) == PO_ERR_INVALID_ARG);
  assert(po33_pattern_fx_decode(NULL, 0, NULL) == PO_ERR_INVALID_ARG);
  for (size_t i = 0; i < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); ++i) {
    assert(po33_pattern_fx_decode(wire, invalid_lengths[i], &fx) == PO_ERR_FRAME);
    assert_filled(fx.code_raw, sizeof(fx.code_raw), 0xa5);
    assert(po33_pattern_fx_decode(NULL, invalid_lengths[i], &fx) == PO_ERR_INVALID_ARG);
    assert_filled(fx.code_raw, sizeof(fx.code_raw), 0xa5);
    assert(po33_pattern_fx_decode(wire, invalid_lengths[i], NULL) == PO_ERR_INVALID_ARG);
  }
  memset(encoded, 0xa5, sizeof(encoded));
  memset(&fx, 0, sizeof(fx));
  assert(po33_pattern_fx_encode(NULL, encoded, sizeof(encoded)) == PO_ERR_INVALID_ARG);
  assert_filled(encoded, sizeof(encoded), 0xa5);
  assert(po33_pattern_fx_encode(&fx, NULL, sizeof(encoded)) == PO_ERR_INVALID_ARG);
  assert(po33_pattern_fx_encode(NULL, NULL, 0) == PO_ERR_INVALID_ARG);
  const uint8_t invalid_codes[] = {16, 255};
  for (size_t step = 0; step < PO33_STEP_COUNT; ++step) {
    for (size_t i = 0; i < sizeof(invalid_codes); ++i) {
      fx.code_raw[step] = invalid_codes[i];
      assert(po33_pattern_fx_encode(&fx, encoded, sizeof(encoded)) == PO_ERR_RANGE);
      assert_filled(encoded, sizeof(encoded), 0xa5);
      assert(po33_pattern_fx_encode(&fx, encoded, PO33_PATTERN_FX_BYTES - 1) ==
             PO_ERR_BUFFER_TOO_SMALL);
      assert_filled(encoded, sizeof(encoded), 0xa5);
      assert(po33_pattern_fx_encode(&fx, NULL, sizeof(encoded)) == PO_ERR_INVALID_ARG);
    }
    fx.code_raw[step] = 0;
  }
  for (size_t capacity = 0; capacity < PO33_PATTERN_FX_BYTES; ++capacity) {
    assert(po33_pattern_fx_encode(&fx, encoded, capacity) == PO_ERR_BUFFER_TOO_SMALL);
    assert_filled(encoded, sizeof(encoded), 0xa5);
    assert(po33_pattern_fx_encode(NULL, encoded, capacity) == PO_ERR_INVALID_ARG);
    assert_filled(encoded, sizeof(encoded), 0xa5);
    assert(po33_pattern_fx_encode(&fx, NULL, capacity) == PO_ERR_INVALID_ARG);
  }
}
static void test_pattern_control_edit_locality(void) {
  uint8_t wire[PO33_PATTERN_BYTES], encoded[PO33_PATTERN_BYTES];
  for (size_t i = 0; i < sizeof(wire); ++i)
    wire[i] = (uint8_t)(i * 29 + i / 256);
  for (size_t step = 0; step < PO33_STEP_COUNT; ++step)
    for (size_t sound = 0; sound < PO33_SOUND_COUNT; ++sound)
      for (unsigned int field = 0; field < 2; ++field) {
        po33_pattern_t pattern;
        po33_pattern_control_t control;
        const size_t offset = 768 + step * PO33_SOUND_COUNT + sound;
        const uint8_t changed_bits = field == 0 ? 0x0f : 0xf0;
        assert(po33_pattern_decode(wire, sizeof(wire), &pattern) == PO_OK);
        assert(po33_pattern_control_decode(pattern.events[step][sound].control_raw, &control) ==
               PO_OK);
        if (field == 0)
          control.note_raw ^= 15;
        else
          control.timing_raw ^= 15;
        assert(po33_pattern_control_encode(&control, &pattern.events[step][sound].control_raw) ==
               PO_OK);
        assert(po33_pattern_encode(&pattern, encoded, sizeof(encoded)) == PO_OK);
        for (size_t i = 0; i < sizeof(wire); ++i)
          assert(encoded[i] == (uint8_t)(wire[i] ^ (i == offset ? changed_bits : 0)));
      }
}
static void test_pattern_fx_edit_locality(void) {
  uint8_t wire[PO33_PATTERN_BYTES], encoded[PO33_PATTERN_BYTES];
  for (size_t i = 0; i < sizeof(wire); ++i)
    wire[i] = (uint8_t)(i * 29 + i / 256);
  for (size_t step = 0; step < PO33_STEP_COUNT; ++step) {
    po33_pattern_t pattern;
    po33_pattern_fx_t fx;
    const size_t offset = 1024 + step / 2;
    const uint8_t changed_bits = step % 2 == 0 ? 0xf0 : 0x0f;
    assert(po33_pattern_decode(wire, sizeof(wire), &pattern) == PO_OK);
    assert(po33_pattern_fx_decode(pattern.footer_raw, sizeof(pattern.footer_raw), &fx) == PO_OK);
    fx.code_raw[step] ^= 15;
    assert(po33_pattern_fx_encode(&fx, pattern.footer_raw, sizeof(pattern.footer_raw)) == PO_OK);
    assert(po33_pattern_encode(&pattern, encoded, sizeof(encoded)) == PO_OK);
    for (size_t i = 0; i < sizeof(wire); ++i)
      assert(encoded[i] == (uint8_t)(wire[i] ^ (i == offset ? changed_bits : 0)));
  }
}
static void test_record_decode_overlong_lengths(void) {
  const uint8_t wire[PO33_PATTERN_BYTES + 1] = {0};
  const size_t settings_lengths[] = {PO33_SETTINGS_BYTES + 1, SIZE_MAX};
  const size_t pattern_lengths[] = {PO33_PATTERN_BYTES + 1, SIZE_MAX};
  const size_t sound_lengths[] = {PO33_SOUND_HEADER_BYTES + 1, SIZE_MAX};
  po33_settings_t settings;
  po33_pattern_t pattern;
  po33_sound_header_t sound;
  memset(&settings, 0xa5, sizeof(settings));
  memset(&pattern, 0xa5, sizeof(pattern));
  memset(&sound, 0xa5, sizeof(sound));
  for (size_t i = 0; i < sizeof(settings_lengths) / sizeof(settings_lengths[0]); ++i) {
    assert(po33_settings_decode(wire, settings_lengths[i], &settings) == PO_ERR_FRAME);
    assert_filled((const uint8_t *)&settings, sizeof(settings), 0xa5);
  }
  for (size_t i = 0; i < sizeof(pattern_lengths) / sizeof(pattern_lengths[0]); ++i) {
    assert(po33_pattern_decode(wire, pattern_lengths[i], &pattern) == PO_ERR_FRAME);
    assert_filled((const uint8_t *)&pattern, sizeof(pattern), 0xa5);
  }
  for (size_t i = 0; i < sizeof(sound_lengths) / sizeof(sound_lengths[0]); ++i) {
    assert(po33_sound_header_decode(wire, sound_lengths[i], &sound) == PO_ERR_FRAME);
    assert_filled((const uint8_t *)&sound, sizeof(sound), 0xa5);
  }
}
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
  uint8_t wire[PO33_SETTINGS_BYTES], copy[PO33_SETTINGS_BYTES + 3];
  po33_settings_t s, before;
  for (size_t i = 0; i < sizeof(wire); ++i)
    wire[i] = (uint8_t)i;
  wire[383] = 2;
  wire[384] = 140;
  wire[385] = 18;
  assert(po33_settings_decode(wire, sizeof(wire), &s) == PO32_OK);
  assert(s.chain_length == 2 && s.tempo == 140 && s.swing_raw == 18 && s.chain[127] == wire[382]);
  assert(s.state_raw[0] == wire[386] && s.suffix_raw[0] == wire[402]);
  memset(copy, 0xa5, sizeof(copy));
  assert(po33_settings_encode(&s, copy, sizeof(wire)) == PO32_OK &&
         memcmp(wire, copy, sizeof(wire)) == 0);
  assert_filled(copy + sizeof(wire), sizeof(copy) - sizeof(wire), 0xa5);
  const uint8_t valid_lengths[] = {0, PO33_CHAIN_CAPACITY};
  for (size_t i = 0; i < sizeof(valid_lengths); ++i) {
    wire[383] = valid_lengths[i];
    assert(po33_settings_decode(wire, sizeof(wire), &s) == PO_OK);
    assert(s.chain_length == valid_lengths[i]);
    memset(copy, 0xa5, sizeof(copy));
    assert(po33_settings_encode(&s, copy, sizeof(copy)) == PO_OK);
    assert(memcmp(wire, copy, sizeof(wire)) == 0);
    assert_filled(copy + sizeof(wire), sizeof(copy) - sizeof(wire), 0xa5);
  }
  before = s;
  const uint8_t invalid_lengths[] = {129, 255};
  for (size_t i = 0; i < sizeof(invalid_lengths); ++i) {
    s = before;
    wire[383] = invalid_lengths[i];
    assert(po33_settings_decode(wire, sizeof(wire), &s) == PO32_ERR_RANGE &&
           memcmp(&s, &before, sizeof(s)) == 0);
    s.chain_length = invalid_lengths[i];
    memset(copy, 0xa5, sizeof(copy));
    assert(po33_settings_encode(&s, copy, sizeof(copy)) == PO32_ERR_RANGE);
    assert_filled(copy, sizeof(copy), 0xa5);
  }
}
static void test_sound(void) {
  const uint8_t wire[] = {0x78, 0x56, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff, 120};
  uint8_t copy[PO33_SOUND_HEADER_BYTES + 3];
  po33_sound_header_t h;
  assert(po33_sound_header_decode(wire, sizeof(wire), &h) == PO32_OK);
  assert(h.range_start_raw == 0x12345678u && h.range_end_raw == 0xffffffffu &&
         h.source_selector == 120);
  memset(copy, 0xa5, sizeof(copy));
  assert(po33_sound_header_encode(&h, copy, sizeof(wire)) == PO32_OK && memcmp(wire, copy, 9) == 0);
  assert_filled(copy + sizeof(wire), sizeof(copy) - sizeof(wire), 0xa5);
  memset(copy, 0xa5, sizeof(copy));
  assert(po33_sound_header_encode(&h, copy, sizeof(copy)) == PO32_OK && memcmp(wire, copy, 9) == 0);
  assert_filled(copy + sizeof(wire), sizeof(copy) - sizeof(wire), 0xa5);
}
int main(void) {
  test_pattern_control_roundtrip();
  test_pattern_control_errors();
  test_pattern_fx_order_and_preservation();
  test_pattern_fx_errors();
  test_pattern_control_edit_locality();
  test_pattern_fx_edit_locality();
  test_record_decode_overlong_lengths();
  test_pattern();
  test_settings();
  test_sound();
  puts("PO-33 logical record tests passed");
  return 0;
}
