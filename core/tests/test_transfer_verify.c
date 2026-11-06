#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "po33.h"
#include "po35.h"
#include "po_frame_report.h"
#include "po_stereo.h"
#include "po_stereo_wav.h"

#define main po_transfer_verify_main
#include "../examples/po_transfer_verify.c"
#undef main

static void verify_save(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  size_t written;
  int status;
  assert(file != NULL);
  written = fwrite(bytes, 1u, length, file);
  status = fclose(file);
  assert(written == length && status == 0);
}

static void verify_write_le16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static void verify_write_le32(uint8_t *out, uint32_t value) {
  for (unsigned int index = 0u; index < 4u; ++index)
    out[index] = (uint8_t)(value >> (8u * index));
}

static void verify_write_raw_transfer_wav(const char *path, const uint8_t *wire,
                                          size_t wire_bytes) {
  po_stereo_modulator_t modulator;
  uint8_t header[44] = {0};
  uint8_t pcm[4096];
  float samples[2048];
  FILE *file;
  size_t count;
  size_t frames;
  size_t written;
  int status;

  assert(po_stereo_modulator_init(&modulator, wire, wire_bytes, 48000u) == PO_OK);
  frames = po_stereo_modulator_remaining(&modulator);
  assert(frames <= (UINT32_MAX - 36u) / 4u);
  memcpy(header, "RIFF", 4u);
  verify_write_le32(header + 4u, (uint32_t)(36u + frames * 4u));
  memcpy(header + 8u, "WAVEfmt ", 8u);
  verify_write_le32(header + 16u, 16u);
  verify_write_le16(header + 20u, 1u);
  verify_write_le16(header + 22u, 2u);
  verify_write_le32(header + 24u, 48000u);
  verify_write_le32(header + 28u, 192000u);
  verify_write_le16(header + 32u, 4u);
  verify_write_le16(header + 34u, 16u);
  memcpy(header + 36u, "data", 4u);
  verify_write_le32(header + 40u, (uint32_t)(frames * 4u));
  file = fopen(path, "wb");
  assert(file != NULL);
  written = fwrite(header, 1u, sizeof(header), file);
  assert(written == sizeof(header));
  while (!po_stereo_modulator_done(&modulator)) {
    assert(po_stereo_modulator_render_f32(&modulator, samples, 1024u, &count) == PO_OK);
    assert(count != 0u);
    for (size_t index = 0u; index < count * 2u; ++index) {
      float sample = samples[index];
      int32_t value = (int32_t)(sample * 32767.0f + (sample < 0.0f ? -0.5f : 0.5f));
      verify_write_le16(pcm + 2u * index, (uint16_t)value);
    }
    written = fwrite(pcm, 4u, count, file);
    assert(written == count);
  }
  status = fclose(file);
  assert(status == 0);
}

static size_t verify_frame(uint8_t *frame, size_t capacity, uint8_t value) {
  const uint8_t payload[] = {value, 0x12u, 0x34u};
  po33_builder_t builder;
  size_t length = 0u;
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  assert(po33_builder_append_packet(&builder, 0x4321u, payload, sizeof(payload), NULL) == PO_OK);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static size_t verify_frame35(uint8_t *frame, size_t capacity, uint8_t value) {
  const uint8_t payload[] = {value, 0x56u, 0x78u};
  po35_builder_t builder;
  size_t length = 0u;
  assert(po35_builder_init(&builder, frame, capacity) == PO_OK);
  assert(po35_builder_append_packet(&builder, 0x4321u, payload, sizeof(payload), NULL) == PO_OK);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static void verify_swap_pcm16_stereo(const char *source, const char *destination) {
  uint8_t *bytes = NULL;
  size_t length = 0u;
  assert(po_frame_read_file(source, &bytes, &length));
  assert(length >= 44u && memcmp(bytes, "RIFF", 4u) == 0 && memcmp(bytes + 8u, "WAVE", 4u) == 0 &&
         memcmp(bytes + 36u, "data", 4u) == 0 && (length - 44u) % 4u == 0u);
  for (size_t offset = 44u; offset < length; offset += 4u) {
    uint8_t left0 = bytes[offset];
    uint8_t left1 = bytes[offset + 1u];
    bytes[offset] = bytes[offset + 2u];
    bytes[offset + 1u] = bytes[offset + 3u];
    bytes[offset + 2u] = left0;
    bytes[offset + 3u] = left1;
  }
  verify_save(destination, bytes, length);
  free(bytes);
}

static void verify_zero_pcm16_stereo(const char *source, const char *destination) {
  uint8_t *bytes = NULL;
  size_t length = 0u;
  assert(po_frame_read_file(source, &bytes, &length));
  assert(length >= 44u && memcmp(bytes, "RIFF", 4u) == 0 && memcmp(bytes + 8u, "WAVE", 4u) == 0 &&
         memcmp(bytes + 36u, "data", 4u) == 0 && (length - 44u) % 4u == 0u);
  memset(bytes + 44u, 0, length - 44u);
  verify_save(destination, bytes, length);
  free(bytes);
}

static void verify_read_text(const char *path, char *text, size_t capacity) {
  uint8_t *bytes = NULL;
  size_t length = 0u;
  assert(po_frame_read_file(path, &bytes, &length));
  assert(length < capacity);
  memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
}

static int verify_run_cli(int argc, char **argv, char *stdout_text, size_t stdout_capacity,
                          char *stderr_text, size_t stderr_capacity) {
  FILE *stdout_file = fopen("verify-stdout.txt", "wb");
  FILE *stderr_file = fopen("verify-stderr.txt", "wb");
  int stdout_fd, stderr_fd, status, rc;

  assert(stdout_file != NULL && stderr_file != NULL);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  stdout_fd = dup(STDOUT_FILENO);
  stderr_fd = dup(STDERR_FILENO);
  assert(stdout_fd >= 0 && stderr_fd >= 0);
  status = dup2(fileno(stdout_file), STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(fileno(stderr_file), STDERR_FILENO);
  assert(status == STDERR_FILENO);
  rc = po_transfer_verify_main(argc, argv);
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
  verify_read_text("verify-stdout.txt", stdout_text, stdout_capacity);
  verify_read_text("verify-stderr.txt", stderr_text, stderr_capacity);
  return rc;
}

static void test_transfer_verification(void) {
  uint8_t reference[8192], corrupt[8192], different[8192], reference35[8192];
  char stdout_text[2048], stderr_text[1024];
  size_t reference_length = verify_frame(reference, sizeof(reference), 0x40u);
  size_t different_length = verify_frame(different, sizeof(different), 0x41u);
  size_t reference35_length = verify_frame35(reference35, sizeof(reference35), 0x50u);
  char *normal[] = {"po_transfer_verify", "--family", "po33", "reference.bin", "capture.wav", NULL};
  char *swapped[] = {"po_transfer_verify", "--family",    "po33",
                     "reference.bin",      "swapped.wav", NULL};
  char *mismatch[] = {"po_transfer_verify", "--family",    "po33",
                      "different.bin",      "capture.wav", NULL};
  char *invalid[] = {"po_transfer_verify", "--family", "po33", "invalid.bin", "capture.wav", NULL};
  char *bad_capture[] = {"po_transfer_verify", "--family",        "po33",
                         "reference.bin",      "bad-capture.wav", NULL};
  char *unrecovered[] = {"po_transfer_verify", "--family",        "po33",
                         "reference.bin",      "unrecovered.wav", NULL};
  char *normal35[] = {"po_transfer_verify", "--family",      "po35",
                      "reference35.bin",    "capture35.wav", NULL};
  char *parser_failure[] = {"po_transfer_verify", "--family",           "po33",
                            "reference.bin",      "parser-failure.wav", NULL};

  verify_save("reference.bin", reference, reference_length);
  verify_save("different.bin", different, different_length);
  verify_save("reference35.bin", reference35, reference35_length);
  assert(po33_write_transfer_wav("capture.wav", reference, reference_length, 48000));
  assert(po35_write_transfer_wav("capture35.wav", reference35, reference35_length, 48000));
  memcpy(corrupt, reference, reference_length);
  corrupt[reference_length - 1u] ^= 1u;
  verify_write_raw_transfer_wav("parser-failure.wav", corrupt, reference_length);
  verify_swap_pcm16_stereo("capture.wav", "swapped.wav");
  verify_zero_pcm16_stereo("capture.wav", "unrecovered.wav");
  verify_save("invalid.bin", reference, reference_length - 1u);
  verify_save("bad-capture.wav", (const uint8_t *)"not a wav", 9u);

  assert(verify_run_cli(5, normal, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 0);
  assert(stderr_text[0] == '\0' &&
         strstr(stdout_text, "family=po33 reference_bytes=") == stdout_text &&
         strstr(stdout_text,
                "channel_order=normal transport=recovered parser=complete exact=yes") != NULL &&
         strstr(stdout_text, "verification=exact matches=1") != NULL);

  assert(verify_run_cli(5, swapped, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 0);
  assert(stderr_text[0] == '\0' &&
         strstr(stdout_text,
                "channel_order=swapped transport=recovered parser=complete exact=yes") != NULL &&
         strstr(stdout_text, "verification=exact matches=1") != NULL);

  assert(verify_run_cli(5, normal35, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 0);
  assert(stderr_text[0] == '\0' &&
         strstr(stdout_text, "family=po35 reference_bytes=") == stdout_text &&
         strstr(stdout_text,
                "channel_order=normal transport=recovered parser=complete exact=yes") != NULL &&
         strstr(stdout_text, "verification=exact matches=1") != NULL);

  assert(verify_run_cli(5, parser_failure, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 1);
  assert(stderr_text[0] == '\0' &&
         strstr(stdout_text, "channel_order=normal transport=recovered parser=frame exact=no") !=
             NULL &&
         strstr(stdout_text, "verification=not-exact matches=0") != NULL);

  assert(verify_run_cli(5, mismatch, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 1);
  assert(stderr_text[0] == '\0' && strstr(stdout_text, "verification=not-exact matches=0") != NULL);

  assert(verify_run_cli(5, invalid, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 1);
  assert(stdout_text[0] == '\0' &&
         strstr(stderr_text, "Reference is not one complete po33 frame") != NULL);

  assert(verify_run_cli(5, bad_capture, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 1);
  assert(stdout_text[0] == '\0' && strstr(stderr_text, "Cannot read capture WAV") != NULL);

  assert(verify_run_cli(5, unrecovered, stdout_text, sizeof(stdout_text), stderr_text,
                        sizeof(stderr_text)) == 1);
  assert(
      stderr_text[0] == '\0' &&
      strstr(stdout_text, "channel_order=normal transport=not-recovered parser=not-run") != NULL &&
      strstr(stdout_text, "channel_order=swapped transport=not-recovered parser=not-run") != NULL &&
      strstr(stdout_text, "verification=not-exact matches=0") != NULL);
}

static void test_frame_file_reader(void) {
  uint8_t *bytes = NULL;
  size_t length = 77u;
  verify_save("empty.bin", (const uint8_t *)"", 0u);
  assert(!po_frame_read_file(NULL, &bytes, &length));
  assert(bytes == NULL && length == 77u);
  assert(!po_frame_read_file("missing.bin", &bytes, &length));
  assert(bytes == NULL && length == 0u);
  assert(po_frame_read_file("empty.bin", &bytes, &length));
  assert(bytes != NULL && length == 0u);
  free(bytes);
}

int main(void) {
  char directory[128];
  int made = 0;
  int status;
  for (unsigned int index = 0u; index < 100u && !made; ++index) {
    snprintf(directory, sizeof(directory), "/tmp/libpo-transfer-verify-%ld-%u", (long)getpid(),
             index);
    made = mkdir(directory, 0700) == 0;
    assert(made || errno == EEXIST);
  }
  assert(made);
  status = chdir(directory);
  assert(status == 0);
  test_frame_file_reader();
  test_transfer_verification();
  puts("Transfer verification tests passed");
  return 0;
}
