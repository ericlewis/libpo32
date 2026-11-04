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
#define main   po35_decode_main
#include "../examples/po35_decode_capture.c"
#undef main
#undef fwrite
#define main po35_encode_main
#include "../examples/po35_encode_capture.c"
#undef main
#include <assert.h>
#include <unistd.h>

static uint8_t test_frame[8192];
static size_t test_length;
static uint16_t test_state;
/* Independent bitwise CRC fixture writer, not the production frame builder. */
static void test_byte(uint8_t raw) {
  test_frame[test_length++] = (uint8_t)(raw + test_state);
  test_state ^= (uint16_t)((uint16_t)raw << 8);
  for (unsigned i = 0; i < 8; ++i)
    test_state = (uint16_t)((test_state << 1) ^ ((test_state & 0x8000u) ? 0x1021u : 0u));
}
static void test_packet(uint16_t tag, const uint8_t *data, size_t count) {
  uint16_t trailer;
  test_byte((uint8_t)tag);
  test_byte((uint8_t)(tag >> 8));
  test_byte((uint8_t)count);
  for (size_t i = 0; i < count; ++i)
    test_byte(data[i]);
  trailer = test_state;
  test_byte((uint8_t)trailer);
  test_byte((uint8_t)(trailer >> 8));
}
static void test_start_frame(void) {
  const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d};
  for (size_t i = 0; i < 4092; ++i)
    test_frame[i] = (uint8_t)i;
  memcpy(test_frame + 4092, sync, 4);
  test_length = 4096;
  test_state = 0x1d0f;
}
static void test_fixture(int malformed) {
  uint8_t header[9] = {2, 0, 1, 80, 140, 1, 2, 3, 4};
  uint8_t record[44] = {0, 0, 20, 0};
  uint8_t settings[188] = {0}, pattern[183] = {3};
  const uint8_t unknown[] = {0xde, 0xad, 0xbe, 0xef};
  test_start_frame();
  test_packet(0x1234, unknown, sizeof(unknown));
  test_packet(0xccb2, pattern, sizeof(pattern));
  for (unsigned slot = 0; slot < 2; ++slot) {
    header[0] = (uint8_t)(slot ? 5 : 2);
    if (malformed == 1)
      header[2] = 2;
    if (malformed == 2)
      header[0] = 2;
    if (malformed == 4)
      header[3] = 1;
    test_packet(0xdc32, header, sizeof(header));
    test_packet(0x73b0, record, sizeof(record));
    /* Unrecognized extension packets remain raw even inside a speech group. */
    if (slot == 0)
      test_packet(0x5678, unknown, sizeof(unknown));
    if (malformed != 3)
      test_packet(0x29a1, NULL, 0);
  }
  settings[152] = 120;
  settings[155] = 32;
  for (unsigned i = 0; i < 32; ++i)
    settings[156 + i] = (uint8_t)(i / 2);
  test_packet(0x84d8, settings, sizeof(settings));
  test_packet(0x71c3, NULL, 0);
}
static void test_path(char *out, size_t capacity, const char *root, const char *name) {
  int n = snprintf(out, capacity, "%s/%s", root, name);
  assert(n > 0 && (size_t)n < capacity);
}
static int test_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}
static void test_save(const char *path, const void *data, size_t size) {
  FILE *fp = fopen(path, "wb");
  size_t written;
  int closed;
  assert(fp);
  written = fwrite(data, 1, size, fp);
  closed = fclose(fp);
  assert(written == size && closed == 0);
}
static size_t test_load(const char *path, uint8_t *out, size_t capacity) {
  FILE *fp = fopen(path, "rb");
  size_t n;
  int closed, complete;
  assert(fp);
  n = fread(out, 1, capacity, fp);
  complete = !ferror(fp) && feof(fp);
  assert(complete);
  closed = fclose(fp);
  assert(closed == 0);
  return n;
}
static uint32_t test_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void test_speech_fixture(size_t first, size_t second) {
  uint8_t header[9] = {2, 0, 1, 80, 140, 1, 2, 3, 4};
  const uint8_t record[45] = {0, 0, 20, 0};
  test_start_frame();
  if (!first && !second)
    header[2] = 0;
  test_packet(0xdc32, header, sizeof(header));
  if (first)
    test_packet(0x73b0, record, first);
  if (second)
    test_packet(0x73b0, record + first, second);
  test_packet(0x29a1, NULL, 0);
  header[0] = 5;
  header[2] = 0;
  test_packet(0xdc32, header, sizeof(header));
  test_packet(0x29a1, NULL, 0);
  test_packet(0x71c3, NULL, 0);
}
static void test_split_speech_records(const char *root) {
  char output[1024], path[1024];
  uint8_t data[1024];
  size_t n;
  int ok;
  test_speech_fixture(13, 31);
  test_path(output, sizeof(output), root, "split-record");
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(ok);
  test_path(path, sizeof(path), output, "speech-02.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 44 && data[2] == 20);
  for (size_t second = 30; second <= 32; second += 2) {
    test_speech_fixture(13, second);
    test_path(output, sizeof(output), root, "invalid-split");
    ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                           PO35_SYNTH_BANK_HIGH);
    assert(!ok && !test_exists(output));
  }
}
static void test_empty_speech_slots(const char *root) {
  char output[1024], path[1024];
  uint8_t data[4096];
  size_t n;
  int ok;
  test_speech_fixture(44, 0);
  test_path(output, sizeof(output), root, "empty-slot");
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(ok);
  test_path(path, sizeof(path), output, "speech-05-header.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 9 && data[0] == 5 && data[1] == 0 && data[2] == 0 && data[8] == 4);
  test_path(path, sizeof(path), output, "speech-05.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 0);
  test_path(path, sizeof(path), output, "speech-05.wav");
  assert(!test_exists(path));
  test_path(path, sizeof(path), output, "all-speech.wav");
  n = test_load(path, data, sizeof(data));
  assert(n == 644 && test_u32(data + 40) == 600);
  test_path(path, sizeof(path), output, "summary.txt");
  n = test_load(path, data, sizeof(data) - 1);
  data[n] = 0;
  assert(strstr((char *)data, "speech_groups=2\n"));
  assert(strstr((char *)data, "speech_audio_files=1\n"));

  test_speech_fixture(0, 0);
  test_path(output, sizeof(output), root, "all-empty-slots");
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(ok);
  test_path(path, sizeof(path), output, "speech-02.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 0);
  test_path(path, sizeof(path), output, "speech-02.wav");
  assert(!test_exists(path));
  test_path(path, sizeof(path), output, "all-speech.wav");
  assert(!test_exists(path));
}
static void test_exports(const char *root) {
  char output[1024], path[1024];
  uint8_t data[20000];
  size_t n;
  int ok;
  test_fixture(0);
  test_path(output, sizeof(output), root, "export");
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(ok);
  test_path(path, sizeof(path), output, "frame.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == test_length && !memcmp(data, test_frame, test_length));
  test_path(path, sizeof(path), output, "speech-02-header.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 9 && data[0] == 2 && data[8] == 4);
  test_path(path, sizeof(path), output, "speech-02.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 44 && data[2] == 20);
  test_path(path, sizeof(path), output, "speech-02.wav");
  n = test_load(path, data, sizeof(data));
  assert(n == 644);
  assert(!memcmp(data, "RIFF", 4) && test_u32(data + 24) == 24000 && test_u32(data + 40) == 600);
  {
    int peak = 0;
    for (size_t i = 44; i < 644; i += 2) {
      unsigned raw = data[i] | ((unsigned)data[i + 1] << 8);
      int sample = raw < 32768 ? (int)raw : (int)raw - 65536;
      if (sample < 0)
        sample = -sample;
      if (sample > peak)
        peak = sample;
    }
    assert(peak >= 26213 && peak <= 26215);
  }
  test_path(path, sizeof(path), output, "all-speech.wav");
  n = test_load(path, data, sizeof(data));
  assert(n == 44 + 6600 * 2 && test_u32(data + 40) == 6600 * 2);
  for (size_t i = 644; i < 12644; ++i)
    assert(data[i] == 0);
  test_path(path, sizeof(path), output, "settings.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 188 && data[155] == 32);
  test_path(path, sizeof(path), output, "pattern-03.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 183 && data[0] == 3);
  test_path(path, sizeof(path), output, "payloads.bin");
  n = test_load(path, data, sizeof(data));
  assert(n == 485 && data[0] == 0xde);
  test_path(path, sizeof(path), output, "summary.txt");
  n = test_load(path, data, sizeof(data) - 1);
  data[n] = 0;
  assert(strstr((char *)data, "synth_model_version=1") &&
         strstr((char *)data, "channel_order=normal"));
  assert(strstr((char *)data, "speech_sample_rate=24000") && strstr((char *)data, "approximate=1"));
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(!ok);
  for (int malformed = 1; malformed <= 4; ++malformed) {
    test_fixture(malformed);
    test_path(output, sizeof(output), root, "malformed");
    ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                           PO35_SYNTH_BANK_HIGH);
    assert(!ok);
    assert(!test_exists(output));
  }
  test_fixture(0);
  test_frame[test_length - 1] ^= 1;
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(!ok);
  assert(!test_exists(output));
  test_frame[test_length - 1] ^= 1;
  test_fail_write = 1;
  ok = d35_write_outputs(output, test_frame, test_length, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(!ok);
  test_fail_write = 0;
  assert(!test_exists(output));
}
static void test_cli(const char *root) {
  char frame_path[1024], wav[1024], output[1024], path[1024], swapped[1024], mono[1024];
  uint8_t *audio, recovered[8192];
  size_t bytes, n;
  int result;
  test_fixture(0);
  test_path(frame_path, sizeof(frame_path), root, "input.bin");
  test_path(wav, sizeof(wav), root, "transfer.wav");
  test_path(output, sizeof(output), root, "decoded");
  test_path(swapped, sizeof(swapped), root, "swapped.wav");
  test_path(mono, sizeof(mono), root, "mono.wav");
  test_save(frame_path, test_frame, test_length);
  {
    char *encode[] = {"encode", frame_path, wav, "--rate", "48000"};
    char *decode[] = {"decode", wav, output, "--bank", "low"};
    result = po35_encode_main(5, encode);
    assert(result == 0);
    result = po35_encode_main(5, encode);
    assert(result != 0);
    result = po35_decode_main(5, decode);
    assert(result == 0);
    test_path(path, sizeof(path), output, "frame.bin");
    n = test_load(path, recovered, sizeof(recovered));
    assert(n == test_length && !memcmp(recovered, test_frame, test_length));
    test_path(path, sizeof(path), output, "speech-02.wav");
    n = test_load(path, recovered, sizeof(recovered));
    assert(n == 244 && test_u32(recovered + 24) == 8000);
    decode[3] = "--swap";
    test_path(output, sizeof(output), root, "wrong-order");
    result = po35_decode_main(4, decode);
    assert(result != 0 && !test_exists(output));
    encode[4] = "32000";
    result = po35_encode_main(5, encode);
    assert(result != 0);
  }
  for (unsigned i = 0; i < 2; ++i) {
    char rate_wav[1024], rate_output[1024];
    const char *rate = i ? "96000" : "44100";
    char *encode[] = {"encode", frame_path, rate_wav, "--rate", (char *)rate};
    char *decode[] = {"decode", rate_wav, rate_output};
    test_path(rate_wav, sizeof(rate_wav), root, i ? "rate96000.wav" : "rate44100.wav");
    test_path(rate_output, sizeof(rate_output), root, i ? "rate96000" : "rate44100");
    result = po35_encode_main(5, encode);
    assert(result == 0);
    result = po35_decode_main(3, decode);
    assert(result == 0);
    test_path(path, sizeof(path), rate_output, "frame.bin");
    n = test_load(path, recovered, sizeof(recovered));
    assert(n == test_length);
    assert(!memcmp(recovered, test_frame, test_length));
  }
  result = e35_read_file(wav, &audio, &bytes);
  assert(result);
  assert(bytes > 44 && audio[22] == 2);
  audio[22] = 1;
  test_save(mono, audio, bytes);
  audio[22] = 2;
  for (size_t i = 44; i + 3 < bytes; i += 4) {
    uint8_t a = audio[i], b = audio[i + 1];
    audio[i] = audio[i + 2];
    audio[i + 1] = audio[i + 3];
    audio[i + 2] = a;
    audio[i + 3] = b;
  }
  test_save(swapped, audio, bytes);
  free(audio);
  {
    char *decode[] = {"decode", swapped, output, "--swap"};
    test_path(output, sizeof(output), root, "swapped-auto");
    result = po35_decode_main(3, decode);
    assert(result == 0);
    test_path(path, sizeof(path), output, "summary.txt");
    bytes = test_load(path, recovered, sizeof(recovered) - 1);
    recovered[bytes] = 0;
    assert(strstr((char *)recovered, "channel_order=swapped"));
    test_path(output, sizeof(output), root, "swapped-forced");
    result = po35_decode_main(4, decode);
    assert(result == 0);
    test_path(output, sizeof(output), root, "mono-output");
    decode[1] = mono;
    result = po35_decode_main(3, decode);
    assert(result != 0 && !test_exists(output));
  }
  test_frame[test_length - 1] ^= 1;
  test_save(frame_path, test_frame, test_length);
  test_path(wav, sizeof(wav), root, "invalid.wav");
  {
    char *encode[] = {"encode", frame_path, wav};
    result = po35_encode_main(3, encode);
    assert(result != 0 && !test_exists(wav));
    test_fixture(0);
    test_save(frame_path, test_frame, test_length);
    test_path(wav, sizeof(wav), root, "missing/output.wav");
    result = po35_encode_main(3, encode);
    assert(result != 0);
  }
}
int main(void) {
  char root[128];
  int made = 0;
  for (unsigned i = 0; i < 100 && !made; ++i) {
    snprintf(root, sizeof(root), "/tmp/libpo35-examples-%ld-%u", (long)getpid(), i);
    made = mkdir(root, 0700) == 0;
    if (!made)
      assert(errno == EEXIST);
  }
  assert(made);
  test_split_speech_records(root);
  test_empty_speech_slots(root);
  test_exports(root);
  test_cli(root);
  puts("PO-35 hosted example tests passed");
  return 0;
}
