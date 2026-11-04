/* Host failure injection is confined to this translation unit. The executable
 * tests below also exercise the unmodified, separately linked command lines. */
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po35.h"
#include "po35_analysis.h"
#include "po35_synth.h"
#include "po_capture.h"
#include "po_stereo_wav.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum failure { ALLOC, ZERO_ALLOC, OPEN, CLOSE, WRITE, PRINT, SEEK, TELL, READ, ERROR, FAILURES };
static struct {
  size_t calls, fail_at;
} faults[FAILURES];
static int fail(enum failure kind) {
  ++faults[kind].calls;
  if (faults[kind].calls != faults[kind].fail_at)
    return 0;
  errno = EIO;
  return 1;
}
static void reset_faults(void) {
  memset(faults, 0, sizeof(faults));
}
static void *host_malloc(size_t size) {
  return fail(ALLOC) ? NULL : malloc(size);
}
static void *host_calloc(size_t count, size_t size) {
  return fail(ZERO_ALLOC) ? NULL : calloc(count, size);
}
static FILE *host_fopen(const char *path, const char *mode) {
  return fail(OPEN) ? NULL : fopen(path, mode);
}
static int host_fclose(FILE *fp) {
  int injected = fail(CLOSE), status = fclose(fp);
  return injected ? EOF : status;
}
static size_t host_fwrite(const void *data, size_t size, size_t count, FILE *fp) {
  return size && count && fail(WRITE) ? 0 : fwrite(data, size, count, fp);
}
static int host_fprintf(FILE *fp, const char *format, ...) {
  int injected = fail(PRINT), status;
  va_list ap;
  if (injected)
    return -1;
  va_start(ap, format);
  status = vfprintf(fp, format, ap);
  va_end(ap);
  return status;
}
static int host_fseek(FILE *fp, long offset, int whence) {
  return fail(SEEK) ? -1 : fseek(fp, offset, whence);
}
static long host_ftell(FILE *fp) {
  return fail(TELL) ? -1L : ftell(fp);
}
static size_t host_fread(void *data, size_t size, size_t count, FILE *fp) {
  return fail(READ) ? 0 : fread(data, size, count, fp);
}
static int host_ferror(FILE *fp) {
  return fail(ERROR) ? 1 : ferror(fp);
}

#define malloc  host_malloc
#define calloc  host_calloc
#define fopen   host_fopen
#define fclose  host_fclose
#define fwrite  host_fwrite
#define fprintf host_fprintf
#define fseek   host_fseek
#define ftell   host_ftell
#define fread   host_fread
#define ferror  host_ferror
#define main    po35_decode_main
#include "../examples/po35_decode_capture.c"
#undef main
#define main po35_encode_main
#include "../examples/po35_encode_capture.c"
#undef main
#undef malloc
#undef calloc
#undef fopen
#undef fclose
#undef fwrite
#undef fprintf
#undef fseek
#undef ftell
#undef fread
#undef ferror

static uint8_t frame[8192];
static size_t frame_size;
static int exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}
static void save(const char *path, const void *data, size_t size) {
  FILE *fp = fopen(path, "wb");
  size_t written;
  int closed;
  assert(fp);
  written = size ? fwrite(data, 1, size, fp) : 0;
  closed = fclose(fp);
  assert(written == size && closed == 0);
}
static void append(po35_builder_t *builder, uint16_t tag, const void *data, size_t size) {
  po_status_t status = po35_builder_append_packet(builder, tag, data, size, NULL);
  assert(status == PO_OK);
}
static void fixture(void) {
  po35_builder_t builder;
  uint8_t header[9] = {2, 0, 1, 80, 140, 1, 2, 3, 4};
  uint8_t record[44] = {0, 0, 20, 0};
  uint8_t settings[156] = {0}, pattern = 4;
  po_status_t status = po35_builder_init(&builder, frame, sizeof(frame));
  assert(status == PO_OK);
  append(&builder, PO35_TAG_SETTINGS, settings, sizeof(settings));
  append(&builder, PO35_TAG_PATTERN_EMPTY, &pattern, 1);
  for (unsigned i = 0; i < 2; ++i) {
    header[0] = (uint8_t)(i ? 5 : 2);
    append(&builder, PO35_TAG_SPEECH_HEADER, header, sizeof(header));
    append(&builder, PO35_TAG_SPEECH_DATA, record, sizeof(record));
    append(&builder, PO35_TAG_SPEECH_END, NULL, 0);
  }
  status = po35_builder_finish(&builder, &frame_size);
  assert(status == PO_OK);
}
static void remove_export(const char *directory) {
  /* Remove only the exact file names created by this fixture. */
  const char *names[] = {"frame.bin",     "payloads.bin",   "packets.tsv",
                         "settings.bin",  "pattern-04.bin", "speech-02-header.bin",
                         "speech-02.bin", "speech-02.wav",  "speech-05-header.bin",
                         "speech-05.bin", "speech-05.wav",  "all-speech.wav",
                         "summary.txt"};
  char path[256];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    int status;
    snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
    status = remove(path);
    assert(status == 0 || errno == ENOENT);
  }
  {
    int status = rmdir(directory);
    assert(status == 0);
  }
}
static void test_export_failures(void) {
  size_t calls[FAILURES];
  int ok;
  fixture();
  reset_faults();
  ok = d35_write_outputs("baseline", frame, frame_size, 48000, PO_STEREO_CHANNEL_NORMAL,
                         PO35_SYNTH_BANK_HIGH);
  assert(ok);
  for (unsigned i = 0; i < FAILURES; ++i)
    calls[i] = faults[i].calls;
  remove_export("baseline");
  /* Every allocation/open/write/flush point must fail without a partial export. */
  for (unsigned kind = ALLOC; kind <= PRINT; ++kind)
    for (size_t nth = 1; nth <= calls[kind]; ++nth) {
      reset_faults();
      faults[kind].fail_at = nth;
      ok = d35_write_outputs("failed-export", frame, frame_size, 48000, PO_STEREO_CHANNEL_NORMAL,
                             PO35_SYNTH_BANK_HIGH);
      assert(!ok && faults[kind].calls >= nth && !exists("failed-export"));
    }
  reset_faults();
}
static void test_reader_failures(void) {
  const uint8_t data[] = {1, 2, 3, 4};
  uint8_t *bytes = NULL;
  size_t size = 0;
  int ok;
  const enum failure kinds[] = {OPEN, ALLOC, CLOSE, SEEK, TELL, READ, ERROR};
  save("read.bin", data, sizeof(data));
  ok = e35_read_file("read.bin", &bytes, &size);
  assert(ok && size == sizeof(data) && !memcmp(data, bytes, size));
  free(bytes);
  for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
    bytes = NULL;
    size = 123;
    reset_faults();
    faults[kinds[i]].fail_at = 1;
    ok = e35_read_file("read.bin", &bytes, &size);
    assert(!ok && bytes == NULL && size == 123 && faults[kinds[i]].calls >= 1);
  }
  reset_faults();
  faults[SEEK].fail_at = 2;
  ok = e35_read_file("read.bin", &bytes, &size);
  assert(!ok && !bytes && size == 123);
  reset_faults();
  save("empty.bin", NULL, 0);
  ok = e35_read_file("empty.bin", &bytes, &size);
  assert(ok && bytes && size == 0);
  free(bytes);
}
static po_packet_t packet(uint16_t tag, const void *data, size_t size) {
  po_packet_t p = {0};
  p.tag_code = tag;
  p.payload_len = size;
  if (size)
    memcpy(p.payload, data, size);
  return p;
}
static void test_collection_validation(void) {
  uint8_t header[9] = {0, 0, 1, 80}, record[45] = {0}, settings[156] = {0};
  const uint16_t interleaved[] = {PO35_TAG_SPEECH_HEADER, PO35_TAG_SETTINGS, PO35_TAG_PATTERN,
                                  PO35_TAG_PATTERN_EMPTY, PO35_TAG_PATCH,    PO35_TAG_TABLE_7BE7,
                                  PO35_TAG_TABLE_1C7F};
  int status;
  for (unsigned test = 0; test < 11; ++test) {
    d35_collection_t c = {0};
    po_packet_t p;
    c.active = -1;
    if (test == 0)
      p = packet(PO35_TAG_SPEECH_HEADER, header, 8);
    else if (test == 1) {
      header[0] = PO35_SPEECH_SLOT_COUNT;
      p = packet(PO35_TAG_SPEECH_HEADER, header, 9);
      header[0] = 0;
    } else if (test == 2)
      p = packet(PO35_TAG_SPEECH_DATA, record, 1);
    else if (test == 3)
      p = packet(PO35_TAG_SPEECH_END, NULL, 0);
    else if (test == 4)
      p = packet(PO35_TAG_SETTINGS, settings, 155);
    else if (test == 5 || test == 6) {
      uint8_t index = test == 5 ? 0 : 16;
      p = packet(PO35_TAG_PATTERN_EMPTY, &index, test == 5 ? 0 : 1);
    } else {
      p = packet(PO35_TAG_SPEECH_HEADER, header, 9);
      status = d35_collect(&p, &c);
      assert(status == 0);
      if (test == 7)
        p = packet(PO35_TAG_SPEECH_DATA, NULL, 0);
      if (test == 8)
        p = packet(PO35_TAG_SPEECH_DATA, record, 45);
      if (test == 9)
        p = packet(PO35_TAG_SPEECH_END, record, 1);
      if (test == 10)
        p = packet(PO35_TAG_SPEECH_END, NULL, 0);
    }
    status = d35_collect(&p, &c);
    assert(status == 1 && c.failed);
    d35_free_collection(&c);
  }
  for (size_t i = 0; i < sizeof(interleaved) / sizeof(interleaved[0]); ++i) {
    d35_collection_t c = {0};
    po_packet_t p = packet(interleaved[i], NULL, 0);
    status = d35_collect(&p, &c);
    assert(status == 1 && c.failed);
  }
  for (unsigned i = 0; i < 2; ++i) {
    d35_collection_t c = {0};
    uint8_t index = 2;
    po_packet_t p =
        i ? packet(PO35_TAG_SETTINGS, settings, 156) : packet(PO35_TAG_PATTERN_EMPTY, &index, 1);
    c.active = -1;
    status = d35_collect(&p, &c);
    assert(!status);
    status = d35_collect(&p, &c);
    assert(status && c.failed);
  }
}
static void test_host_helpers(void) {
  d35_outputs_t outputs = {0};
  FILE *fp;
  int ok, status;
  float invalid[] = {NAN, -2.0f, 2.0f};
  float samples[] = {-1.0f, 0.0f, 1.0f};
  char *path;
  outputs.directory = "helpers";
  status = mkdir(outputs.directory, 0700);
  assert(status == 0);
  outputs.count = 80;
  fp = d35_open(&outputs, "x");
  assert(!fp);
  outputs.count = 0;
  fp = d35_open(&outputs, "this-filename-exceeds-thirty-one-bytes.bin");
  assert(!fp);
  path = d35_path("", "");
  assert(path && !strcmp(path, "/"));
  free(path);
  ok = d35_write_wav(&outputs, "too-large.wav", NULL, (size_t)UINT32_MAX, 24000);
  assert(!ok && outputs.count == 0);
  for (size_t i = 0; i < 3; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "invalid-%zu.wav", i);
    ok = d35_write_wav(&outputs, name, &invalid[i], 1, 24000);
    assert(!ok);
  }
  ok = d35_write_wav(&outputs, "valid.wav", samples, 3, 24000);
  assert(ok);
  fp = d35_open(&outputs, "valid.wav");
  assert(!fp); /* An existing output is never truncated. */
  reset_faults();
  faults[ALLOC].fail_at = 1;
  d35_remove_outputs(&outputs);
  assert(exists("helpers/invalid-0.wav"));
  reset_faults();
  d35_remove_outputs(&outputs);
  assert(!exists("helpers"));
  {
    d35_collection_t c = {0};
    float *all = NULL;
    size_t count = 77;
    uint32_t rate = 0;
    ok = d35_render(&c, (po35_synth_bank_t)99, &all, &count, &rate);
    assert(!ok && all == NULL && count == 77);
    c.slots[0].seen = 1;
    c.slots[0].header.record_count = 1;
    c.slots[0].header.records_per_second = 1;
    c.slots[0].records = (uint8_t *)calloc(44, 1);
    assert(c.slots[0].records);
    c.slots[0].record_bytes = 44;
    ok = d35_render(&c, PO35_SYNTH_BANK_HIGH, &all, &count, &rate);
    assert(!ok && !all);
    c.slots[0].header.records_per_second = 20;
    /* A valid extreme coefficient bank can fail numerically. The host must
     * reject the partial render and release its allocation. */
    c.slots[0].records[1] = 6;
    c.slots[0].records[2] = 128;
    memset(c.slots[0].records + 14, 127, 30);
    ok = d35_render(&c, PO35_SYNTH_BANK_HIGH, &all, &count, &rate);
    assert(!ok && !all);
    d35_free_collection(&c);
    memset(&c, 0, sizeof(c));
    c.slots[0].seen = 1;
    c.slots[0].header.record_count = 1;
    c.slots[0].header.records_per_second = 80;
    c.slots[0].records = (uint8_t *)calloc(44, 1);
    assert(c.slots[0].records);
    c.slots[0].record_bytes = 44;
    ok = d35_render(&c, PO35_SYNTH_BANK_HIGH, &all, &count, &rate);
    assert(ok && all && count == 300 && rate == 24000);
    for (size_t i = 0; i < count; ++i)
      assert(all[i] == 0.0f); /* Zero-energy records need no normalization. */
    free(all);
    d35_free_collection(&c);
  }
}
static void test_numbers_and_options(void) {
  const char *invalid[] = {NULL, "", "-1", "+1", "x", "1x", "0x", "999999999999999999999999"};
  unsigned long value = 123;
  e35_options_t options;
  int ok;
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    ok = e35_number(invalid[i], 100, &value);
    assert(!ok && value == 123);
  }
  ok = e35_number("0x10", 100, &value);
  assert(ok && value == 16);
  ok = e35_number("0X10", 100, &value);
  assert(ok && value == 16);
  ok = e35_number("101", 100, &value);
  assert(!ok && value == 16);
  {
    char *args[] = {"encode", "frame", "out", "--speech", "0", "audio", "--fps", "60"};
    ok = e35_parse_options(8, args, &options);
    assert(ok && options.fps == 60 && options.edit_count == 1);
  }
}
static void test_rebuild_failures(void) {
  uint8_t storage[8192], header[9] = {0}, records[264] = {0};
  e35_options_t options = {0};
  po_packet_t p = packet(PO35_TAG_SPEECH_HEADER, header, sizeof(header));
  options.edits[0].path = "replacement";
  options.edits[0].records = records;
  options.edits[0].bytes = sizeof(records);
  options.fps = 60;
  for (unsigned i = 0; i < 3; ++i) {
    e35_rebuild_t rebuild = {0};
    po_status_t status = po35_builder_init(&rebuild.builder, storage, sizeof(storage));
    int stopped;
    assert(status == PO_OK);
    rebuild.active = -1;
    rebuild.options = i == 2 ? NULL : &options;
    rebuild.builder.capacity = rebuild.builder.length + (i == 1 ? 14 : 0);
    stopped = e35_rebuild_packet(&p, &rebuild);
    assert(stopped == 1 && rebuild.failed);
  }
  {
    const uint16_t tags[] = {PO35_TAG_SETTINGS, PO35_TAG_PATTERN,    PO35_TAG_PATTERN_EMPTY,
                             PO35_TAG_PATCH,    PO35_TAG_TABLE_7BE7, PO35_TAG_TABLE_1C7F};
    options.edits[0].path = NULL;
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
      e35_scan_t scan = {0};
      int stopped;
      scan.options = &options;
      p = packet(tags[i], NULL, 0);
      stopped = e35_scan_packet(&p, &scan);
      assert(stopped && scan.failed);
    }
    {
      e35_scan_t scan = {0};
      int stopped;
      scan.options = &options;
      p = packet(0x1234, NULL, 0);
      stopped = e35_scan_packet(&p, &scan);
      assert(!stopped && !scan.failed && scan.active == 0);
    }
  }
}

static int run(char *const args[]) {
  pid_t child = fork();
  int status;
  assert(child >= 0);
  if (!child) {
    execv(args[0], args);
    _exit(127);
  }
  child = waitpid(child, &status, 0);
  assert(child >= 0 && WIFEXITED(status));
  return WEXITSTATUS(status);
}
static void test_real_commands(char *decoder, char *encoder) {
  char *encode[] = {encoder, "frame.bin", "transfer.wav", NULL};
  char *decode[] = {decoder, "transfer.wav", "decoded", NULL};
  char *missing[] = {encoder, "missing.bin", "missing.wav", NULL};
  uint8_t *bytes = NULL;
  size_t count;
  int status, ok;
  fixture();
  save("frame.bin", frame, frame_size);
  status = run(missing);
  assert(status == 1 && !exists("missing.wav"));
  status = run(encode);
  assert(status == 0);
  ok = e35_read_file("transfer.wav", &bytes, &count);
  assert(ok);
  {
    uint8_t *after = NULL;
    size_t after_count = 0;
    status = run(encode);
    assert(status == 1);
    ok = e35_read_file("transfer.wav", &after, &after_count);
    assert(ok && count == after_count && !memcmp(bytes, after, count));
    free(after);
    free(bytes);
    bytes = NULL;
  }
  status = run(decode);
  assert(status == 0);
  ok = e35_read_file("decoded/frame.bin", &bytes, &count);
  assert(ok && count == frame_size && !memcmp(bytes, frame, count));
  free(bytes);
  remove_export("decoded");
  {
    char *bad_args[][10] = {
        {decoder, NULL},
        {decoder, "transfer.wav", "bad", "--bank", "invalid", NULL},
        {decoder, "transfer.wav", "bad", "--bank", NULL},
        {decoder, "transfer.wav", "bad", "--swap", "--swap", NULL},
        {decoder, "transfer.wav", "bad", "--bank", "high", "--bank", "low", NULL},
        {encoder, NULL},
        {encoder, "frame.bin", "bad", "--speech", NULL},
        {encoder, "frame.bin", "bad", "--speech", "x", "audio.wav", NULL},
        {encoder, "frame.bin", "bad", "--rate", NULL},
        {encoder, "frame.bin", "bad", "--rate", "48000", "--rate", "48000", NULL},
        {encoder, "frame.bin", "bad", "--rate", "96001", NULL},
        {encoder, "frame.bin", "bad", "--fps", NULL},
        {encoder, "frame.bin", "bad", "--fps", "80", "--fps", "80", NULL},
        {encoder, "frame.bin", "bad", "--fps", "x", NULL},
        {encoder, "frame.bin", "bad", "--channel", NULL},
        {encoder, "frame.bin", "bad", "--channel", "left", "--channel", "right", NULL},
        {encoder, "frame.bin", "bad", "--channel", "invalid", NULL},
        {encoder, "frame.bin", "bad", "--unknown", NULL}};
    for (size_t i = 0; i < sizeof(bad_args) / sizeof(bad_args[0]); ++i) {
      int argc = 0;
      while (bad_args[i][argc])
        ++argc;
      status = run(bad_args[i]);
      assert(status == 2 && !exists("bad"));
      status = bad_args[i][0] == decoder ? po35_decode_main(argc, bad_args[i])
                                         : po35_encode_main(argc, bad_args[i]);
      assert(status == 2 && !exists("bad"));
    }
  }
  {
    char *high[] = {decoder, "transfer.wav", "high", "--bank", "high", NULL};
    char *low[] = {decoder, "transfer.wav", "low", "--bank", "low", NULL};
    char *wrong[] = {decoder, "transfer.wav", "wrong", "--swap", NULL};
    char *absent[] = {decoder, "absent.wav", "absent", NULL};
    status = run(high);
    assert(status == 0);
    status = run(low);
    assert(status == 0);
    status = run(wrong);
    assert(status == 1 && !exists("wrong"));
    status = run(absent);
    assert(status == 1 && !exists("absent"));
    remove_export("high");
    remove_export("low");
  }
  {
    char *swapped[] = {decoder, "swapped.wav", "swapped", "--swap", NULL};
    ok = e35_read_file("transfer.wav", &bytes, &count);
    assert(ok && count > 44);
    for (size_t i = 44; i + 3 < count; i += 4) {
      uint8_t a = bytes[i], b = bytes[i + 1];
      bytes[i] = bytes[i + 2];
      bytes[i + 1] = bytes[i + 3];
      bytes[i + 2] = a;
      bytes[i + 3] = b;
    }
    save("swapped.wav", bytes, count);
    free(bytes);
    status = run(swapped);
    assert(status == 0);
    remove_export("swapped");
    swapped[3] = NULL;
    status = run(swapped);
    assert(status == 0);
    remove_export("swapped");
  }
  {
    char *invalid[] = {encoder, "invalid-frame.bin", "invalid-frame.wav", NULL};
    char *incomplete[] = {
        encoder, "invalid-frame.bin", "invalid-frame.wav", "--speech", "2", "speech.wav", NULL};
    po35_builder_t builder;
    uint8_t header[9] = {2, 0, 1, 80}, record[44] = {0};
    po_status_t built;
    size_t incomplete_size;
    for (unsigned i = 0; i < 3; ++i) {
      save("invalid-frame.bin", frame, i == 0 ? 0 : frame_size - 1);
      if (i == 2) {
        frame[frame_size] = 0;
        save("invalid-frame.bin", frame, frame_size + 1);
      }
      status = run(invalid);
      assert(status == 1 && !exists("invalid-frame.wav"));
      status = po35_encode_main(3, invalid);
      assert(status == 1 && !exists("invalid-frame.wav"));
    }
    built = po35_builder_init(&builder, frame, sizeof(frame));
    assert(built == PO_OK);
    append(&builder, PO35_TAG_SPEECH_HEADER, header, sizeof(header));
    append(&builder, PO35_TAG_SPEECH_DATA, record, sizeof(record));
    built = po35_builder_finish(&builder, &incomplete_size);
    assert(built == PO_OK);
    save("invalid-frame.bin", frame, incomplete_size);
    status = run(incomplete);
    assert(status == 1 && !exists("invalid-frame.wav"));
    status = po35_encode_main(6, incomplete);
    assert(status == 1 && !exists("invalid-frame.wav"));
    fixture();
  }
  {
    /* Transport decoding can succeed while the model's CRC validation fails. */
    char *invalid[] = {decoder, "bad-crc.wav", "bad-crc", NULL};
    po_stereo_modulator_t modulator;
    float *pcm;
    size_t samples, rendered;
    po_status_t encoded;
    ok = e35_read_file("transfer.wav", &bytes, &count);
    assert(ok && count > 44 && (count - 44) % 4 == 0);
    samples = (count - 44) / 4;
    pcm = (float *)malloc(samples * 2 * sizeof(float));
    assert(pcm);
    frame[frame_size - 1] ^= 1;
    encoded = po_stereo_modulator_init(&modulator, frame, frame_size, 48000);
    assert(encoded == PO_OK && po_stereo_modulator_remaining(&modulator) == samples);
    encoded = po_stereo_modulator_render_f32(&modulator, pcm, samples, &rendered);
    assert(encoded == PO_OK && rendered == samples);
    for (size_t i = 0; i < samples * 2; ++i) {
      uint16_t sample = (uint16_t)(int16_t)lround((double)pcm[i] * 32767.0);
      bytes[44 + 2 * i] = (uint8_t)sample;
      bytes[45 + 2 * i] = (uint8_t)(sample >> 8);
    }
    save("bad-crc.wav", bytes, count);
    free(pcm);
    free(bytes);
    bytes = NULL;
    status = run(invalid);
    assert(status == 1 && !exists("bad-crc"));
    status = po35_decode_main(3, invalid);
    assert(status == 1 && !exists("bad-crc"));
    fixture();
  }
  {
    char *edit[] = {encoder,      "frame.bin", "edited.wav", "--speech", "2",
                    "speech.wav", "--fps",     "60",         NULL};
    d35_outputs_t output = {0};
    float samples[400] = {0};
    output.directory = ".";
    ok = d35_write_wav(&output, "speech.wav", samples, 400, 8000);
    assert(ok);
    status = run(edit);
    assert(status == 0);
    {
      char *edited[] = {decoder, "edited.wav", "edited", NULL};
      status = run(edited);
      assert(status == 0);
      ok = e35_read_file("edited/speech-02-header.bin", &bytes, &count);
      assert(ok && count == 9 && bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 3 &&
             bytes[3] == 60 && bytes[4] == 140 && bytes[5] == 1 && bytes[6] == 2 && bytes[7] == 3 &&
             bytes[8] == 4);
      free(bytes);
      bytes = NULL;
      ok = e35_read_file("edited/pattern-04.bin", &bytes, &count);
      assert(ok && count == 1 && bytes[0] == 4);
      free(bytes);
      bytes = NULL;
      remove_export("edited");
    }
    {
      size_t allocations;
      edit[2] = "host-edited.wav";
      reset_faults();
      status = po35_encode_main(8, edit);
      assert(status == 0);
      allocations = faults[ALLOC].calls;
      status = remove(edit[2]);
      assert(status == 0);
      for (size_t nth = 1; nth <= allocations; ++nth) {
        reset_faults();
        faults[ALLOC].fail_at = nth;
        status = po35_encode_main(8, edit);
        assert(status == 1 && !exists(edit[2]) && faults[ALLOC].calls >= nth);
      }
      reset_faults();
    }
    edit[2] = "failed-edit.wav";
    edit[5] = "absent.wav";
    status = run(edit);
    assert(status == 1 && !exists("failed-edit.wav"));
    edit[4] = "3";
    status = run(edit);
    assert(status == 1 && !exists("failed-edit.wav"));
    frame[frame_size - 1] ^= 1;
    save("frame.bin", frame, frame_size);
    status = run(edit);
    assert(status == 1 && !exists("failed-edit.wav"));
    encode[2] = "invalid.wav";
    status = run(encode);
    assert(status == 1 && !exists("invalid.wav"));
  }
}
int main(int argc, char **argv) {
  char directory[128];
  int created = 0;
  int status;
  assert(argc == 3);
  for (unsigned i = 0; i < 100 && !created; ++i) {
    snprintf(directory, sizeof(directory), "/tmp/libpo35-host-%ld-%u", (long)getpid(), i);
    created = mkdir(directory, 0700) == 0;
    assert(created || errno == EEXIST);
  }
  assert(created);
  status = chdir(directory);
  assert(status == 0);
  test_export_failures();
  test_reader_failures();
  test_collection_validation();
  test_host_helpers();
  test_numbers_and_options();
  test_rebuild_failures();
  reset_faults();
  test_real_commands(argv[1], argv[2]);
  puts("PO-35 host failures and executable command lines passed");
  return 0;
}
