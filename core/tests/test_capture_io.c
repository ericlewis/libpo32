/* Exercise hosted I/O errors without production fault switches or fake DSP state. */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po_capture.h"
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
  puts("Shared capture I/O and WAV failure tests passed");
  return 0;
}
