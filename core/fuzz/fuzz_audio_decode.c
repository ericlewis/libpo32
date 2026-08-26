/*
 * Fuzz harness for po32_decode_f32().
 *
 * The input bytes are reinterpreted as raw float samples — including NaNs,
 * infinities, and denormals, exactly what a hostile WAV file can contain.
 * The decoder must never crash, and any frame it does reconstruct must be
 * re-parseable by po32_frame_parse().
 */

#include "po32.h"
#include "fuzz_driver.h"

#include <string.h>

#define FUZZ_MAX_SAMPLES 65536u
#define FUZZ_FRAME_CAP   2048u

typedef struct {
  int packets;
} fuzz_count_ctx_t;

static int fuzz_count_packet(const po32_packet_t *packet, void *user) {
  fuzz_count_ctx_t *ctx = (fuzz_count_ctx_t *)user;

  fuzz_check(packet->payload_len <= PO32_MAX_PAYLOAD);
  ctx->packets++;
  return 0;
}

static const float fuzz_rates[] = {8000.0f, 22050.0f, 44100.0f, 48000.0f};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static float samples[FUZZ_MAX_SAMPLES];
  static uint8_t frame[FUZZ_FRAME_CAP];
  po32_decode_result_t result;
  float sample_rate;
  size_t count;
  size_t frame_len = 0u;

  if (size < 1u) {
    return 0;
  }

  sample_rate = fuzz_rates[data[0] % (sizeof(fuzz_rates) / sizeof(fuzz_rates[0]))];
  data += 1u;
  size -= 1u;

  count = size / sizeof(float);
  if (count > FUZZ_MAX_SAMPLES) {
    count = FUZZ_MAX_SAMPLES;
  }
  if (count == 0u) {
    return 0;
  }
  memcpy(samples, data, count * sizeof(float));

  if (po32_decode_f32(samples, count, sample_rate, &result, frame, sizeof(frame), &frame_len) ==
      PO32_OK) {
    po32_final_tail_t tail;
    fuzz_count_ctx_t ctx = {0};

    fuzz_check(result.done == 1);
    fuzz_check(frame_len >= PO32_PREAMBLE_BYTES + PO32_FINAL_TAIL_BYTES);
    fuzz_check(frame_len <= sizeof(frame));

    /* A frame the decoder accepted must be valid for the frame parser. The
       decoder can span several sync sessions, so the parser may see more
       packets than the final session counted — never fewer. */
    fuzz_check(po32_frame_parse(frame, frame_len, fuzz_count_packet, &ctx, &tail) == PO32_OK);
    fuzz_check(ctx.packets >= result.packet_count);
  }

  return 0;
}
