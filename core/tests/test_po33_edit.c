#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "po32.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint8_t rendered_frame[8192];
static size_t rendered_length, render_calls;
static uint32_t rendered_rate;
#define po33_write_transfer_wav test_write_transfer_wav
#define main                    po33_encode_main
#include "../examples/po33_encode_capture.c"
#undef main
#undef po33_write_transfer_wav

int test_write_transfer_wav(const char *path, const uint8_t *frame, size_t length, uint32_t rate) {
  FILE *fp;
  size_t written;
  if (path == NULL || frame == NULL)
    return 0;
  ++render_calls;
  assert(length <= sizeof(rendered_frame));
  memcpy(rendered_frame, frame, length);
  rendered_length = length;
  rendered_rate = rate;
  fp = fopen(path, "wbx");
  if (!fp)
    return 0;
  written = fwrite(frame, 1, length, fp);
  assert(written == length);
  return fclose(fp) == 0;
}

static uint8_t fixture[8192];
static size_t fixture_length;
static uint16_t fixture_state;
static void emit_byte(uint8_t raw) {
  fixture[fixture_length++] = po32_whiten_byte(fixture_state, raw);
  fixture_state = po32_crc_update(fixture_state, raw);
}
static void emit_packet(uint16_t tag, const uint8_t *payload, size_t n) {
  uint16_t trailer;
  emit_byte((uint8_t)tag);
  emit_byte((uint8_t)(tag >> 8));
  emit_byte((uint8_t)n);
  for (size_t i = 0; i < n; ++i)
    emit_byte(payload[i]);
  trailer = fixture_state;
  emit_byte((uint8_t)trailer);
  emit_byte((uint8_t)(trailer >> 8));
}
static const uint8_t sound7[] = {3, 0, 0, 0, 0xa0, 0x86, 1, 0, 42, 0, 255, 1, 254, 127, 128};
static const uint8_t sound8[] = {2, 0, 0, 0, 90, 0, 0, 0, 7, 255, 2};
static const uint8_t pattern2[] = {10, 20, 30, 40, 50, 60};
static void make_fixture(void) {
  const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d}, unknown[] = {0, 255, 129, 42};
  uint8_t selector = 2;
  for (size_t i = 0; i < PO33_PREAMBLE_BYTES - 4; ++i)
    fixture[i] = (uint8_t)i;
  memcpy(fixture + PO33_PREAMBLE_BYTES - 4, sync, 4);
  fixture_length = PO33_PREAMBLE_BYTES;
  fixture_state = PO32_INITIAL_STATE;
  emit_packet(PO33_TAG_BEGIN, NULL, 0);
  emit_packet(PO33_TAG_SETTINGS, unknown, sizeof(unknown));
  emit_packet(0x1234, unknown, sizeof(unknown));
  emit_packet(PO33_TAG_PATTERN_SELECT, &selector, 1);
  emit_packet(PO33_TAG_PATTERN_DATA, pattern2, 2);
  emit_packet(0xabcd, unknown, 1);
  emit_packet(PO33_TAG_PATTERN_DATA, pattern2 + 2, 4);
  selector = 3;
  emit_packet(PO33_TAG_PATTERN_SELECT, &selector, 1);
  emit_packet(PO33_TAG_PATTERN_DATA, unknown, 2);
  selector = 7;
  emit_packet(PO33_TAG_SOUND_SELECT, &selector, 1);
  emit_packet(PO33_TAG_SOUND_DATA, sound7, 5);
  emit_packet(0x4321, unknown, sizeof(unknown));
  emit_packet(PO33_TAG_SOUND_DATA, sound7 + 5, sizeof(sound7) - 5);
  selector = 8;
  emit_packet(PO33_TAG_SOUND_SELECT, &selector, 1);
  emit_packet(PO33_TAG_SOUND_DATA, sound8, sizeof(sound8));
  selector = 9;
  emit_packet(PO33_TAG_SOUND_SELECT, &selector, 1);
  emit_packet(PO33_TAG_SOUND_DATA, sound7, 9);
  emit_packet(PO33_TAG_END, NULL, 0);
}
static void save_file(const char *path, const uint8_t *bytes, size_t n) {
  FILE *fp = fopen(path, "wb");
  size_t written;
  int status;
  assert(fp);
  written = fwrite(bytes, 1, n, fp);
  assert(written == n);
  status = fclose(fp);
  assert(status == 0);
}
static void make_path(char *path, size_t capacity, const char *directory, const char *name) {
  int length = snprintf(path, capacity, "%s/%s", directory, name);
  assert(length > 0 && (size_t)length < capacity);
}
static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = (uint8_t)(v >> (8 * i));
}
static void save_wav(const char *path, const int16_t *pcm, size_t count, uint32_t rate) {
  uint8_t b[128] = {0};
  assert(count * 2 + 44 <= sizeof(b));
  memcpy(b, "RIFF", 4);
  put32(b + 4, (uint32_t)(36 + count * 2));
  memcpy(b + 8, "WAVEfmt ", 8);
  put32(b + 16, 16);
  put16(b + 20, 1);
  put16(b + 22, 1);
  put32(b + 24, rate);
  put32(b + 28, rate * 2);
  put16(b + 32, 2);
  put16(b + 34, 16);
  memcpy(b + 36, "data", 4);
  put32(b + 40, (uint32_t)(count * 2));
  for (size_t i = 0; i < count; i++)
    put16(b + 44 + 2 * i, (uint16_t)pcm[i]);
  save_file(path, b, 44 + count * 2);
}
static po32_packet_t original[64], edited[64];
static size_t original_count, edited_count;
static int collect_original(const po32_packet_t *p, void *user) {
  (void)user;
  assert(original_count < 64);
  original[original_count++] = *p;
  return 0;
}
static int collect_edited(const po32_packet_t *p, void *user) {
  (void)user;
  assert(edited_count < 64);
  edited[edited_count++] = *p;
  return 0;
}
static void compare_preserved(int changed_pattern, int changed_sound) {
  po33_decode_result_t result;
  int pattern = -1, sound = -1;
  size_t pattern_offset = 0, sound_offset = 0;
  edited_count = 0;
  po_status_t status =
      po33_frame_parse(rendered_frame, rendered_length, collect_edited, NULL, &result);
  assert(status == PO_OK && result.done);
  assert(edited_count == original_count);
  for (size_t i = 0; i < original_count; i++) {
    po32_packet_t *a = original + i, *b = edited + i;
    assert(a->tag_code == b->tag_code && a->payload_len == b->payload_len &&
           a->offset == b->offset);
    if (a->tag_code == PO33_TAG_PATTERN_SELECT) {
      pattern = a->payload[0];
      pattern_offset = 0;
    }
    if (a->tag_code == PO33_TAG_SOUND_SELECT) {
      sound = a->payload[0];
      sound_offset = 0;
    }
    for (size_t j = 0; j < a->payload_len; j++) {
      int allowed = (changed_pattern && pattern == 2 && a->tag_code == PO33_TAG_PATTERN_DATA) ||
                    (changed_sound && sound == 7 && a->tag_code == PO33_TAG_SOUND_DATA &&
                     sound_offset + j >= 9);
      if (!allowed)
        assert(a->payload[j] == b->payload[j]);
    }
    if (a->tag_code == PO33_TAG_PATTERN_DATA)
      pattern_offset += a->payload_len;
    if (a->tag_code == PO33_TAG_SOUND_DATA)
      sound_offset += a->payload_len;
  }
  (void)pattern_offset;
}
#define CALL(a) po33_encode_main((int)(sizeof(a) / sizeof((a)[0])), (a))
int main(void) {
  char root[128], input[256], output[256], wav[256], wrong_rate[256], short_wav[256], pattern[256],
      short_pattern[256];
  int made = 0;
  int16_t pcm[6];
  size_t calls;
  for (unsigned i = 0; i < 100 && !made; i++) {
    snprintf(root, sizeof(root), "/tmp/libpo32-po33-edit-%ld-%u", (long)getpid(), i);
    made = mkdir(root, 0700) == 0;
    if (!made)
      assert(errno == EEXIST);
  }
  assert(made);
#define PATH(var, name) make_path(var, sizeof(var), root, name)
  PATH(input, "frame.bin");
  PATH(output, "output.wav");
  PATH(wav, "sample.wav");
  PATH(wrong_rate, "wrong-rate.wav");
  PATH(short_wav, "short.wav");
  PATH(pattern, "pattern.bin");
  PATH(short_pattern, "short-pattern.bin");
  make_fixture();
  save_file(input, fixture, fixture_length);
  po33_decode_result_t result;
  po_status_t status = po33_frame_parse(fixture, fixture_length, collect_original, NULL, &result);
  assert(status == PO_OK);
  for (size_t i = 0; i < 6; i++)
    pcm[i] = po33_sample_decode(sound7[9 + i]);
  save_wav(wav, pcm, 6, 23438);
  save_wav(wrong_rate, pcm, 6, 44100);
  save_wav(short_wav, pcm, 5, 23438);
  {
    uint8_t replacement[] = {6, 5, 4, 3, 2, 1};
    save_file(pattern, replacement, 6);
    save_file(short_pattern, replacement, 5);
  }
  {
    char *a[] = {"encode", input, output};
    int exit_status = CALL(a);
    assert(exit_status == 0);
    assert(rendered_rate == 48000 && rendered_length == fixture_length &&
           !memcmp(rendered_frame, fixture, fixture_length));
    exit_status = unlink(output);
    assert(exit_status == 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "7", wav, "--rate", "44100"};
    int exit_status = CALL(a);
    assert(exit_status == 0);
    assert(rendered_rate == 44100 && !memcmp(rendered_frame, fixture, fixture_length));
    exit_status = unlink(output);
    assert(exit_status == 0);
  }
  pcm[2] = 1000;
  save_wav(wav, pcm, 6, 23438);
  {
    char *a[] = {"encode", input,  output, "--sample",       "7", wav, "--pattern-byte",
                 "2",      "1",    "99",   "--pattern-byte", "2", "4", "0xab",
                 "--rate", "96000"};
    int exit_status = CALL(a);
    assert(exit_status == 0);
    assert(rendered_rate == 96000);
    compare_preserved(1, 1);
    assert(edited[4].payload[1] == 99 && edited[6].payload[2] == 0xab);
    assert(edited[12].payload[6] == po33_sample_encode(1000));
    assert(edited[12].payload[5] == 255);
    exit_status = unlink(output);
    assert(exit_status == 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern", "2", pattern};
    int exit_status = CALL(a);
    assert(exit_status == 0);
    compare_preserved(1, 0);
    assert(edited[4].payload[0] == 6 && edited[6].payload[3] == 1);
    exit_status = unlink(output);
    assert(exit_status == 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "7", wrong_rate};
    int exit_status = CALL(a);
    assert(exit_status == 0);
    compare_preserved(0, 1);
    exit_status = unlink(output);
    assert(exit_status == 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "7", short_wav};
    int exit_status = CALL(a);
    assert(exit_status == 0);
    compare_preserved(0, 1);
    assert(edited[12].payload[9] == 0);
    exit_status = unlink(output);
    assert(exit_status == 0);
  }
  calls = render_calls;
  {
    char *a[] = {"encode", input, output, "--pattern-byte", "2", "0", "1", "--pattern-byte",
                 "2",      "999", "2"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "7", wav, "--pattern", "2", short_pattern};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "9", wav};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "100", wav};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "7", wav, "--sample", "7", wav};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern", "2", short_pattern};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode",         input, output, "--pattern", "2", pattern,
                 "--pattern-byte", "2",   "0",    "1"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern-byte", "2", "0", "1", "--pattern-byte",
                 "2",      "0",   "2"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern-byte", "2", "6", "1"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern-byte", "2", "0", "256"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern-byte", "2", "-1", "1"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--pattern-byte", "255", "0", "1"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--rate", "23438"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--rate", "44100", "--rate", "48000"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--unknown"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  {
    char *a[] = {"encode", input, output, "--sample", "7"};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  fixture[fixture_length - 1] ^= 1;
  save_file(input, fixture, fixture_length);
  {
    char *a[] = {"encode", input, output};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  fixture[fixture_length - 1] ^= 1;
  fixture[fixture_length] = 0;
  save_file(input, fixture, fixture_length + 1);
  {
    char *a[] = {"encode", input, output};
    int exit_status = CALL(a);
    assert(exit_status != 0);
  }
  assert(render_calls == calls);
  {
    struct stat st;
    int stat_status = stat(output, &st);
    assert(stat_status != 0 && errno == ENOENT);
  }
  puts("PO-33 template edit tests passed");
  return 0;
}
