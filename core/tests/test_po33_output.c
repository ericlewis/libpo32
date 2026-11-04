#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <stdio.h>
static int test_fail_write;
static size_t test_fwrite(const void *data, size_t size, size_t count, FILE *fp) {
  return test_fail_write ? 0 : fwrite(data, size, count, fp);
}
#define fwrite test_fwrite
#define main   po33_capture_main
#include "../examples/po33_decode_capture.c"
#undef main
#undef fwrite
#include <assert.h>
#include "po32.h"
#include <math.h>
#include "wav_test_helpers.h"
#include <unistd.h>

static uint8_t test_frame[PO33_PREAMBLE_BYTES + 1024];
static size_t test_length;
static uint16_t test_state;
static void test_byte(uint8_t value) {
  test_frame[test_length++] = po32_whiten_byte(test_state, value);
  test_state = po32_crc_update(test_state, value);
}
static void test_packet(uint16_t tag, const uint8_t *data, size_t size) {
  uint16_t trailer;
  test_byte((uint8_t)tag);
  test_byte((uint8_t)(tag >> 8));
  test_byte((uint8_t)size);
  for (size_t i = 0; i < size; ++i)
    test_byte(data[i]);
  trailer = test_state;
  test_byte((uint8_t)trailer);
  test_byte((uint8_t)(trailer >> 8));
}
static void test_begin(void) {
  static const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d};
  for (size_t i = 0; i < PO33_PREAMBLE_BYTES - 4; ++i)
    test_frame[i] = (uint8_t)i;
  memcpy(test_frame + PO33_PREAMBLE_BYTES - 4, sync, sizeof(sync));
  test_length = PO33_PREAMBLE_BYTES;
  test_state = PO32_INITIAL_STATE;
  test_packet(PO33_TAG_BEGIN, NULL, 0);
}
static size_t test_read(const char *directory, const char *name, uint8_t *out, size_t capacity) {
  char path[1024];
  FILE *fp;
  size_t size;
  int status = snprintf(path, sizeof(path), "%s/%s", directory, name);
  assert(status > 0 && (size_t)status < sizeof(path));
  fp = fopen(path, "rb");
  assert(fp);
  size = fread(out, 1, capacity, fp);
  status = ferror(fp);
  assert(!status);
  status = feof(fp);
  assert(status);
  status = fclose(fp);
  assert(status == 0);
  return size;
}
static int test_exists(const char *directory, const char *name) {
  char path[1024];
  struct stat info;
  int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
  assert(length > 0 && (size_t)length < sizeof(path));
  return stat(path, &info) == 0;
}
int main(void) {
  char root[128], output[1024], bad[1024];
  uint8_t data[8192], selector = 2;
  const uint8_t settings[] = {11, 22, 33};
  const uint8_t pattern[] = {5, 4, 3, 2, 1};
  /* Deliberately implausible trim endpoints must not change the PCM export. */
  const uint8_t sound[] = {3, 0, 0, 0, 0xa0, 0x86, 1, 0, 42, 0, 1, 127, 128, 254, 255};
  const int16_t pcm[] = {0, 8, 32124, -32124, -8, 0};
  int status;
  size_t bytes;
  FILE *duplicate;
  {
    int made = 0;
    for (unsigned i = 0; i < 100 && !made; ++i) {
      snprintf(root, sizeof(root), "/tmp/libpo32-po33-output-%ld-%u", (long)getpid(), i);
      made = mkdir(root, 0700) == 0;
      if (!made)
        assert(errno == EEXIST);
    }
    assert(made);
  }
  status = snprintf(output, sizeof(output), "%s/decoded", root);
  assert(status > 0 && (size_t)status < sizeof(output));
  status = snprintf(bad, sizeof(bad), "%s/invalid", root);
  assert(status > 0 && (size_t)status < sizeof(bad));
  test_begin();
  test_packet(PO33_TAG_SETTINGS, settings, 1);
  test_packet(PO33_TAG_SETTINGS, settings + 1, 2);
  test_packet(PO33_TAG_PATTERN_SELECT, &selector, 1);
  test_packet(PO33_TAG_PATTERN_DATA, pattern, 2);
  test_packet(PO33_TAG_PATTERN_DATA, pattern + 2, 3);
  selector = 7;
  test_packet(PO33_TAG_SOUND_SELECT, &selector, 1);
  test_packet(PO33_TAG_SOUND_DATA, sound, 5);
  test_packet(PO33_TAG_SOUND_DATA, sound + 5, sizeof(sound) - 5);
  selector = 8;
  test_packet(PO33_TAG_SOUND_SELECT, &selector, 1);
  test_packet(PO33_TAG_SOUND_DATA, sound, 9);
  selector = 9;
  test_packet(PO33_TAG_SOUND_SELECT, &selector, 1);
  test_packet(0x1234, settings, sizeof(settings));
  test_packet(PO33_TAG_END, NULL, 0);
  test_frame[test_length - 1] ^= 1;
  status = write_outputs(bad, test_frame, test_length, 44100);
  assert(!status);
  {
    struct stat st;
    status = stat(bad, &st);
    assert(status != 0 && errno == ENOENT);
  }
  test_frame[test_length - 1] ^= 1;
  status = write_outputs(output, test_frame, test_length, 44100);
  assert(status);
  bytes = test_read(output, "settings.bin", data, sizeof(data));
  assert(bytes == sizeof(settings));
  assert(!memcmp(data, settings, sizeof(settings)));
  bytes = test_read(output, "pattern-02.bin", data, sizeof(data));
  assert(bytes == sizeof(pattern));
  assert(!memcmp(data, pattern, sizeof(pattern)));
  bytes = test_read(output, "sound-007.bin", data, sizeof(data));
  assert(bytes == sizeof(sound));
  assert(!memcmp(data, sound, sizeof(sound)));
  bytes = test_read(output, "sound-008.bin", data, sizeof(data));
  assert(bytes == 9);
  status = test_exists(output, "sound-008.wav");
  assert(!status);
  status = test_exists(output, "sound-009.bin");
  assert(!status);
  bytes = test_read(output, "sound-007.wav", data, sizeof(data));
  assert(bytes == 44 + sizeof(pcm));
  assert(!memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WAVE", 4));
  assert(test_wav_u32(data + 4) == 36 + sizeof(pcm) && test_wav_u32(data + 40) == sizeof(pcm));
  assert(test_wav_u16(data + 20) == 1 && test_wav_u16(data + 22) == 1);
  assert(test_wav_u32(data + 24) == 23438 && test_wav_u16(data + 34) == 16);
  for (size_t i = 0; i < sizeof(pcm) / sizeof(pcm[0]); ++i)
    assert(test_wav_u16(data + 44 + 2 * i) == (uint16_t)pcm[i]);
  bytes = test_read(output, "frame.bin", data, sizeof(data));
  assert(bytes == test_length);
  assert(!memcmp(data, test_frame, test_length));
  {
    size_t n = test_read(output, "summary.txt", data, sizeof(data) - 1);
    data[n] = 0;
    assert(strstr((char *)data, "sound_nominal_sample_rate=23437.5"));
    assert(strstr((char *)data, "sound_wav_sample_rate=23438"));
    assert(strstr((char *)data, "sound_trimmed=0"));
  }
  status = write_outputs(output, test_frame, test_length, 44100);
  assert(!status);
  bytes = test_read(output, "sound-007.bin", data, sizeof(data));
  assert(bytes == sizeof(sound));
  assert(!memcmp(data, sound, sizeof(sound)));
  duplicate = open_output(output, "sound-007.bin");
  assert(duplicate == NULL);
  status = snprintf(bad, sizeof(bad), "%s/write-failure", root);
  assert(status > 0 && (size_t)status < sizeof(bad));
  test_fail_write = 1;
  status = write_outputs(bad, test_frame, test_length, 44100);
  assert(!status);
  test_fail_write = 0;
  status = test_exists(bad, "summary.txt");
  assert(!status);
  puts("PO-33 semantic output tests passed");
  return 0;
}
