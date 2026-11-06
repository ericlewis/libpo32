/* Exercise hosted I/O errors without production fault switches or fake DSP state. */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po_capture.h"
#include "po_frame_report.h"
#include "po_stereo_wav.h"
#include "po33.h"
#include "po35.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum io_fault {
  IO_ALLOC,
  IO_FOPEN,
  IO_SEEK,
  IO_TELL,
  IO_READ,
  IO_FCLOSE,
  IO_OPEN,
  IO_FDOPEN,
  IO_WRITE,
  IO_RENDER,
  IO_EMPTY_RENDER,
  IO_FAULTS
};
static struct {
  size_t calls, at;
} failures[IO_FAULTS];
static int last_fd = -1;
static int injected(enum io_fault kind) {
  ++failures[kind].calls;
  if (failures[kind].calls != failures[kind].at)
    return 0;
  errno = EIO;
  return 1;
}
static void reset(void) {
  memset(failures, 0, sizeof(failures));
  last_fd = -1;
}
static void *io_malloc(size_t size) {
  return injected(IO_ALLOC) ? NULL : malloc(size);
}
static FILE *io_fopen(const char *path, const char *mode) {
  return injected(IO_FOPEN) ? NULL : fopen(path, mode);
}
static int io_fseek(FILE *fp, long offset, int whence) {
  return injected(IO_SEEK) ? -1 : fseek(fp, offset, whence);
}
static long io_ftell(FILE *fp) {
  return injected(IO_TELL) ? -1L : ftell(fp);
}
static size_t io_fread(void *data, size_t size, size_t count, FILE *fp) {
  return injected(IO_READ) ? 0 : fread(data, size, count, fp);
}
static int io_fclose(FILE *fp) {
  int fault = injected(IO_FCLOSE), status = fclose(fp);
  return fault ? EOF : status;
}
static int io_open(const char *path, int flags, ...) {
  mode_t mode;
  va_list ap;
  if (injected(IO_OPEN))
    return -1;
  va_start(ap, flags);
  mode = (mode_t)va_arg(ap, int);
  va_end(ap);
  last_fd = open(path, flags, mode);
  return last_fd;
}
static FILE *io_fdopen(int fd, const char *mode) {
  return injected(IO_FDOPEN) ? NULL : fdopen(fd, mode);
}
static size_t io_fwrite(const void *data, size_t size, size_t count, FILE *fp) {
  return injected(IO_WRITE) ? 0 : fwrite(data, size, count, fp);
}
static po_status_t io_render(po_stereo_modulator_t *state, float *out, size_t capacity,
                             size_t *written) {
  if (injected(IO_RENDER)) {
    *written = 0;
    return PO_ERR_RANGE;
  }
  if (injected(IO_EMPTY_RENDER)) {
    *written = 0;
    return PO_OK;
  }
  return po_stereo_modulator_render_f32(state, out, capacity, written);
}

#define malloc io_malloc
#define fopen  io_fopen
#define fseek  io_fseek
#define ftell  io_ftell
#define fread  io_fread
#define fclose io_fclose
#include "../examples/po_capture.c"
#define open                           io_open
#define fdopen                         io_fdopen
#define fwrite                         io_fwrite
#define po_stereo_modulator_render_f32 io_render
#include "../examples/po_stereo_wav.c"
#undef malloc
#undef fopen
#undef fseek
#undef ftell
#undef fread
#undef fclose
#undef open
#undef fdopen
#undef fwrite
#undef po_stereo_modulator_render_f32

#define main po_capture_doctor_example_main
#include "../examples/po_capture_doctor.c"
#undef main

static int exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}
static void save(const char *path, const void *data, size_t size) {
  FILE *fp = fopen(path, "wb");
  size_t count;
  int status;
  assert(fp);
  count = fwrite(data, 1, size, fp);
  status = fclose(fp);
  assert(count == size && status == 0);
}
static size_t make_frame(uint8_t *frame, size_t capacity, int family35) {
  size_t size = 0;
  po_status_t status;
  const uint8_t extension[] = {1, 2, 3, 4};
  if (family35) {
    po35_builder_t builder;
    status = po35_builder_init(&builder, frame, capacity);
    assert(status == PO_OK);
    status = po35_builder_append_packet(&builder, 0x1234, extension, sizeof(extension), NULL);
    assert(status == PO_OK);
    status = po35_builder_finish(&builder, &size);
  } else {
    po33_builder_t builder;
    status = po33_builder_init(&builder, frame, capacity);
    assert(status == PO_OK);
    status = po33_builder_append_packet(&builder, 0x1234, extension, sizeof(extension), NULL);
    assert(status == PO_OK);
    status = po33_builder_finish(&builder, &size);
  }
  assert(status == PO_OK);
  return size;
}
static void small_wav(uint8_t *bytes, size_t size) {
  assert(size >= 48);
  memset(bytes, 0, size);
  memcpy(bytes, "RIFF", 4);
  wav_le32(bytes + 4, (uint32_t)size - 8);
  memcpy(bytes + 8, "WAVEfmt ", 8);
  wav_le32(bytes + 16, 16);
  wav_le16(bytes + 20, 1);
  wav_le16(bytes + 22, 2);
  wav_le32(bytes + 24, 48000);
  wav_le32(bytes + 28, 192000);
  wav_le16(bytes + 32, 4);
  wav_le16(bytes + 34, 16);
  memcpy(bytes + 36, "data", 4);
  wav_le32(bytes + 40, 4);
  wav_le16(bytes + 44, 0x8000);
  wav_le16(bytes + 46, 0x4000);
}
static void test_reader_errors(void) {
  uint8_t bytes[48];
  const enum io_fault kinds[] = {IO_ALLOC, IO_FOPEN, IO_SEEK, IO_TELL, IO_READ, IO_FCLOSE};
  po_audio_t audio = {0};
  po_capture_t capture = {0};
  int ok;
  small_wav(bytes, sizeof(bytes));
  save("small.wav", bytes, sizeof(bytes));
  ok = po_audio_read("small.wav", &audio);
  assert(ok && audio.frames == 1 && audio.channels == 2 && audio.rate == 48000 &&
         audio.samples[0] == -1.0f && audio.samples[1] == 0.5f);
  free(audio.samples);
  for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
    size_t limit = kinds[i] == IO_ALLOC || kinds[i] == IO_SEEK ? 2 : 1;
    for (size_t nth = 1; nth <= limit; ++nth) {
      reset();
      failures[kinds[i]].at = nth;
      audio = (po_audio_t){0};
      ok = po_audio_read("small.wav", &audio);
      assert(!ok && !audio.samples && !audio.frames && !audio.rate && !audio.channels);
      assert(failures[kinds[i]].calls >= nth);
    }
  }
  reset();
  ok = po_audio_read(NULL, &audio);
  assert(!ok && !audio.samples && !audio.frames);
  ok = po_audio_read("small.wav", NULL);
  assert(!ok);
  ok = po_capture_read("small.wav", NULL);
  assert(!ok);
  ok = po_capture_read(NULL, &capture);
  assert(!ok && !capture.samples && !capture.frames && !capture.rate);
  ok = po_capture_read("small.wav", &capture);
  assert(!ok && !capture.samples && !capture.frames && !capture.rate);
}
static void test_malformed_chunks(void) {
  uint8_t bytes[96], base[48];
  po_audio_t audio = {0};
  int ok;
  small_wav(base, sizeof(base));
  /* A non-padded odd final extension belongs to RIFF, not to the PCM data. */
  memcpy(bytes, base, sizeof(base));
  memcpy(bytes + 48, "JUNK", 4);
  wav_le32(bytes + 52, 1);
  bytes[56] = 7;
  wav_le32(bytes + 4, 49);
  save("odd.wav", bytes, 57);
  ok = po_audio_read("odd.wav", &audio);
  assert(ok && audio.frames == 1 && audio.samples[1] == 0.5f);
  free(audio.samples);
  audio = (po_audio_t){0};
  /* With a pad, parsing continues to another trailing chunk. */
  bytes[57] = 0;
  memcpy(bytes + 58, "JUNK", 4);
  wav_le32(bytes + 62, 0);
  wav_le32(bytes + 4, 58);
  save("odd-padded.wav", bytes, 66);
  po_audio_t padded = {0};
  ok = po_audio_read("odd-padded.wav", &padded);
  assert(ok && padded.frames == 1 && padded.samples[0] == -1.0f);
  free(padded.samples);
  /* The extensible format marker cannot make a short fmt payload safe. */
  memcpy(bytes, base, sizeof(base));
  wav_le16(bytes + 20, 0xfffe);
  save("short-extensible.wav", bytes, sizeof(base));
  ok = po_audio_read("short-extensible.wav", &audio);
  assert(!ok && !audio.samples && !audio.frames);
  for (unsigned variant = 0; variant < 10; ++variant) {
    memcpy(bytes, base, sizeof(base));
    switch (variant) {
    case 0:
      bytes[0] = 'X';
      break;
    case 1:
      bytes[8] = 'X';
      break;
    case 2:
      wav_le32(bytes + 4, 3);
      break;
    case 3:
      wav_le32(bytes + 4, 100);
      break;
    case 4:
      wav_le32(bytes + 24, 7999);
      break;
    case 5:
      wav_le32(bytes + 24, 192001);
      break;
    case 6:
      wav_le16(bytes + 22, 0);
      break;
    case 7:
      wav_le16(bytes + 20, 3);
      break; /* Float16 is not supported. */
    case 8:
      wav_le16(bytes + 20, 2);
      break;
    case 9:
      wav_le16(bytes + 34, 8);
      break;
    }
    save("malformed.wav", bytes, sizeof(base));
    ok = po_audio_read("malformed.wav", &audio);
    assert(!ok && !audio.samples && !audio.frames && !audio.rate && !audio.channels);
  }
  {
    const uint8_t float_guid[16] = {3,    0, 0, 0,    0, 0,    0x10, 0,
                                    0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
    const uint32_t nonfinite[] = {0x7f800000u, 0xff800000u, 0x7fc00000u};
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, base, 36);
    wav_le32(bytes + 4, 64);
    wav_le32(bytes + 16, 40);
    wav_le16(bytes + 20, 0xfffe);
    wav_le16(bytes + 22, 1);
    wav_le16(bytes + 34, 32);
    wav_le16(bytes + 36, 22);
    wav_le16(bytes + 38, 16); /* A float GUID requires all 32 valid bits. */
    memcpy(bytes + 44, float_guid, sizeof(float_guid));
    memcpy(bytes + 60, "data", 4);
    wav_le32(bytes + 64, 4);
    wav_le32(bytes + 68, 0x3f000000u);
    save("extensible.wav", bytes, 72);
    ok = po_audio_read("extensible.wav", &audio);
    assert(!ok && !audio.samples && !audio.frames);
    wav_le16(bytes + 38, 32);
    save("extensible.wav", bytes, 72);
    ok = po_audio_read("extensible.wav", &audio);
    assert(ok && audio.frames == 1 && audio.channels == 1 && audio.samples[0] == 0.5f);
    free(audio.samples);
    audio = (po_audio_t){0};
    bytes[44] = 4; /* An unknown subtype must not be interpreted as float. */
    save("extensible.wav", bytes, 72);
    ok = po_audio_read("extensible.wav", &audio);
    assert(!ok && !audio.samples && !audio.frames);
    bytes[44] = 3;
    for (size_t i = 0; i < sizeof(nonfinite) / sizeof(nonfinite[0]); ++i) {
      wav_le32(bytes + 68, nonfinite[i]);
      save("extensible.wav", bytes, 72);
      ok = po_audio_read("extensible.wav", &audio);
      assert(!ok && !audio.samples && !audio.frames);
    }
  }
}
static void test_roundtrip_and_decode_errors(void) {
  uint8_t frame[8192];
  po_capture_t capture = {0};
  uint8_t *wire = NULL;
  size_t length = 0, frame_size = make_frame(frame, sizeof(frame), 1);
  int ok, status;
  ok = po35_write_transfer_wav("roundtrip.wav", frame, frame_size, 48000);
  assert(ok);
  ok = po_capture_read("roundtrip.wav", &capture);
  assert(ok && capture.rate == 48000 && capture.frames > 24000);
  ok = po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, &wire, &length);
  assert(ok && length >= frame_size && !memcmp(wire, frame, frame_size));
  free(wire);
  wire = NULL;
  for (size_t nth = 1; nth <= 3; ++nth) {
    reset();
    failures[IO_ALLOC].at = nth;
    length = 123;
    ok = po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, &wire, &length);
    assert(!ok && !wire && !length && failures[IO_ALLOC].calls == 3);
  }
  reset();
  ok = po_capture_decode(&capture, (po_stereo_channel_order_t)99, &wire, &length);
  assert(!ok && !wire && !length);
  ok = po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, NULL, &length);
  assert(!ok);
  ok = po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, &wire, NULL);
  assert(!ok);
  ok = po_capture_decode(NULL, PO_STEREO_CHANNEL_NORMAL, &wire, &length);
  assert(!ok && !wire && !length);
  {
    po_capture_t bad = {0};
    ok = po_capture_decode(&bad, PO_STEREO_CHANNEL_NORMAL, &wire, &length);
    assert(!ok && !wire && !length);
    bad = capture;
    bad.frames = 0;
    ok = po_capture_decode(&bad, PO_STEREO_CHANNEL_NORMAL, &wire, &length);
    assert(!ok && !wire && !length);
    bad = capture;
    bad.rate = 1;
    ok = po_capture_decode(&bad, PO_STEREO_CHANNEL_NORMAL, &wire, &length);
    assert(!ok && !wire && !length);
  }
  free(capture.samples);
  status = remove("roundtrip.wav");
  assert(status == 0);
}
static void test_writer_failures(void) {
  uint8_t frame[8192];
  size_t frame_size = make_frame(frame, sizeof(frame), 1);
  const enum io_fault kinds[] = {IO_OPEN,   IO_FDOPEN,       IO_WRITE,
                                 IO_RENDER, IO_EMPTY_RENDER, IO_FCLOSE};
  int ok;
  for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
    size_t limit = kinds[i] == IO_WRITE ? 2 : 1;
    for (size_t nth = 1; nth <= limit; ++nth) {
      int descriptor_status;
      reset();
      failures[kinds[i]].at = nth;
      ok = po35_write_transfer_wav("failed.wav", frame, frame_size, 48000);
      assert(!ok && !exists("failed.wav") && failures[kinds[i]].calls >= nth);
      if (last_fd >= 0) {
        descriptor_status = fcntl(last_fd, F_GETFD);
        assert(descriptor_status == -1 && errno == EBADF);
      }
    }
  }
  reset();
  {
    static const char sentinel[] = "existing output must survive";
    char actual[sizeof(sentinel)];
    FILE *fp;
    size_t read;
    int closed;
    save("existing.wav", sentinel, sizeof(sentinel));
    ok = po35_write_transfer_wav("existing.wav", frame, frame_size, 48000);
    assert(!ok);
    fp = fopen("existing.wav", "rb");
    assert(fp);
    read = fread(actual, 1, sizeof(actual), fp);
    closed = fclose(fp);
    assert(read == sizeof(actual) && closed == 0 && !memcmp(actual, sentinel, sizeof(actual)));
  }
}
static void test_writer_validation(void) {
  uint8_t frame[8192];
  int ok;
  for (int family35 = 0; family35 <= 1; ++family35) {
    size_t length = make_frame(frame, sizeof(frame), family35);
    int (*write)(const char *, const uint8_t *, size_t, uint32_t) =
        family35 ? po35_write_transfer_wav : po33_write_transfer_wav;
    ok = write(NULL, frame, length, 48000);
    assert(!ok);
    ok = write("invalid.wav", NULL, 0, 48000);
    assert(!ok && !exists("invalid.wav"));
    ok = write("invalid.wav", frame, (size_t)UINT32_MAX / 4 + 1, 48000);
    assert(!ok && !exists("invalid.wav"));
    ok = write("invalid.wav", frame, length, 32000);
    assert(!ok && !exists("invalid.wav"));
    ok = write("invalid.wav", frame, 0, 48000);
    assert(!ok && !exists("invalid.wav"));
    frame[length] = 0;
    ok = write("invalid.wav", frame, length + 1, 48000);
    assert(!ok && !exists("invalid.wav"));
    frame[length - 1] ^= 1;
    ok = write("invalid.wav", frame, length, 48000);
    assert(!ok && !exists("invalid.wav"));
  }
  {
    /* An actual wire span this large would exceed RIFF's 32-bit byte size at
     * 96 kHz. Init borrows it; rejection must precede rendering and opening. */
    const size_t size = 64u * 1024u * 1024u;
    uint8_t *wire = (uint8_t *)calloc(size, 1);
    assert(wire);
    reset();
    ok = write_stereo_wav("too-large.wav", wire, size, 96000);
    assert(!ok && !exists("too-large.wav") && !failures[IO_OPEN].calls &&
           !failures[IO_RENDER].calls);
    free(wire);
  }
}
static size_t make_report_frame33(uint8_t *frame, size_t capacity) {
  const uint8_t settings[] = {0x10, 0x20};
  const uint8_t pattern[] = {0x30, 0x40};
  const uint8_t sound[] = {0x50, 0x60, 0x70};
  const uint8_t unknown_a[] = {0x80, 0x90};
  const uint8_t unknown_b[] = {0xa0};
  const uint8_t unknown_c[] = {0xb0, 0xc0};
  po33_builder_t builder;
  uint8_t pattern_selector = 2, sound_selector = 7;
  size_t length = 0;
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  assert(po33_builder_append_packet(&builder, PO33_TAG_SETTINGS, settings, sizeof(settings),
                                    NULL) == PO_OK);
  assert(po33_builder_append_packet(&builder, PO33_TAG_PATTERN_SELECT, &pattern_selector,
                                    sizeof(pattern_selector), NULL) == PO_OK);
  assert(po33_builder_append_packet(&builder, PO33_TAG_PATTERN_DATA, pattern, sizeof(pattern),
                                    NULL) == PO_OK);
  assert(po33_builder_append_packet(&builder, PO33_TAG_SOUND_SELECT, &sound_selector,
                                    sizeof(sound_selector), NULL) == PO_OK);
  assert(po33_builder_append_packet(&builder, PO33_TAG_SOUND_DATA, sound, sizeof(sound), NULL) ==
         PO_OK);
  assert(po33_builder_append_packet(&builder, 0x1234, unknown_a, sizeof(unknown_a), NULL) == PO_OK);
  assert(po33_builder_append_packet(&builder, 0x1234, unknown_b, sizeof(unknown_b), NULL) == PO_OK);
  assert(po33_builder_append_packet(&builder, 0x0001, unknown_c, sizeof(unknown_c), NULL) == PO_OK);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  return length;
}
static size_t make_report_frame35(uint8_t *frame, size_t capacity) {
  const uint8_t pattern[] = {4};
  const uint8_t speech_header[PO35_SPEECH_HEADER_BYTES] = {3, 0, 0, 80, 140, 1, 2, 3, 4};
  const uint8_t speech_data[] = {0x10, 0x20, 0x30, 0x40};
  const uint8_t unknown[] = {0x50, 0x60};
  po35_builder_t builder;
  size_t length = 0;
  assert(po35_builder_init(&builder, frame, capacity) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_PATTERN_EMPTY, pattern, sizeof(pattern),
                                    NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_SPEECH_HEADER, speech_header,
                                    sizeof(speech_header), NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_SPEECH_DATA, speech_data,
                                    sizeof(speech_data), NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_SPEECH_END, NULL, 0, NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, 0xabcd, unknown, sizeof(unknown), NULL) == PO_OK);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  return length;
}
static void report_string(const po_frame_report_t *report, int json, char *out, size_t capacity) {
  FILE *fp = tmpfile();
  long position;
  size_t length, count;
  int status;
  assert(fp && capacity);
  status = json ? po_frame_report_write_json(fp, report) : po_frame_report_write_text(fp, report);
  assert(status);
  status = fflush(fp);
  assert(status == 0);
  status = fseek(fp, 0, SEEK_END);
  assert(status == 0);
  position = ftell(fp);
  assert(position >= 0);
  length = (size_t)position;
  assert(length < capacity);
  status = fseek(fp, 0, SEEK_SET);
  assert(status == 0);
  count = fread(out, 1, length, fp);
  assert(count == length);
  out[length] = '\0';
  status = fclose(fp);
  assert(status == 0);
}
static void test_frame_reports(void) {
  uint8_t frame[8192];
  char json[2048], text[2048];
  po_frame_report_t report = {0};
  size_t length = make_report_frame33(frame, sizeof(frame));
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO33, frame, length, &report));
  assert(report.family == PO_FRAME_FAMILY_PO33 && report.parser_status == PO_OK &&
         report.complete && !report.stopped && report.input_bytes == length &&
         report.consumed_bytes == length && !report.trailing_bytes && !report.unparsed_bytes &&
         report.packet_count == 10);
  assert(report.selectors[PO_FRAME_SELECTOR_PATTERN].occurrences[2] == 1 &&
         report.selectors[PO_FRAME_SELECTOR_SOUND].occurrences[7] == 1 &&
         report.selectors[PO_FRAME_SELECTOR_SPEECH].occurrences[3] == 0);
  assert(report.unknown_packet_count == 3 && report.unknown_payload_bytes == 5 &&
         report.unknown_tag_count == 2 && report.unknown_tags[0].tag_code == 0x0001 &&
         report.unknown_tags[0].packet_count == 1 && report.unknown_tags[0].payload_bytes == 2 &&
         report.unknown_tags[1].tag_code == 0x1234 && report.unknown_tags[1].packet_count == 2 &&
         report.unknown_tags[1].payload_bytes == 3);
  report_string(&report, 1, json, sizeof(json));
  report_string(&report, 0, text, sizeof(text));
  assert(!strcmp(
      json,
      "{\"family\":\"po33\",\"parser_status\":\"ok\",\"complete\":true,\"stopped\":false,"
      "\"packet_count\":10,\"input_bytes\":4160,\"consumed_bytes\":4160,\"trailing_bytes\":0,"
      "\"unparsed_bytes\":0,\"final_state\":\"0x0000\",\"selectors\":{\"patterns\":[{\"value\":2,"
      "\"occurrences\":1}],\"sounds\":[{\"value\":7,\"occurrences\":1}],\"speech\":[]},"
      "\"invalid_selectors\":{\"patterns\":0,\"sounds\":0,\"speech\":0},\"unknown_packets\":{"
      "\"count\":3,\"payload_bytes\":5,\"tags\":[{\"tag\":\"0x0001\",\"packets\":1,"
      "\"payload_bytes\":2},{\"tag\":\"0x1234\",\"packets\":2,\"payload_bytes\":3}]}}\n"));
  assert(!strcmp(
      text, "po33 parser=ok complete=yes stopped=no packets=10 input=4160 consumed=4160 trailing=0 "
            "unparsed=0 state=0x0000 selectors=patterns[2x1];sounds[7x1];speech[-];invalid=0/0/0 "
            "unknown=packets:3,bytes:5,tags[0x0001x1/2,0x1234x2/3]\n"));
  po_frame_report_destroy(&report);

  frame[length] = 0xde;
  frame[length + 1] = 0xad;
  frame[length + 2] = 0xbe;
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO33, frame, length + 3, &report));
  assert(report.complete && report.consumed_bytes == length && report.trailing_bytes == 3 &&
         report.unparsed_bytes == 3);
  po_frame_report_destroy(&report);

  frame[length - 1] ^= 1;
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO33, frame, length, &report));
  assert(report.parser_status == PO_ERR_FRAME && !report.complete && !report.trailing_bytes &&
         report.unparsed_bytes == 5 && report.packet_count == 9);
  po_frame_report_destroy(&report);

  length = make_report_frame35(frame, sizeof(frame));
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO35, frame, length, &report));
  assert(report.family == PO_FRAME_FAMILY_PO35 && report.parser_status == PO_OK &&
         report.complete && report.packet_count == 5 &&
         report.selectors[PO_FRAME_SELECTOR_PATTERN].occurrences[4] == 1 &&
         report.selectors[PO_FRAME_SELECTOR_SPEECH].occurrences[3] == 1 &&
         report.unknown_tag_count == 1 && report.unknown_tags[0].tag_code == 0xabcd);
  report_string(&report, 1, json, sizeof(json));
  report_string(&report, 0, text, sizeof(text));
  assert(!strcmp(
      json,
      "{\"family\":\"po35\",\"parser_status\":\"ok\",\"complete\":true,\"stopped\":false,"
      "\"packet_count\":5,\"input_bytes\":4142,\"consumed_bytes\":4142,\"trailing_bytes\":0,"
      "\"unparsed_bytes\":0,\"final_state\":\"0xcc30\",\"selectors\":{\"patterns\":[{\"value\":4,"
      "\"occurrences\":1}],\"sounds\":[],\"speech\":[{\"value\":3,\"occurrences\":1}]},"
      "\"invalid_selectors\":{\"patterns\":0,\"sounds\":0,\"speech\":0},\"unknown_packets\":{"
      "\"count\":1,\"payload_bytes\":2,\"tags\":[{\"tag\":\"0xabcd\",\"packets\":1,"
      "\"payload_bytes\":2}]}}\n"));
  assert(!strcmp(
      text, "po35 parser=ok complete=yes stopped=no packets=5 input=4142 consumed=4142 trailing=0 "
            "unparsed=0 state=0xcc30 selectors=patterns[4x1];sounds[-];speech[3x1];invalid=0/0/0 "
            "unknown=packets:1,bytes:2,tags[0xabcdx1/2]\n"));
  po_frame_report_destroy(&report);

  frame[length] = 0xca;
  frame[length + 1] = 0xfe;
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO35, frame, length + 2, &report));
  assert(report.complete && report.consumed_bytes == length && report.trailing_bytes == 2 &&
         report.unparsed_bytes == 2);
  po_frame_report_destroy(&report);

  frame[length - 1] ^= 1;
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO35, frame, length, &report));
  assert(report.parser_status == PO_ERR_FRAME && !report.complete && !report.trailing_bytes &&
         report.unparsed_bytes == 5 && report.packet_count == 5);
  po_frame_report_destroy(&report);
}

static void test_frame_report_many_unknown_tags(void) {
  const size_t tag_count = 512u;
  const size_t capacity = PO33_PREAMBLE_BYTES + 5u + tag_count * 5u + 5u;
  po33_builder_t builder;
  po_frame_report_t report = {0};
  uint8_t *frame = (uint8_t *)malloc(capacity);
  size_t length = 0u;

  assert(frame != NULL);
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  for (size_t index = 0u; index < tag_count; ++index)
    assert(po33_builder_append_packet(&builder, (uint16_t)(0x4000u + index), NULL, 0u, NULL) ==
           PO_OK);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  assert(po_frame_report_collect(PO_FRAME_FAMILY_PO33, frame, length, &report));
  assert(report.unknown_tag_count == tag_count && report.unknown_packet_count == tag_count &&
         report.unknown_payload_bytes == 0u);
  for (size_t index = 0u; index < tag_count; ++index)
    assert(report.unknown_tags[index].tag_code == (uint16_t)(0x4000u + index) &&
           report.unknown_tags[index].packet_count == 1u &&
           report.unknown_tags[index].payload_bytes == 0u);
  po_frame_report_destroy(&report);
  free(frame);
}

static void doctor_load_file(const char *path, uint8_t **out, size_t *out_size) {
  FILE *file;
  long end;
  size_t size, count;
  uint8_t *bytes;
  int status;

  assert(path != NULL && out != NULL && out_size != NULL);
  *out = NULL;
  *out_size = 0;
  file = fopen(path, "rb");
  assert(file != NULL);
  status = fseek(file, 0, SEEK_END);
  assert(status == 0);
  end = ftell(file);
  assert(end >= 0);
  size = (size_t)end;
  status = fseek(file, 0, SEEK_SET);
  assert(status == 0);
  bytes = (uint8_t *)malloc(size ? size : 1u);
  assert(bytes != NULL);
  count = size ? fread(bytes, 1, size, file) : 0u;
  assert(size == 0u || count == size);
  status = fclose(file);
  assert(status == 0);
  *out = bytes;
  *out_size = size;
}

static void doctor_read_text(const char *path, char *out, size_t capacity) {
  uint8_t *bytes;
  size_t size;

  assert(capacity != 0u);
  doctor_load_file(path, &bytes, &size);
  assert(size < capacity);
  memcpy(out, bytes, size);
  out[size] = '\0';
  free(bytes);
}

static int doctor_run_cli(int (*entry)(int, char **), int argc, char **argv, char *stdout_text,
                          size_t stdout_capacity, char *stderr_text, size_t stderr_capacity) {
  const char *stdout_path = "doctor-stdout.txt";
  const char *stderr_path = "doctor-stderr.txt";
  FILE *stdout_file = fopen(stdout_path, "wb");
  FILE *stderr_file = fopen(stderr_path, "wb");
  int stdout_fd, stderr_fd, stdout_target, stderr_target, rc, status;

  assert(stdout_file != NULL && stderr_file != NULL);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  stdout_fd = dup(STDOUT_FILENO);
  stderr_fd = dup(STDERR_FILENO);
  assert(stdout_fd >= 0 && stderr_fd >= 0);
  stdout_target = fileno(stdout_file);
  stderr_target = fileno(stderr_file);
  assert(stdout_target >= 0 && stderr_target >= 0);
  status = dup2(stdout_target, STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(stderr_target, STDERR_FILENO);
  assert(status == STDERR_FILENO);
  rc = entry(argc, argv);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  status = dup2(stdout_fd, STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(stderr_fd, STDERR_FILENO);
  assert(status == STDERR_FILENO);
  status = close(stdout_fd);
  assert(status == 0);
  status = close(stderr_fd);
  assert(status == 0);
  status = fclose(stdout_file);
  assert(status == 0);
  status = fclose(stderr_file);
  assert(status == 0);
  doctor_read_text(stdout_path, stdout_text, stdout_capacity);
  doctor_read_text(stderr_path, stderr_text, stderr_capacity);
  return rc;
}

static void doctor_swap_pcm16_stereo(const char *source, const char *destination) {
  uint8_t *bytes;
  size_t size;

  doctor_load_file(source, &bytes, &size);
  assert(size >= 44u && memcmp(bytes, "RIFF", 4u) == 0 && memcmp(bytes + 8u, "WAVE", 4u) == 0 &&
         memcmp(bytes + 36u, "data", 4u) == 0 && (size - 44u) % 4u == 0u);
  for (size_t offset = 44u; offset < size; offset += 4u) {
    uint8_t left0 = bytes[offset];
    uint8_t left1 = bytes[offset + 1u];
    bytes[offset] = bytes[offset + 2u];
    bytes[offset + 1u] = bytes[offset + 3u];
    bytes[offset + 2u] = left0;
    bytes[offset + 3u] = left1;
  }
  save(destination, bytes, size);
  free(bytes);
}

static void doctor_remove_pcm16_preamble(const char *source, const char *destination) {
  uint8_t *bytes;
  size_t size;

  doctor_load_file(source, &bytes, &size);
  assert(size >= 44u && memcmp(bytes, "RIFF", 4u) == 0 && memcmp(bytes + 8u, "WAVE", 4u) == 0 &&
         memcmp(bytes + 36u, "data", 4u) == 0);
  memset(bytes + 44u, 0, size - 44u);
  save(destination, bytes, size);
  free(bytes);
}

static size_t doctor_count_text(const char *text, const char *needle) {
  size_t count = 0;
  const char *position = text;

  assert(text != NULL && needle != NULL && needle[0] != '\0');
  while ((position = strstr(position, needle)) != NULL) {
    ++count;
    position += strlen(needle);
  }
  return count;
}

static void test_capture_doctor(void) {
  uint8_t frame33[8192];
  uint8_t frame35[8192];
  uint8_t corrupt[8192];
  char stdout_text[16384];
  char stdout_again[16384];
  char stderr_text[1024];
  size_t frame33_size = make_frame(frame33, sizeof(frame33), 0);
  size_t frame35_size = make_frame(frame35, sizeof(frame35), 1);
  int rc;
  char *po33_text[] = {"po_capture_doctor", "doctor-po33.wav", NULL};
  char *po35_json[] = {"po_capture_doctor", "--json", "doctor-po35.wav", NULL};
  char *swapped_text[] = {"po_capture_doctor", "doctor-swapped.wav", NULL};
  char *no_preamble_text[] = {"po_capture_doctor", "doctor-no-preamble.wav", NULL};
  char *corrupt_text[] = {"po_capture_doctor", "doctor-corrupt.wav", NULL};
  char *malformed_text[] = {"po_capture_doctor", "doctor-malformed.wav", NULL};

  assert(po33_write_transfer_wav("doctor-po33.wav", frame33, frame33_size, 48000));
  assert(po35_write_transfer_wav("doctor-po35.wav", frame35, frame35_size, 48000));

  rc = doctor_run_cli(po_capture_doctor_example_main, 2, po33_text, stdout_text,
                      sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(
      strstr(stdout_text, "capture status=ok format=pcm16 channels=2 sample_rate=48000 frames=") ==
      stdout_text);
  assert(strstr(stdout_text, "selection=none\n") != NULL);
  assert(strstr(stdout_text,
                "candidate channel_order=normal transport=recovered recovered_bytes=") != NULL);
  assert(strstr(stdout_text, "  po33 parser=ok complete=yes") != NULL);
  assert(strstr(stdout_text, "candidate channel_order=swapped transport=not-recovered") != NULL);
  rc = doctor_run_cli(po_capture_doctor_example_main, 2, po33_text, stdout_again,
                      sizeof(stdout_again), stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0' && strcmp(stdout_text, stdout_again) == 0);

  rc = doctor_run_cli(po_capture_doctor_example_main, 3, po35_json, stdout_text,
                      sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(strstr(stdout_text, "{\"capture\":{\"status\":\"ok\",\"format\":\"pcm16\",\"channels\":2,"
                             "\"sample_rate\":48000,") == stdout_text);
  assert(strstr(stdout_text, "\"selection\":\"none\"") != NULL);
  assert(strstr(stdout_text, "\"channel_order\":\"normal\",\"transport\":\"recovered\"") != NULL);
  assert(strstr(stdout_text, "\"family\":\"po35\",\"parser_status\":\"ok\",\"complete\":true") !=
         NULL);

  doctor_swap_pcm16_stereo("doctor-po33.wav", "doctor-swapped.wav");
  rc = doctor_run_cli(po_capture_doctor_example_main, 2, swapped_text, stdout_text,
                      sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(strstr(stdout_text, "candidate channel_order=normal transport=not-recovered") != NULL);
  assert(strstr(stdout_text,
                "candidate channel_order=swapped transport=recovered recovered_bytes=") != NULL);
  assert(strstr(stdout_text, "  po33 parser=ok complete=yes") != NULL);

  doctor_remove_pcm16_preamble("doctor-po33.wav", "doctor-no-preamble.wav");
  rc = doctor_run_cli(po_capture_doctor_example_main, 2, no_preamble_text, stdout_text,
                      sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(doctor_count_text(stdout_text, "transport=not-recovered") == 2u);
  assert(doctor_count_text(stdout_text, "parser=not-run complete=no") == 4u);

  memcpy(corrupt, frame33, frame33_size);
  corrupt[frame33_size - 1u] ^= 1u;
  assert(write_stereo_wav("doctor-corrupt.wav", corrupt, frame33_size, 48000));
  rc = doctor_run_cli(po_capture_doctor_example_main, 2, corrupt_text, stdout_text,
                      sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(strstr(stdout_text,
                "candidate channel_order=normal transport=recovered recovered_bytes=") != NULL);
  assert(strstr(stdout_text, "  po33 parser=frame complete=no") != NULL);

  save("doctor-malformed.wav", "not a WAV", sizeof("not a WAV") - 1u);
  rc = doctor_run_cli(po_capture_doctor_example_main, 2, malformed_text, stdout_text,
                      sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 1 && stderr_text[0] == '\0');
  assert(strcmp(stdout_text, "capture status=unreadable\nselection=none\n") == 0);
}

int main(void) {
  char directory[128];
  int made = 0, status;
  for (unsigned i = 0; i < 100 && !made; ++i) {
    snprintf(directory, sizeof(directory), "/tmp/libpo-capture-io-%ld-%u", (long)getpid(), i);
    made = mkdir(directory, 0700) == 0;
    assert(made || errno == EEXIST);
  }
  assert(made);
  status = chdir(directory);
  assert(status == 0);
  test_reader_errors();
  test_malformed_chunks();
  test_roundtrip_and_decode_errors();
  test_writer_failures();
  test_writer_validation();
  test_frame_reports();
  test_frame_report_many_unknown_tags();
  test_capture_doctor();
  puts("Shared capture I/O and WAV failure tests passed");
  return 0;
}
