/* Frame-to-modem encoder only: this does not analyze PCM into speech records. */
#include "po35.h"
#include "po_stereo_wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  po35_builder_t builder;
  int failed;
} e35_rebuild_t;

static int e35_read_file(const char *path, uint8_t **out, size_t *out_length) {
  FILE *fp = fopen(path, "rb");
  uint8_t *bytes = NULL;
  long length = 0;
  int ok = 0;
  if (fp == NULL)
    return 0;
  if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0)
    goto cleanup;
  bytes = (uint8_t *)malloc(length ? (size_t)length : 1u);
  if (bytes == NULL || fread(bytes, 1, (size_t)length, fp) != (size_t)length || ferror(fp))
    goto cleanup;
  ok = 1;
cleanup:
  if (fclose(fp) != 0)
    ok = 0;
  if (ok) {
    *out = bytes;
    *out_length = (size_t)length;
  } else
    free(bytes);
  return ok;
}

static int e35_rebuild_packet(const po_packet_t *packet, void *user) {
  e35_rebuild_t *rebuild = (e35_rebuild_t *)user;
  if (po35_builder_append_packet(&rebuild->builder, packet->tag_code, packet->payload,
                                 packet->payload_len, NULL) != PO_OK) {
    rebuild->failed = 1;
    return 1;
  }
  return 0;
}

static void e35_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s frame.bin new-output.wav [--rate 44100|48000|96000]\n"
          "Default transfer rate: 48000 Hz. Rebuilds an existing PO-35/137 frame unchanged.\n"
          "This encodes modem audio, not PCM-to-speech analysis records.\n",
          program);
}

int main(int argc, char **argv) {
  uint8_t *frame = NULL, *rebuilt = NULL;
  size_t frame_len = 0, rebuilt_len = 0;
  uint32_t rate = 48000;
  po35_decode_result_t result;
  e35_rebuild_t rebuild = {0};
  int ok = 0;
  if (argc != 3 && argc != 5) {
    e35_usage(argv[0]);
    return 2;
  }
  if (argc == 5) {
    if (strcmp(argv[3], "--rate")) {
      e35_usage(argv[0]);
      return 2;
    }
    if (!strcmp(argv[4], "44100"))
      rate = 44100;
    else if (!strcmp(argv[4], "48000"))
      rate = 48000;
    else if (!strcmp(argv[4], "96000"))
      rate = 96000;
    else {
      e35_usage(argv[0]);
      return 2;
    }
  }
  if (!e35_read_file(argv[1], &frame, &frame_len)) {
    fprintf(stderr, "Cannot read input frame.\n");
    goto cleanup;
  }
  rebuilt = (uint8_t *)malloc(frame_len ? frame_len : 1u);
  if (rebuilt == NULL || po35_builder_init(&rebuild.builder, rebuilt, frame_len) != PO_OK ||
      po35_frame_parse(frame, frame_len, e35_rebuild_packet, &rebuild, &result) != PO_OK ||
      !result.done || result.stopped || result.frame_bytes != frame_len || rebuild.failed ||
      po35_builder_finish(&rebuild.builder, &rebuilt_len) != PO_OK || rebuilt_len != frame_len ||
      memcmp(frame, rebuilt, frame_len)) {
    fprintf(stderr, "Input must contain exactly one complete, byte-preserving PO-35 frame.\n");
    goto cleanup;
  }
  if (!po35_write_transfer_wav(argv[2], rebuilt, rebuilt_len, rate)) {
    fprintf(stderr, "Cannot create new transfer WAV. Existing outputs are not overwritten.\n");
    goto cleanup;
  }
  printf("Encoded %zu-byte PO-35 frame at %u Hz; byte-exact rebuild verified.\n", rebuilt_len,
         rate);
  ok = 1;
cleanup:
  free(rebuilt);
  free(frame);
  return ok ? 0 : 1;
}
