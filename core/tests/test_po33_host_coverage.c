#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Replay real workflows with one host resource failure. Parser and codec
 * calls remain real; these wrappers model allocation and file I/O errors. */
enum fault_kind { NO_FAULT, ALLOC, OPEN, READ, WRITE, PRINT, SEEK, TELL, CLOSE, GET, FAULTS };
static enum fault_kind fault;
static size_t calls[FAULTS], fail_at;
static FILE *read_error;
static int fail(enum fault_kind kind) {
  ++calls[kind];
  if (fault == kind && calls[kind] == fail_at) {
    errno = kind == ALLOC ? ENOMEM : EIO;
    return 1;
  }
  return 0;
}
static void reset_fault(enum fault_kind kind, size_t occurrence) {
  memset(calls, 0, sizeof(calls));
  fault = kind;
  fail_at = occurrence;
  read_error = NULL;
}
static void *host_malloc(size_t size) {
  return fail(ALLOC) ? NULL : malloc(size);
}
static void *host_calloc(size_t count, size_t size) {
  return fail(ALLOC) ? NULL : calloc(count, size);
}
static FILE *host_fopen(const char *path, const char *mode) {
  return fail(OPEN) ? NULL : fopen(path, mode);
}
static size_t host_fread(void *bytes, size_t size, size_t count, FILE *fp) {
  if (size && count && fail(READ)) {
    read_error = fp;
    return 0;
  }
  return fread(bytes, size, count, fp);
}
static size_t host_fwrite(const void *bytes, size_t size, size_t count, FILE *fp) {
  return size && count && fail(WRITE) ? 0 : fwrite(bytes, size, count, fp);
}
static int host_fprintf(FILE *fp, const char *format, ...) {
  va_list args;
  int result;
  if (fp != stderr && fp != stdout && fail(PRINT))
    return -1;
  va_start(args, format);
  result = vfprintf(fp, format, args);
  va_end(args);
  return result;
}
static int host_fseek(FILE *fp, long offset, int origin) {
  return fail(SEEK) ? -1 : fseek(fp, offset, origin);
}
static long host_ftell(FILE *fp) {
  return fail(TELL) ? -1 : ftell(fp);
}
static int host_fclose(FILE *fp) {
  int injected = fail(CLOSE);
  if (fp == read_error)
    read_error = NULL;
  int result = fclose(fp);
  return injected ? EOF : result;
}
static int host_fgetc(FILE *fp) {
  if (fail(GET)) {
    read_error = fp;
    return EOF;
  }
  return fgetc(fp);
}
static int host_ferror(FILE *fp) {
  return fp == read_error || ferror(fp);
}

#define malloc  host_malloc
#define calloc  host_calloc
#define fopen   host_fopen
#define fread   host_fread
#define fwrite  host_fwrite
#define fprintf host_fprintf
#define fseek   host_fseek
#define ftell   host_ftell
#define fclose  host_fclose
#define fgetc   host_fgetc
#define ferror  host_ferror
#define main    po33_capture_main
#include "../examples/po33_decode_capture.c"
#undef main
#define main po33_encode_main
#include "../examples/po33_encode_capture.c"
#undef main
#undef malloc
#undef calloc
#undef fopen
#undef fread
#undef fwrite
#undef fprintf
#undef fseek
#undef ftell
#undef fclose
#undef fgetc
#undef ferror

static char root[128], input[256], output[256], sample[256], pattern[256];
static uint8_t wire[32768];
static size_t wire_size;
static void path_for(char *out, size_t size, const char *name) {
  int count = snprintf(out, size, "%s/%s", root, name);
  assert(count > 0 && (size_t)count < size);
}
static void save(const char *path, const void *bytes, size_t size) {
  FILE *fp = fopen(path, "wb");
  assert(fp);
  size_t count = fwrite(bytes, 1, size, fp);
  int status = fclose(fp);
  assert(count == size && status == 0);
}
static void absent(const char *path) {
  struct stat info;
  int status = stat(path, &info);
  assert(status == -1 && errno == ENOENT);
}
static void remove_output(void) {
  DIR *directory = opendir(output);
  int status;
  if (!directory) {
    status = unlink(output);
    assert(status == 0 || errno == ENOENT);
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    char path[512];
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    int count = snprintf(path, sizeof(path), "%s/%s", output, entry->d_name);
    assert(count > 0 && (size_t)count < sizeof(path));
    status = unlink(path);
    assert(status == 0);
  }
  status = closedir(directory);
  assert(status == 0);
  status = rmdir(output);
  assert(status == 0);
}
static void append(po33_builder_t *builder, uint16_t tag, const uint8_t *bytes, size_t size) {
  po_status_t status = po33_builder_append_packet(builder, tag, bytes, size, NULL);
  assert(status == PO_OK);
}
/* More than one WAV read buffer; all malformed cases are CRC-valid frames. */
static void fixture(int malformed) {
  po33_builder_t builder;
  uint8_t bytes[255], index = 2;
  memset(bytes, 0x21, sizeof(bytes));
  po_status_t status = po33_builder_init(&builder, wire, sizeof(wire));
  assert(status == PO_OK);
  append(&builder, PO33_TAG_SETTINGS, bytes, 3);
  append(&builder, PO33_TAG_SETTINGS, bytes, 2);
  if (malformed != 1)
    append(&builder, PO33_TAG_PATTERN_SELECT, &index, malformed == 2 ? 0 : 1);
  append(&builder, PO33_TAG_PATTERN_DATA, bytes, 3);
  append(&builder, PO33_TAG_PATTERN_DATA, bytes, 2);
  if (malformed == 3) {
    append(&builder, PO33_TAG_PATTERN_SELECT, &index, 1);
    append(&builder, PO33_TAG_PATTERN_DATA, bytes, 1);
  }
  index = 7;
  if (malformed != 4)
    append(&builder, PO33_TAG_SOUND_SELECT, &index, malformed == 5 ? 0 : 1);
  size_t remaining = malformed == 6 ? 8 : 4105;
  while (remaining) {
    size_t n = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
    append(&builder, PO33_TAG_SOUND_DATA, bytes, n);
    remaining -= n;
  }
  if (malformed == 7) {
    append(&builder, PO33_TAG_SOUND_SELECT, &index, 1);
    append(&builder, PO33_TAG_SOUND_DATA, bytes, 10);
  }
  index = 8;
  append(&builder, PO33_TAG_SOUND_SELECT, &index, 1);
  append(&builder, PO33_TAG_SOUND_DATA, bytes, 9);
  append(&builder, 0x1234, bytes, 1);
  status = po33_builder_finish(&builder, &wire_size);
  assert(status == PO_OK);
}
static void sample_wav(void) {
  uint8_t wav[44 + 5000 * 2] = {0};
  memcpy(wav, "RIFF", 4);
  store32(wav + 4, sizeof(wav) - 8);
  memcpy(wav + 8, "WAVEfmt ", 8);
  store32(wav + 16, 16);
  store16(wav + 20, 1);
  store16(wav + 22, 1);
  store32(wav + 24, 23438);
  store32(wav + 28, 46876);
  store16(wav + 32, 2);
  store16(wav + 34, 16);
  memcpy(wav + 36, "data", 4);
  store32(wav + 40, sizeof(wav) - 44);
  for (size_t i = 44; i < sizeof(wav); i += 2)
    store16(wav + i, 8192);
  save(sample, wav, sizeof(wav));
}
static int encode(void) {
  char *args[] = {"encode", input, output, "--sample", "7", sample, "--pattern", "2", pattern};
  return po33_encode_main(9, args);
}
static void faults(void) {
  size_t observed[FAULTS];
  fixture(0);
  save(input, wire, wire_size);
  sample_wav();
  save(pattern, "abcde", 5);
  reset_fault(NO_FAULT, 0);
  int result = encode();
  assert(result == 0);
  memcpy(observed, calls, sizeof(observed));
  remove_output();
  for (enum fault_kind kind = ALLOC; kind < FAULTS; ++kind)
    for (size_t n = 1; n <= observed[kind]; ++n) {
      reset_fault(kind, n);
      result = encode();
      assert(calls[kind] >= n && result != 0);
      absent(output);
    }
  reset_fault(NO_FAULT, 0);
  result = write_outputs(output, wire, wire_size, 48000);
  assert(result == 1);
  memcpy(observed, calls, sizeof(observed));
  remove_output();
  for (enum fault_kind kind = ALLOC; kind < FAULTS; ++kind)
    for (size_t n = 1; n <= observed[kind]; ++n) {
      reset_fault(kind, n);
      result = write_outputs(output, wire, wire_size, 48000);
      if (calls[kind] < n || result != 0)
        fprintf(stderr, "Decoder accepted fault %d at operation %zu (calls %zu).\n", kind, n,
                calls[kind]);
      assert(calls[kind] >= n && result == 0);
      remove_output();
    }
  reset_fault(NO_FAULT, 0);
}
static void malformed(void) {
  for (int variant = 1; variant <= 7; ++variant) {
    fixture(variant);
    int status = write_outputs(output, wire, wire_size, 48000);
    assert(status == 0);
    remove_output();
  }
  /* Valid transport can contain a malformed semantic selector. */
  fixture(2);
  save(input, wire, wire_size);
  char *args[] = {"encode", input, output, "--pattern-byte", "2", "0", "9"};
  int status = po33_encode_main(7, args);
  assert(status != 0);
  absent(output);
  /* Finite float WAV samples must also respect the normalized PCM range. */
  sample_wav();
  uint8_t header[48] = {0};
  FILE *fp = fopen(sample, "rb");
  assert(fp);
  size_t n = fread(header, 1, sizeof(header), fp);
  status = fclose(fp);
  assert(n == sizeof(header) && status == 0);
  store32(header + 4, sizeof(header) - 8);
  store16(header + 20, 3);
  store32(header + 28, 23438 * 4);
  store16(header + 32, 4);
  store16(header + 34, 32);
  store32(header + 40, 4);
  float amplitude = -1.1f;
  uint32_t bits;
  memcpy(&bits, &amplitude, sizeof(bits));
  store32(header + 44, bits);
  save(sample, header, sizeof(header));
  fixture(0);
  save(input, wire, wire_size);
  char *small_sample[] = {"encode", input, output, "--sample", "7", sample};
  status = po33_encode_main(6, small_sample);
  assert(status == 1);
  absent(output);
}
static void options(void) {
  char *cases[][12] = {{"encode", "--help"},
                       {"encode"},
                       {"encode", input, output, "--channel"},
                       {"encode", input, output, "--channel", "wrong"},
                       {"encode", input, output, "--channel", "left", "--channel", "right"},
                       {"encode", input, output, "--rate"},
                       {"encode", input, output, "--rate", "x"},
                       {"encode", input, output, "--rate", "18446744073709551616"},
                       {"encode", input, output, "--sample", "256", sample},
                       {"encode", input, output, "--pattern-byte"},
                       {"encode", input, output, "--pattern-byte", "256", "0", "1"},
                       {"encode", input, output, "--pattern-byte", "2", "0x", "1"},
                       {"encode", input, output, "--pattern-byte", "2", "0", "1x"}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    int argc = 0;
    while (cases[i][argc])
      ++argc;
    int result = po33_encode_main(argc, cases[i]);
    assert(result == (i == 0 ? 0 : 2));
    absent(output);
  }
  unsigned long long number;
  int status = edit_number(NULL, 255, &number);
  assert(!status);
  status = edit_number("0XfF", 255, &number);
  assert(status && number == 255);
  status = edit_number("010", 255, &number);
  assert(status && number == 10);
}
static void helper_boundaries(void) {
  /* RIFF cannot describe a larger decoded pool even on a 64-bit host. */
  int status = write_sound_wav(output, 7, 10u + (UINT32_MAX - 36u) / 2u);
  assert(status == 0);
  const size_t actual[] = {8, 10, 11}, claimed[] = {10, 11, 10};
  for (size_t i = 0; i < 3; ++i) {
    status = mkdir(output, 0700);
    assert(status == 0);
    char source[512];
    int count = snprintf(source, sizeof(source), "%s/sound-007.bin", output);
    assert(count > 0 && (size_t)count < sizeof(source));
    uint8_t bytes[11] = {0};
    save(source, bytes, actual[i]);
    /* Simulate a truncated or extended backing file between dump and export. */
    status = write_sound_wav(output, 7, claimed[i]);
    assert(status == 0);
    remove_output();
  }

  fixture(0);
  po32_packet_t packets[64];
  template_t frame = {packets, 0, 1, 0};
  po33_decode_result_t parsed;
  po_status_t parsed_status = po33_frame_parse(wire, wire_size, edit_collect, &frame, &parsed);
  assert(parsed_status == PO_OK && parsed.stopped && frame.failed && frame.count == 1);
  frame = (template_t){packets, 0, 64, 0};
  parsed_status = po33_frame_parse(wire, wire_size, edit_collect, &frame, &parsed);
  assert(parsed_status == PO_OK && parsed.done && !frame.failed);
  uint8_t rebuilt[sizeof(wire)];
  size_t length = 0;
  const size_t capacities[] = {PO33_PREAMBLE_BYTES + 4, PO33_PREAMBLE_BYTES + 5, wire_size - 1};
  for (size_t i = 0; i < sizeof(capacities) / sizeof(capacities[0]); ++i) {
    status = edit_rebuild(&frame, rebuilt, capacities[i], &length);
    assert(status == 0);
  }
  status = edit_rebuild(&frame, rebuilt, sizeof(rebuilt), &length);
  assert(status == 1 && length == wire_size);
  parsed_status = po33_frame_parse(rebuilt, length, NULL, NULL, &parsed);
  assert(parsed_status == PO_OK && parsed.done && parsed.frame_bytes == length);
}
static void process(char **args, int expected) {
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    execv(args[0], args);
    _exit(127);
  }
  int status;
  pid_t waited = waitpid(child, &status, 0);
  assert(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == expected);
}
static void cli(const char *encoder, const char *decoder) {
  char capture[256], saved[512];
  path_for(capture, sizeof(capture), "capture.wav");
  fixture(0);
  save(input, wire, wire_size);
  char *encode_args[] = {(char *)encoder, input, capture, NULL};
  process(encode_args, 0);
  sample_wav();
  char *edit_args[] = {(char *)encoder, input,       output, "--sample", "7",
                       sample,          "--pattern", "2",    pattern,    NULL};
  process(edit_args, 0);
  po_capture_t edited_capture = {0};
  int status = po_capture_read(output, &edited_capture);
  assert(status == 1);
  uint8_t *edited_wire = NULL;
  size_t edited_size = 0;
  status = po_capture_decode(&edited_capture, PO_STEREO_CHANNEL_NORMAL, &edited_wire, &edited_size);
  assert(status == 1);
  po32_packet_t packets[64];
  template_t edited = {packets, 0, 64, 0};
  po33_decode_result_t parsed;
  po_status_t parsed_status =
      po33_frame_parse(edited_wire, edited_size, edit_collect, &edited, &parsed);
  assert(parsed_status == PO_OK && parsed.done && parsed.packet_count == 45);
  uint8_t *region = NULL;
  size_t region_size = 0;
  status = edit_region(&edited, PO33_TAG_PATTERN_SELECT, PO33_TAG_PATTERN_DATA, 2, &region,
                       &region_size);
  assert(status == 1 && region_size == 5 && !memcmp(region, "abcde", 5));
  free(region);
  region = NULL;
  status =
      edit_region(&edited, PO33_TAG_SOUND_SELECT, PO33_TAG_SOUND_DATA, 7, &region, &region_size);
  assert(status == 1 && region_size == 8201);
  for (size_t i = 0; i < region_size; ++i)
    assert(region[i] == (i < 9 ? 0x21 : i < 5009 ? po33_sample_encode(8192) : 0));
  free(region);
  free(edited_wire);
  free(edited_capture.samples);
  remove_output();
  edit_args[4] = "99";
  process(edit_args, 1);
  absent(output);
  edit_args[3] = "--unknown";
  process(edit_args, 2);
  absent(output);
  char *decode_args[] = {(char *)decoder, capture, output, NULL};
  process(decode_args, 0);
  int count = snprintf(saved, sizeof(saved), "%s/frame.bin", output);
  assert(count > 0 && (size_t)count < sizeof(saved));
  FILE *fp = fopen(saved, "rb");
  assert(fp);
  uint8_t actual[sizeof(wire)];
  size_t size = fread(actual, 1, sizeof(actual), fp);
  status = fclose(fp);
  assert(status == 0 && size == wire_size && !memcmp(actual, wire, size));
  process(decode_args, 1); /* Existing exports must survive. */
  remove_output();
  status = po33_capture_main(3, decode_args);
  assert(status == 0);
  remove_output();
  process(encode_args, 1); /* Existing captures must survive. */
  char *usage[] = {(char *)decoder, NULL};
  process(usage, 2);
  status = po33_capture_main(1, usage);
  assert(status == 2);
  char *missing[] = {(char *)decoder, output, output, NULL};
  process(missing, 1);
  status = po33_capture_main(3, missing);
  assert(status == 1);
  char *help[] = {(char *)encoder, "--help", NULL};
  process(help, 0);
  usage[0] = (char *)encoder;
  process(usage, 2);
  char *missing_template[] = {(char *)encoder, output, output, NULL};
  process(missing_template, 1);
  save(input, "", 0);
  encode_args[2] = output;
  process(encode_args, 1);
  /* Finite stereo silence reaches the no-sync CLI error. */
  uint8_t silence[44 + 24000 * 4] = {0};
  memcpy(silence, "RIFF", 4);
  store32(silence + 4, sizeof(silence) - 8);
  memcpy(silence + 8, "WAVEfmt ", 8);
  store32(silence + 16, 16);
  store16(silence + 20, 1);
  store16(silence + 22, 2);
  store32(silence + 24, 48000);
  store32(silence + 28, 192000);
  store16(silence + 32, 4);
  store16(silence + 34, 16);
  memcpy(silence + 36, "data", 4);
  store32(silence + 40, sizeof(silence) - 44);
  save(capture, silence, sizeof(silence));
  po_capture_t silent_capture = {0};
  status = po_capture_read(capture, &silent_capture);
  assert(status == 1);
  free(silent_capture.samples);
  process(decode_args, 1);
  status = po33_capture_main(3, decode_args);
  assert(status == 1);
  status = unlink(capture);
  assert(status == 0);
}
int main(int argc, char **argv) {
  assert(argc == 3);
  int made = 0;
  for (unsigned i = 0; i < 100 && !made; ++i) {
    snprintf(root, sizeof(root), "/tmp/libpo33-host-%ld-%u", (long)getpid(), i);
    made = mkdir(root, 0700) == 0;
    if (!made)
      assert(errno == EEXIST);
  }
  assert(made);
  path_for(input, sizeof(input), "frame.bin");
  path_for(output, sizeof(output), "output");
  path_for(sample, sizeof(sample), "sample.wav");
  path_for(pattern, sizeof(pattern), "pattern.bin");
  faults();
  malformed();
  options();
  helper_boundaries();
  cli(argv[1], argv[2]);
  int status = unlink(input);
  assert(status == 0);
  status = unlink(sample);
  assert(status == 0);
  status = unlink(pattern);
  assert(status == 0);
  status = rmdir(root);
  assert(status == 0);
  puts("PO-33 host failure and CLI tests passed");
  return 0;
}
