#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include "po_capture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint8_t transmitted[131072];
static size_t transmitted_bytes, write_calls;
#define po33_write_transfer_wav test_write_transfer
#define main                    po33_encode_main
#include "../examples/po33_encode_capture.c"
#undef main
#undef po33_write_transfer_wav

int test_write_transfer(const char *path, const uint8_t *frame, size_t bytes, uint32_t rate) {
  FILE *fp;
  int closed;
  size_t written;
  (void)rate;
  if (!path || !frame)
    return 0;
  ++write_calls;
  assert(bytes <= sizeof(transmitted));
  fp = fopen(path, "wbx");
  if (!fp)
    return 0;
  written = fwrite(frame, 1, bytes, fp);
  closed = fclose(fp);
  if (written != bytes || closed)
    return 0;
  memcpy(transmitted, frame, bytes);
  transmitted_bytes = bytes;
  return 1;
}

static uint8_t fixture[131072];
static size_t fixture_bytes;
static const uint8_t header[9] = {5, 0, 0, 0, 0xfe, 0xff, 2, 0, 7};
static char directory[128], input_path[256], output_path[256], wav_path[256], other_path[256];

static void save_bytes(const char *path, const void *bytes, size_t count) {
  FILE *fp = fopen(path, "wb");
  size_t written;
  int closed;
  assert(fp);
  written = fwrite(bytes, 1, count, fp);
  closed = fclose(fp);
  assert(written == count && closed == 0);
}
static void le16(uint8_t *out, unsigned value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}
static void le32(uint8_t *out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[i] = (uint8_t)(value >> (8 * i));
}
static void save_wav(const char *path, size_t frames, unsigned channels, unsigned rate,
                     unsigned bits, int floating, int extensible, float amplitude) {
  static const uint8_t guid_tail[12] = {0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
  size_t width = bits / 8, start = extensible ? 68 : 44;
  size_t data_bytes = frames * channels * width, size = start + data_bytes + (data_bytes & 1u);
  uint8_t *wav = (uint8_t *)calloc(size, 1);
  assert(wav && size <= UINT32_MAX);
  memcpy(wav, "RIFF", 4);
  le32(wav + 4, (uint32_t)(size - 8));
  memcpy(wav + 8, "WAVEfmt ", 8);
  le32(wav + 16, extensible ? 40 : 16);
  le16(wav + 20, extensible ? 65534 : floating ? 3 : 1);
  le16(wav + 22, channels);
  le32(wav + 24, rate);
  le32(wav + 28, rate * channels * (unsigned)width);
  le16(wav + 32, channels * (unsigned)width);
  le16(wav + 34, bits);
  if (extensible) {
    le16(wav + 36, 22);
    le16(wav + 38, bits);
    le32(wav + 44, floating ? 3 : 1);
    memcpy(wav + 48, guid_tail, sizeof(guid_tail));
  }
  memcpy(wav + start - 8, "data", 4);
  le32(wav + start - 4, (uint32_t)data_bytes);
  for (size_t i = 0; i < frames * channels; ++i) {
    float value = channels == 2 && (i & 1u) ? -amplitude : amplitude;
    uint32_t word;
    if (floating)
      memcpy(&word, &value, sizeof(word));
    else {
      double scale = bits == 16 ? 32768.0 : bits == 24 ? 8388608.0 : 2147483648.0;
      word = (uint32_t)(int32_t)(value * scale);
    }
    for (size_t j = 0; j < width; ++j)
      wav[start + i * width + j] = (uint8_t)(word >> (8 * j));
  }
  save_bytes(path, wav, size);
  free(wav);
}
static void append(po33_builder_t *builder, unsigned tag, const uint8_t *bytes, size_t size) {
  po_status_t status = po33_builder_append_packet(builder, (uint16_t)tag, bytes, size, NULL);
  assert(status == PO_OK);
}
static void make_fixture(size_t samples, int extension, int duplicate) {
  po33_builder_t builder;
  uint8_t data[255], selector = 2;
  po_status_t status = po33_builder_init(&builder, fixture, sizeof(fixture));
  assert(status == PO_OK);
  for (unsigned i = 0; i < sizeof(data); ++i)
    data[i] = (uint8_t)(i * 13 + 7);
  append(&builder, PO33_TAG_SETTINGS, data, 31);
  append(&builder, 0x1234, data, 5);
  append(&builder, PO33_TAG_PATTERN_SELECT, &selector, 1);
  append(&builder, PO33_TAG_PATTERN_DATA, data, 255);
  selector = 7;
  append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1);
  for (size_t pos = 0; pos < samples + 9;) {
    size_t size = samples + 9 - pos;
    if (size > 255)
      size = 255;
    for (size_t j = 0; j < size; ++j)
      data[j] = pos + j < 9 ? header[pos + j] : (uint8_t)((pos + j) % 128);
    append(&builder, PO33_TAG_SOUND_DATA, data, size);
    pos += size;
    if (extension && pos == 255)
      append(&builder, 0x4321, data, 3);
  }
  selector = duplicate ? 7 : 8;
  append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1);
  append(&builder, PO33_TAG_SOUND_DATA, header, sizeof(header));
  selector = 0;
  append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1);
  for (size_t pos = 0; pos < 4105;) {
    size_t size = 4105 - pos;
    if (size > sizeof(data))
      size = sizeof(data);
    for (size_t i = 0; i < size; ++i)
      data[i] = (uint8_t)(pos + i);
    append(&builder, PO33_TAG_SOUND_DATA, data, size);
    pos += size;
  }
  status = po33_builder_finish(&builder, &fixture_bytes);
  assert(status == PO_OK);
  save_bytes(input_path, fixture, fixture_bytes);
}

typedef struct {
  po_packet_t packets[512];
  size_t count;
} packet_list_t;
static int collect(const po_packet_t *packet, void *user) {
  packet_list_t *list = (packet_list_t *)user;
  assert(list->count < 512);
  list->packets[list->count++] = *packet;
  return 0;
}
static packet_list_t original, result;
static uint8_t pool[65536];
static size_t collect_pool(const uint8_t *frame, size_t bytes, packet_list_t *packets) {
  po33_decode_result_t decoded;
  int selected = 0;
  size_t size = 0;
  packets->count = 0;
  po_status_t status = po33_frame_parse(frame, bytes, collect, packets, &decoded);
  assert(status == PO_OK && decoded.done && decoded.frame_bytes == bytes);
  for (size_t i = 0; i < packets->count; ++i) {
    po_packet_t *p = &packets->packets[i];
    if (p->tag_code == PO33_TAG_SOUND_SELECT)
      selected = p->payload[0] == 7;
    if (selected && p->tag_code == PO33_TAG_SOUND_DATA) {
      assert(size + p->payload_len <= sizeof(pool));
      memcpy(pool + size, p->payload, p->payload_len);
      size += p->payload_len;
    }
  }
  return size;
}
static void verify_preservation(size_t allocation, size_t extra_blocks) {
  size_t old_pool = collect_pool(fixture, fixture_bytes, &original);
  size_t new_pool = collect_pool(transmitted, transmitted_bytes, &result);
  size_t before = 0, old_end, new_end;
  assert(new_pool == allocation + 9 && !memcmp(pool, header, 9));
  while (original.packets[before].tag_code != PO33_TAG_SOUND_SELECT)
    ++before;
  old_end = before + 1;
  while (original.packets[old_end].tag_code != PO33_TAG_SOUND_SELECT)
    ++old_end;
  new_end = old_end + 17 * extra_blocks;
  assert(result.count == original.count + 17 * extra_blocks);
  for (size_t i = 0; i <= before; ++i)
    assert(original.packets[i].tag_code == result.packets[i].tag_code &&
           original.packets[i].payload_len == result.packets[i].payload_len &&
           !memcmp(original.packets[i].payload, result.packets[i].payload,
                   original.packets[i].payload_len));
  for (size_t i = old_end, j = new_end; i < original.count; ++i, ++j)
    assert(original.packets[i].tag_code == result.packets[j].tag_code &&
           original.packets[i].payload_len == result.packets[j].payload_len &&
           !memcmp(original.packets[i].payload, result.packets[j].payload,
                   original.packets[i].payload_len));
  for (size_t i = before + 1; i < old_end; ++i)
    assert(original.packets[i].tag_code == result.packets[i].tag_code &&
           original.packets[i].payload_len == result.packets[i].payload_len);
  for (size_t b = 0; b < extra_blocks; ++b)
    for (size_t p = 0; p < 17; ++p) {
      po_packet_t *packet = &result.packets[old_end + 17 * b + p];
      assert(packet->tag_code == PO33_TAG_SOUND_DATA && packet->payload_len == (p < 16 ? 255 : 16));
    }
  assert(transmitted_bytes == fixture_bytes + extra_blocks * 4181);
  assert(old_pool >= 9);
}
static void run(int argc, char **argv, int success) {
  size_t calls = write_calls;
  int status = po33_encode_main(argc, argv);
  assert((status == 0) == success);
  if (success) {
    assert(write_calls == calls + 1);
    status = unlink(output_path);
    assert(status == 0);
  } else {
    struct stat st;
    assert(write_calls == calls);
    status = stat(output_path, &st);
    assert(status != 0 && errno == ENOENT);
  }
}
static void test_growth_and_padding(void) {
  char *argv[] = {"encode", input_path, output_path, "--sample", "7", wav_path};
  make_fixture(4096, 0, 0);
  save_wav(wav_path, 15000, 1, 44100, 24, 0, 1, .25f);
  run(6, argv, 1);
  verify_preservation(8192, 1);
  size_t count;
  po_status_t status = po33_sample_count_for_pcm(15000, 44100, &count);
  assert(status == PO_OK && count > 4096 && count <= 8192);
  assert(pool[9 + 1000] == po33_sample_encode(8192));
  for (size_t i = count; i < 8192; ++i)
    assert(pool[9 + i] == 0);
  save_wav(wav_path, 23000, 1, 23438, 16, 0, 0, .25f);
  run(6, argv, 1);
  verify_preservation(24576, 5);
  for (size_t i = 0; i < 23000; ++i)
    assert(pool[9 + i] == po33_sample_encode(8192));
  for (size_t i = 23000; i < 24576; ++i)
    assert(pool[9 + i] == 0);
  save_wav(wav_path, 5, 1, 23438, 32, 0, 0, .25f);
  run(6, argv, 1);
  verify_preservation(4096, 0);
  for (size_t i = 0; i < 4096; ++i)
    assert(pool[9 + i] == (i < 5 ? po33_sample_encode(8192) : 0));
}
static void test_channels_and_formats(void) {
  char *argv[] = {"encode", input_path, output_path, "--sample",
                  "7",      wav_path,   "--channel", "left"};
  static const unsigned bits[] = {16, 24, 32, 32};
  make_fixture(4096, 0, 0);
  for (unsigned format = 0; format < 4; ++format)
    for (int extensible = 0; extensible < 2; ++extensible) {
      save_wav(wav_path, 100, 2, 23438, bits[format], format == 3, extensible, .25f);
      argv[7] = "left";
      run(8, argv, 1);
      verify_preservation(4096, 0);
      assert(pool[9] == po33_sample_encode(8192));
      argv[7] = "right";
      run(8, argv, 1);
      verify_preservation(4096, 0);
      assert(pool[9] == po33_sample_encode(-8192));
      argv[7] = "mix";
      run(8, argv, 1);
      verify_preservation(4096, 0);
      for (size_t i = 0; i < 4096; ++i)
        assert(pool[9 + i] == 0);
    }
  save_wav(wav_path, 100, 1, 192000, 32, 1, 0, .25f);
  argv[7] = "left";
  run(8, argv, 1);
  verify_preservation(4096, 0);
  save_wav(wav_path, 100, 1, 8000, 16, 0, 0, .25f);
  argv[7] = "mix";
  run(8, argv, 1);
  verify_preservation(4096, 0);
}
static void test_rejections(void) {
  char *argv[] = {"encode",    input_path, output_path, "--sample", "7",       wav_path,
                  "--channel", "right",    "--sample",  "8",        other_path};
  make_fixture(4096, 0, 0);
  save_wav(wav_path, 100, 2, 23438, 16, 0, 0, .25f);
  run(6, argv, 0);
  save_wav(wav_path, 100, 1, 23438, 16, 0, 0, .25f);
  run(8, argv, 0);
  save_wav(wav_path, 0, 1, 23438, 16, 0, 0, .25f);
  run(6, argv, 0);
  save_wav(wav_path, 100, 1, 23438, 32, 1, 0, NAN);
  run(6, argv, 0);
  save_wav(wav_path, 100, 1, 23438, 32, 1, 0, 1.1f);
  run(6, argv, 0);
  save_wav(wav_path, 5000, 1, 23438, 16, 0, 0, .25f);
  make_fixture(4095, 0, 0);
  run(6, argv, 0);
  make_fixture(4096, 1, 0);
  run(6, argv, 0);
  make_fixture(4096, 0, 1);
  run(6, argv, 0);
  make_fixture(4096, 0, 0);
  argv[7] = "left";
  save_wav(other_path, 100, 1, 23438, 16, 0, 0, .25f);
  run(11, argv, 0); /* a later reference-only target fails before output */
  argv[4] = "8";
  run(6, argv, 0);
}
static void test_multiple_and_existing_output(void) {
  char *argv[] = {"encode", input_path, output_path, "--sample", "7",
                  wav_path, "--sample", "0",         other_path};
  make_fixture(4096, 0, 0);
  save_wav(wav_path, 5000, 1, 23438, 16, 0, 0, .25f);
  save_wav(other_path, 9000, 1, 23438, 16, 0, 0, -.25f);
  run(9, argv, 1);
  size_t size = collect_pool(transmitted, transmitted_bytes, &result);
  assert(size == 8201 && !memcmp(pool, header, 9));
  size_t count = 0;
  int selected = 0;
  for (size_t i = 0; i < result.count; ++i) {
    const po_packet_t *p = &result.packets[i];
    if (p->tag_code == PO33_TAG_SOUND_SELECT)
      selected = p->payload[0] == 0;
    if (selected && p->tag_code == PO33_TAG_SOUND_DATA) {
      for (size_t j = 0; j < p->payload_len; ++j, ++count)
        assert(p->payload[j] == (count < 9      ? (uint8_t)count
                                 : count < 9009 ? po33_sample_encode(-8192)
                                                : 0));
    }
  }
  assert(count == 12297 && transmitted_bytes == fixture_bytes + 3 * 4181);
  save_bytes(output_path, "keep", 4);
  int status = po33_encode_main(9, argv);
  assert(status != 0);
  FILE *fp = fopen(output_path, "rb");
  char saved[8];
  assert(fp);
  size_t read = fread(saved, 1, sizeof(saved), fp);
  status = fclose(fp);
  assert(status == 0 && read == 4 && !memcmp(saved, "keep", 4));
  status = unlink(output_path);
  assert(status == 0);
  char *channel_only[] = {"encode", input_path, output_path, "--channel", "left"};
  run(5, channel_only, 0);
}
int main(void) {
  int made = 0;
  for (unsigned i = 0; i < 100 && !made; ++i) {
    snprintf(directory, sizeof(directory), "/tmp/libpo33-audio-edit-%ld-%u", (long)getpid(), i);
    made = mkdir(directory, 0700) == 0;
    if (!made)
      assert(errno == EEXIST);
  }
  assert(made);
  snprintf(input_path, sizeof(input_path), "%s/frame.bin", directory);
  snprintf(output_path, sizeof(output_path), "%s/output.wav", directory);
  snprintf(wav_path, sizeof(wav_path), "%s/sample.wav", directory);
  snprintf(other_path, sizeof(other_path), "%s/other.wav", directory);
  test_growth_and_padding();
  test_channels_and_formats();
  test_rejections();
  test_multiple_and_existing_output();
  puts("PO-33 flexible WAV edit tests passed");
  return 0;
}
