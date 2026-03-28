#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "po32.h"

static int fail_message(const char *message) {
  fputs(message, stderr);
  return 1;
}

static int fail_status(const char *context, int status) {
  char buffer[128];
  (void)snprintf(buffer, sizeof(buffer), "%s: %d\n", context, status);
  fputs(buffer, stderr);
  return 1;
}

/* Minimal packet printer for the roundtrip example. */
typedef struct {
  int packet_count;
} packet_printer_t;

static int print_packet(const po32_packet_t *packet, void *user) {
  packet_printer_t *printer = (packet_printer_t *)user;
  const char *name = po32_tag_name(packet->tag_code);

  printer->packet_count += 1;
  printf("packet %d: %s (0x%04x), payload=%zu bytes, offset=%zu\n",
         printer->packet_count,
         name,
         (unsigned int)packet->tag_code,
         packet->payload_len,
         packet->offset);
  return 0;
}

int main(void) {
  po32_patch_params_t patch;
  po32_builder_t builder;
  po32_decode_result_t decode_result;
  po32_final_tail_t parsed_tail;
  po32_patch_packet_t patch_packet;
  po32_packet_t encoded_packet;
  packet_printer_t printer = {0};
  uint8_t frame[2048];
  uint8_t decoded_frame[2048];
  size_t frame_len = 0u;
  size_t decoded_len = 0u;
  size_t sample_count;
  float *samples = NULL;
  po32_status_t status;
  const uint32_t sample_rate = 44100u;
  int same_frame = 0;

  po32_patch_params_zero(&patch);
  patch.OscFreq = 0.28f;
  patch.OscDcy = 0.42f;
  patch.Level = 0.88f;

  patch_packet.instrument = 1u;
  patch_packet.side = PO32_PATCH_LEFT;
  patch_packet.params = patch;

  status = po32_packet_encode(PO32_TAG_PATCH, &patch_packet, &encoded_packet);
  if (status != PO32_OK) {
    return fail_status("encode patch failed", status);
  }

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &encoded_packet);
  if (status != PO32_OK) {
    return fail_status("append patch failed", status);
  }

  status = po32_builder_finish(&builder, &frame_len);
  if (status != PO32_OK) {
    return fail_status("finish frame failed", status);
  }

  sample_count = po32_render_sample_count(frame_len, sample_rate);
  samples = (float *)malloc(sample_count * sizeof(*samples));
  if (samples == NULL) {
    return fail_message("sample allocation failed\n");
  }

  status = po32_render_dpsk_f32(frame, frame_len, sample_rate, samples, sample_count);
  if (status != PO32_OK) {
    int exit_code = fail_status("render failed", status);
    free(samples);
    return exit_code;
  }

  status = po32_decode_f32(samples,
                           sample_count,
                           (float)sample_rate,
                           &decode_result,
                           decoded_frame,
                           sizeof(decoded_frame),
                           &decoded_len);
  free(samples);
  if (status != PO32_OK) {
    return fail_status("decode failed", status);
  }

  status = po32_frame_parse(decoded_frame, decoded_len, print_packet, &printer, &parsed_tail);
  if (status != PO32_OK) {
    return fail_status("parse decoded frame failed", status);
  }

  same_frame = (frame_len == decoded_len && memcmp(frame, decoded_frame, frame_len) == 0);

  printf("\nsummary:\n");
  printf("  original frame: %zu bytes\n", frame_len);
  printf("  rendered audio: %zu float samples @ %u Hz\n", sample_count, sample_rate);
  printf("  decoded frame:  %zu bytes\n", decoded_len);
  printf("  decoded packets: %d\n", decode_result.packet_count);
  printf("  decoded tail: %s\n", decode_result.done ? "yes" : "no");
  printf("  frame roundtrip: %s\n", same_frame ? "exact" : "normalized-but-different");
  printf("  final tail state: 0x%04x\n", parsed_tail.final_state);

  return 0;
}
