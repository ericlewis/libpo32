/*
 * Fuzz harness for po32_frame_parse().
 *
 * The input is parsed twice: once as raw bytes, and once behind the static
 * 128-byte preamble so the fuzzer reaches the packet layer without having
 * to synthesize the preamble byte-for-byte. Every packet the parser accepts
 * is fed to the typed decoders, which must never crash on parser-approved
 * payloads.
 */

#include "po32.h"
#include "fuzz_driver.h"

#include <string.h>

#define FUZZ_FRAME_MAX 4096u

typedef struct {
  size_t frame_len;
  int packets;
} fuzz_parse_ctx_t;

static int fuzz_on_packet(const po32_packet_t *packet, void *user) {
  fuzz_parse_ctx_t *ctx = (fuzz_parse_ctx_t *)user;
  union {
    po32_patch_packet_t patch;
    po32_knob_packet_t knob;
    po32_reset_packet_t reset;
    po32_state_packet_t state;
    po32_pattern_packet_t pattern;
  } typed;

  fuzz_check(packet != NULL);
  fuzz_check(packet->payload_len <= PO32_MAX_PAYLOAD);
  fuzz_check(packet->trailer.matches_state == 1);
  fuzz_check(strcmp(po32_tag_name(packet->tag_code), "unknown") != 0);
  fuzz_check(packet->offset >= PO32_PREAMBLE_BYTES);
  /* Header (3) + payload + trailer (2) must sit inside the frame. */
  fuzz_check(packet->offset + packet->payload_len + 5u <= ctx->frame_len);

  /* Typed decode of parser-approved payloads must not crash; rejection is
     fine (the frame layer validates length, not payload content). */
  (void)po32_packet_decode(packet->tag_code, packet->payload, packet->payload_len, &typed);

  ctx->packets++;
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static uint8_t frame[PO32_PREAMBLE_BYTES + FUZZ_FRAME_MAX];
  fuzz_parse_ctx_t ctx;
  po32_final_tail_t tail;
  size_t framed_len;

  ctx.frame_len = size;
  ctx.packets = 0;
  (void)po32_frame_parse(data, size, fuzz_on_packet, &ctx, &tail);

  if (size > FUZZ_FRAME_MAX) {
    size = FUZZ_FRAME_MAX;
  }
  memcpy(frame, po32_preamble_bytes(), PO32_PREAMBLE_BYTES);
  memcpy(frame + PO32_PREAMBLE_BYTES, data, size);
  framed_len = PO32_PREAMBLE_BYTES + size;

  ctx.frame_len = framed_len;
  ctx.packets = 0;
  (void)po32_frame_parse(frame, framed_len, fuzz_on_packet, &ctx, &tail);

  return 0;
}
