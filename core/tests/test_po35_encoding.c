#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "po_capture.h"

#define main po35_encode_main
#include "../examples/po35_encode_capture.c"
#undef main

static uint8_t fixture[16384];
static size_t fixture_size;
static uint16_t fixture_state;

/* Independent bitwise CRC writer; fixtures do not use the production builder. */
static void fixture_byte(uint8_t value) {
  assert(fixture_size < sizeof(fixture));
  fixture[fixture_size++] = (uint8_t)(value + fixture_state);
  fixture_state ^= (uint16_t)((uint16_t)value << 8);
  for (unsigned i = 0; i < 8; ++i)
    fixture_state = (uint16_t)((fixture_state << 1) ^ ((fixture_state & 0x8000u) ? 0x1021u : 0));
}

static void fixture_packet(uint16_t tag, const uint8_t *bytes, size_t count) {
  uint16_t trailer;
  fixture_byte((uint8_t)tag);
  fixture_byte((uint8_t)(tag >> 8));
  fixture_byte((uint8_t)count);
  for (size_t i = 0; i < count; ++i)
    fixture_byte(bytes[i]);
  trailer = fixture_state;
  fixture_byte((uint8_t)trailer);
  fixture_byte((uint8_t)(trailer >> 8));
}

static void make_fixture(size_t records, unsigned flags) {
  uint8_t header[] = {2, 0, (uint8_t)records, 60, 140, 43, 2, 3, 4};
  uint8_t body[220], pattern[183] = {3}, settings[159] = {0};
  const uint8_t unknown[] = {3, 1, 4, 1, 5};
  for (size_t i = 0; i < 4092; ++i)
    fixture[i] = (uint8_t)i;
  memcpy(fixture + 4092, "\x4d\xb8\x16\x5d", 4);
  fixture_size = 4096;
  fixture_state = 0x1d0f;
  memset(body, 0x2a, sizeof(body));
  fixture_packet(0x1234, unknown, sizeof(unknown));
  fixture_packet(0x37b2, body, 43);
  fixture_packet(0x7be7, body, 9);
  fixture_packet(0x1c7f, body, 13);
  fixture_packet(0xccb2, pattern, sizeof(pattern));
  fixture_packet(0x29ff, unknown, 1);
  if (flags & 4)
    ++header[2];
  if (flags & 64)
    header[0] = 15;
  if (flags & 32)
    fixture_packet(0x73b0, body, 44);
  fixture_packet(0xdc32, header, sizeof(header));
  if (flags & 256)
    fixture_packet(0x73b0, NULL, 0);
  while (records) {
    size_t batch = records > 5 ? 5 : records;
    fixture_packet(0x73b0, body, batch * 44);
    records -= batch;
  }
  if (flags & 1)
    fixture_packet(0x5678, unknown, sizeof(unknown));
  if (!(flags & 16))
    fixture_packet(0x29a1, unknown, (flags & 128) ? 1 : 0);
  header[0] = (flags & 8) ? 2 : 5;
  header[2] = 1;
  fixture_packet(0xdc32, header, sizeof(header));
  fixture_packet(0x73b0, body, 44);
  if (!(flags & 2))
    fixture_packet(0x5678, unknown, sizeof(unknown));
  fixture_packet(0x29a1, NULL, 0);
  settings[152] = 120;
  settings[155] = 3;
  settings[156] = 2;
  settings[157] = 2;
  settings[158] = 3;
  fixture_packet(0x84d8, settings, sizeof(settings));
  fixture_packet(0x71c3, NULL, 0);
}

static void write_file(const char *path, const void *bytes, size_t count) {
  FILE *fp = fopen(path, "wb");
  size_t written;
  int closed;
  assert(fp);
  written = fwrite(bytes, 1, count, fp);
  closed = fclose(fp);
  assert(written == count && closed == 0);
}

static void put16(FILE *fp, uint16_t n) {
  int status = fputc((uint8_t)n, fp);
  assert(status != EOF);
  status = fputc((uint8_t)(n >> 8), fp);
  assert(status != EOF);
}

static void put32(FILE *fp, uint32_t n) {
  put16(fp, (uint16_t)n);
  put16(fp, (uint16_t)(n >> 16));
}

static void write_audio(const char *path, uint32_t rate, size_t frames, unsigned channels,
                        unsigned bits, int floating, int extensible, int opposite) {
  unsigned width = bits / 8;
  uint32_t data_size = (uint32_t)(frames * channels * width);
  FILE *fp = fopen(path, "wb");
  size_t written;
  int status;
  assert(fp);
  written = fwrite("RIFF", 1, 4, fp);
  assert(written == 4);
  put32(fp, 36u + data_size + (extensible ? 24u : 0u) + (data_size & 1u));
  written = fwrite("WAVEfmt ", 1, 8, fp);
  assert(written == 8);
  put32(fp, extensible ? 40 : 16);
  put16(fp, (uint16_t)(extensible ? 0xfffe : floating ? 3 : 1));
  put16(fp, (uint16_t)channels);
  put32(fp, rate);
  put32(fp, rate * channels * width);
  put16(fp, (uint16_t)(channels * width));
  put16(fp, (uint16_t)bits);
  if (extensible) {
    const uint8_t guid_tail[] = {0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    put16(fp, 22);
    put16(fp, (uint16_t)bits);
    put32(fp, channels == 1 ? 4 : 3);
    put32(fp, floating ? 3 : 1);
    written = fwrite(guid_tail, 1, sizeof(guid_tail), fp);
    assert(written == sizeof(guid_tail));
  }
  written = fwrite("data", 1, 4, fp);
  assert(written == 4);
  put32(fp, data_size);
  for (size_t i = 0; i < frames; ++i) {
    float value = (float)(0.25 * sin(6.283185307179586 * 440 * (double)i / rate));
    for (unsigned ch = 0; ch < channels; ++ch) {
      float sample = ch && opposite ? -value : value;
      uint32_t word;
      if (floating)
        memcpy(&word, &sample, sizeof(word));
      else
        word = (uint32_t)(int32_t)((double)sample * (bits == 16   ? 32768.0
                                                     : bits == 24 ? 8388608.0
                                                                  : 2147483648.0));
      for (unsigned b = 0; b < width; ++b) {
        status = fputc((uint8_t)(word >> (8 * b)), fp);
        assert(status != EOF);
      }
    }
  }
  if (data_size & 1u) {
    status = fputc(0, fp);
    assert(status != EOF);
  }
  status = fclose(fp);
  assert(status == 0);
}

typedef struct {
  po_packet_t packets[256];
  size_t count;
} packet_list_t;

static int collect_packet(const po_packet_t *packet, void *user) {
  packet_list_t *list = (packet_list_t *)user;
  assert(list->count < sizeof(list->packets) / sizeof(list->packets[0]));
  list->packets[list->count++] = *packet;
  return 0;
}

static void list_frame(const uint8_t *frame, size_t bytes, packet_list_t *list) {
  po35_decode_result_t result;
  po_status_t status;
  list->count = 0;
  status = po35_frame_parse(frame, bytes, collect_packet, list, &result);
  assert(status == PO_OK);
  assert(result.done && !result.stopped && result.frame_bytes == bytes);
}

static uint8_t *load_transfer(const char *path, size_t *bytes, packet_list_t *list) {
  po_capture_t capture = {0};
  po35_decode_result_t result;
  uint8_t *wire = NULL;
  size_t wire_bytes = 0;
  int status = po_capture_read(path, &capture);
  po_status_t parsed;
  assert(status);
  status = po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, &wire, &wire_bytes);
  assert(status);
  free(capture.samples);
  parsed = po35_frame_parse(wire, wire_bytes, NULL, NULL, &result);
  assert(parsed == PO_OK);
  assert(result.done);
  *bytes = result.frame_bytes;
  list_frame(wire, *bytes, list);
  return wire;
}

static size_t speech_bytes(const packet_list_t *list, unsigned slot, uint8_t *records,
                           unsigned count, unsigned fps) {
  size_t bytes = 0;
  int active = 0, seen = 0, ended = 0;
  for (size_t i = 0; i < list->count; ++i) {
    const po_packet_t *p = &list->packets[i];
    if (p->tag_code == 0xdc32) {
      active = p->payload[0] == slot;
      if (active) {
        const uint8_t metadata[] = {140, 43, 2, 3, 4};
        assert(p->payload_len == 9 && !seen);
        assert(p->payload[1] == count / 256 && p->payload[2] == count % 256);
        assert(p->payload[3] == fps);
        assert(!memcmp(p->payload + 4, metadata, sizeof(metadata)));
        seen = 1;
      }
    } else if (active && p->tag_code == 0x73b0) {
      assert(p->payload_len && p->payload_len <= 220 && p->payload_len % 44 == 0);
      assert(bytes + p->payload_len <= count * 44);
      memcpy(records + bytes, p->payload, p->payload_len);
      bytes += p->payload_len;
    } else if (active && p->tag_code == 0x29a1) {
      assert(p->payload_len == 0);
      active = 0;
      ended = 1;
    }
  }
  assert(seen && ended && bytes == count * 44);
  return bytes;
}

static void without_slot(const packet_list_t *source, unsigned slot, packet_list_t *out) {
  int selected = 0;
  out->count = 0;
  for (size_t i = 0; i < source->count; ++i) {
    const po_packet_t *p = &source->packets[i];
    if (p->tag_code == 0xdc32)
      selected = p->payload[0] == slot;
    if (!(selected && (p->tag_code == 0xdc32 || p->tag_code == 0x73b0)))
      out->packets[out->count++] = *p;
    if (p->tag_code == 0x29a1)
      selected = 0;
  }
}

static void unchanged_outside_slot(const packet_list_t *before, const packet_list_t *after,
                                   unsigned slot) {
  packet_list_t a, b;
  without_slot(before, slot, &a);
  without_slot(after, slot, &b);
  assert(a.count == b.count);
  for (size_t i = 0; i < a.count; ++i) {
    assert(a.packets[i].tag_code == b.packets[i].tag_code);
    assert(a.packets[i].payload_len == b.packets[i].payload_len);
    assert(!memcmp(a.packets[i].payload, b.packets[i].payload, a.packets[i].payload_len));
  }
}

static int exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static void test_new_audio(void) {
  char *argv[] = {"encode", "input.bin", "output.wav", "--speech", "2", "audio.wav"};
  packet_list_t before, after;
  uint8_t records[11 * 44], *wire, *saved, *again;
  size_t bytes, saved_bytes, again_bytes, record_bytes;
  int status;
  make_fixture(1, 0);
  list_frame(fixture, fixture_size, &before);
  write_file("input.bin", fixture, fixture_size);
  write_audio("audio.wav", 48000, 6600, 1, 16, 0, 0, 0);
  status = po35_encode_main(6, argv);
  assert(status == 0);
  wire = load_transfer("output.wav", &bytes, &after);
  assert(bytes == fixture_size + 10 * 44 + 2 * 5);
  record_bytes = speech_bytes(&after, 2, records, 11, 80);
  assert(record_bytes == sizeof(records));
  assert(records[2] != 0);
  unchanged_outside_slot(&before, &after, 2);
  free(wire);
  status = e35_read_file("output.wav", &saved, &saved_bytes);
  assert(status);
  status = po35_encode_main(6, argv);
  assert(status != 0);
  status = e35_read_file("output.wav", &again, &again_bytes);
  assert(status);
  assert(saved_bytes == again_bytes && !memcmp(saved, again, saved_bytes));
  free(saved);
  free(again);
  status = remove("output.wav");
  assert(status == 0);
}

static void test_shrink_and_empty(void) {
  char *argv[] = {"encode", "input.bin", "output.wav", "--speech", "2", "audio.wav", "--fps", "20"};
  packet_list_t before, after;
  uint8_t records[44];
  for (unsigned original = 0; original <= 9; original += 9) {
    size_t bytes, record_bytes;
    uint8_t *wire;
    int status;
    make_fixture(original, 0);
    list_frame(fixture, fixture_size, &before);
    write_file("input.bin", fixture, fixture_size);
    write_audio("audio.wav", 44100, 442, 1, 24, 0, 1, 0);
    status = po35_encode_main(8, argv);
    assert(status == 0);
    wire = load_transfer("output.wav", &bytes, &after);
    record_bytes = speech_bytes(&after, 2, records, 1, 20);
    assert(record_bytes == 44);
    unchanged_outside_slot(&before, &after, 2);
    free(wire);
    status = remove("output.wav");
    assert(status == 0);
  }
}

static void test_audio_reader(void) {
  const unsigned bits[] = {16, 24, 32, 32};
  for (unsigned kind = 0; kind < 4; ++kind)
    for (unsigned channels = 1; channels <= 2; ++channels)
      for (int extensible = 0; extensible <= 1; ++extensible) {
        po_audio_t audio = {0};
        po_capture_t capture = {0};
        int status;
        double expected = 0.25 * sin(6.283185307179586 * 440 / 44100);
        write_audio("reader.wav", 44100, 13, channels, bits[kind], kind == 3, extensible, 1);
        status = po_audio_read("reader.wav", &audio);
        assert(status);
        assert(audio.frames == 13 && audio.rate == 44100 && audio.channels == channels);
        assert(audio.samples[0] == 0 && fabs(audio.samples[channels] - expected) < 0.000031);
        if (channels == 2)
          assert(audio.samples[2] == -audio.samples[3]);
        free(audio.samples);
        status = po_capture_read("reader.wav", &capture);
        assert(!status);
        assert(capture.samples == NULL && capture.frames == 0);
      }
  {
    po_capture_t capture = {0};
    po_audio_t audio = {0};
    int status;
    write_audio("reader.wav", 32000, 16000, 2, 16, 0, 0, 0);
    status = po_capture_read("reader.wav", &capture);
    assert(status);
    free(capture.samples);
    write_audio("reader.wav", 8000, 4000, 2, 16, 0, 0, 0);
    status = po_capture_read("reader.wav", &capture);
    assert(!status);
    status = po_audio_read("reader.wav", &audio);
    assert(status);
    free(audio.samples);
    write_audio("reader.wav", 44100, 1, 3, 16, 0, 0, 0);
    audio = (po_audio_t){0};
    status = po_audio_read("reader.wav", &audio);
    assert(!status && audio.samples == NULL && audio.frames == 0);
    write_audio("reader.wav", 44100, 0, 1, 16, 0, 0, 0);
    status = po_audio_read("reader.wav", &audio);
    assert(!status && audio.samples == NULL && audio.frames == 0);
  }
}

static void expect_failure(int argc, char **argv) {
  int status;
  assert(!exists(argv[2]));
  status = po35_encode_main(argc, argv);
  assert(status != 0);
  assert(!exists(argv[2]));
}

static void test_channel_selection(void) {
  char *argv[] = {"encode", "input.bin", "output.wav", "--speech",
                  "2",      "audio.wav", "--channel",  "mix"};
  const char *channels[] = {"mix", "left", "right"};
  make_fixture(1, 0);
  write_file("input.bin", fixture, fixture_size);
  write_audio("audio.wav", 48000, 1200, 2, 32, 1, 1, 1);
  expect_failure(6, argv);
  for (unsigned i = 0; i < 3; ++i) {
    uint8_t *wire, records[88];
    packet_list_t after;
    size_t bytes, record_bytes;
    int status;
    argv[7] = (char *)channels[i];
    status = po35_encode_main(8, argv);
    assert(status == 0);
    wire = load_transfer("output.wav", &bytes, &after);
    record_bytes = speech_bytes(&after, 2, records, 2, 80);
    assert(record_bytes == 88);
    assert(i ? records[2] != 0 : records[2] == 0 && records[46] == 0);
    free(wire);
    status = remove("output.wav");
    assert(status == 0);
  }
  write_audio("audio.wav", 48000, 1200, 1, 16, 0, 0, 0);
  expect_failure(8, argv); /* right channel does not exist in a mono file */
}

static void test_unchanged_and_repeated_edits(void) {
  char *argv[] = {"encode",   "input.bin", "output.wav", "--speech", "2", "audio.wav",
                  "--speech", "5",         "audio.wav",  "--fps",    "40"};
  packet_list_t after;
  size_t bytes, record_bytes;
  uint8_t *wire, records[88];
  int status;
  make_fixture(1, 1); /* Even extensions inside speech groups survive no-edit mode. */
  write_file("input.bin", fixture, fixture_size);
  status = po35_encode_main(3, argv);
  assert(status == 0);
  wire = load_transfer("output.wav", &bytes, &after);
  assert(bytes == fixture_size && !memcmp(wire, fixture, bytes));
  free(wire);
  status = remove("output.wav");
  assert(status == 0);

  /* No-edit mode preserves a wire-valid capture even when a speech group is
   * incomplete. The transactional edit API intentionally rejects that source. */
  make_fixture(1, 16);
  write_file("input.bin", fixture, fixture_size);
  status = po35_encode_main(3, argv);
  assert(status == 0);
  wire = load_transfer("output.wav", &bytes, &after);
  assert(bytes == fixture_size && !memcmp(wire, fixture, bytes));
  free(wire);
  status = remove("output.wav");
  assert(status == 0);

  make_fixture(1, 2);
  write_file("input.bin", fixture, fixture_size);
  write_audio("audio.wav", 48000, 1200, 1, 32, 0, 0, 0);
  status = po35_encode_main(11, argv);
  assert(status == 0);
  wire = load_transfer("output.wav", &bytes, &after);
  record_bytes = speech_bytes(&after, 2, records, 1, 40);
  assert(record_bytes == 44);
  record_bytes = speech_bytes(&after, 5, records, 1, 40);
  assert(record_bytes == 44);
  free(wire);
  status = remove("output.wav");
  assert(status == 0);
  argv[8] = "missing-second.wav";
  expect_failure(11, argv); /* A later input failure must not create a partial output. */
}

static void overwrite_word(const char *path, long offset, uint32_t word) {
  FILE *fp = fopen(path, "r+b");
  int status;
  assert(fp);
  status = fseek(fp, offset, SEEK_SET);
  assert(status == 0);
  put32(fp, word);
  status = fclose(fp);
  assert(status == 0);
}

static void test_failures_create_nothing(void) {
  char *base[] = {"encode", "input.bin", "output.wav", "--speech", "2", "audio.wav"};
  const unsigned malformed[] = {1, 4, 8, 16, 32, 64, 128, 256};
  write_audio("audio.wav", 48000, 1200, 1, 16, 0, 0, 0);
  for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
    make_fixture(1, malformed[i]);
    write_file("input.bin", fixture, fixture_size);
    expect_failure(6, base);
  }
  make_fixture(1, 0);
  fixture[fixture_size - 1] ^= 1;
  write_file("input.bin", fixture, fixture_size);
  expect_failure(6, base);
  make_fixture(1, 0);
  write_file("input.bin", fixture, fixture_size);
  base[4] = "3";
  expect_failure(6, base);
  base[4] = "2";
  base[5] = "missing.wav";
  expect_failure(6, base);
  base[5] = "audio.wav";
  {
    char *duplicates[] = {"encode",    "input.bin", "output.wav", "--speech", "2",
                          "audio.wav", "--speech",  "2",          "audio.wav"};
    char *unused[] = {"encode", "input.bin", "output.wav", "--fps", "80"};
    char *bad_fps[] = {"encode", "input.bin", "output.wav", "--speech",
                       "2",      "audio.wav", "--fps",      "30"};
    expect_failure(9, duplicates);
    expect_failure(5, unused);
    unused[3] = "--channel";
    unused[4] = "left";
    expect_failure(5, unused);
    expect_failure(8, bad_fps);
    base[4] = "15";
    expect_failure(6, base);
    base[4] = "2";
  }
  for (unsigned i = 0; i < 3; ++i) {
    const uint32_t invalid[] = {0x7fc00000u, 0x7f800000u, 0x40000000u};
    po_audio_t audio = {0};
    write_audio("audio.wav", 48000, 1200, 1, 32, 1, 0, 0);
    overwrite_word("audio.wav", 44, invalid[i]);
    expect_failure(6, base);
    if (i < 2) {
      int status = po_audio_read("audio.wav", &audio);
      assert(!status && !audio.samples && !audio.frames);
    }
  }
  write_audio("audio.wav", 48000, 0, 1, 16, 0, 0, 0);
  expect_failure(6, base);
}

static void test_record_budget(void) {
  char *argv[] = {"encode", "input.bin", "output.wav", "--speech", "2", "audio.wav", "--fps", "20"};
  int status;
  make_fixture(1, 0);
  write_file("input.bin", fixture, fixture_size);
  /* Low fps permits more than eight seconds; the creation budget is records. */
  write_audio("audio.wav", 8000, 256000, 1, 16, 0, 0, 0);
  status = po35_encode_main(8, argv);
  assert(status == 0);
  {
    packet_list_t after;
    uint8_t records[640 * 44], *wire;
    size_t bytes, record_bytes;
    wire = load_transfer("output.wav", &bytes, &after);
    record_bytes = speech_bytes(&after, 2, records, 640, 20);
    assert(record_bytes == sizeof(records));
    free(wire);
  }
  status = remove("output.wav");
  assert(status == 0);
  write_audio("audio.wav", 8000, 256001, 1, 16, 0, 0, 0);
  expect_failure(8, argv);
  argv[7] = "80";
  write_audio("audio.wav", 8000, 64001, 1, 16, 0, 0, 0);
  expect_failure(8, argv);
}

int main(void) {
  char directory[128];
  int created = 0;
  int status;
  for (unsigned i = 0; i < 100 && !created; ++i) {
    snprintf(directory, sizeof(directory), "/tmp/libpo35-encoding-%ld-%u", (long)getpid(), i);
    created = mkdir(directory, 0700) == 0;
    assert(created || errno == EEXIST);
  }
  assert(created);
  status = chdir(directory);
  assert(status == 0);
  test_new_audio();
  test_shrink_and_empty();
  test_audio_reader();
  test_channel_selection();
  test_unchanged_and_repeated_edits();
  test_failures_create_nothing();
  test_record_budget();
  puts("PO-35 audio encoding tests passed");
  return 0;
}
