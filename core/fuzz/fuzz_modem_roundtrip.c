/*
 * Differential fuzz harness for the whole transfer pipeline.
 *
 * The input is treated as a build script: it chooses a sample rate and up to
 * three typed packets with fuzz-controlled contents. The harness then checks
 * the full chain end to end:
 *
 *   typed packets → frame builder → frame parser   (must match exactly)
 *   frame → DPSK render → audio decode → frame     (must be lossless)
 *
 * Any disagreement between the builder, the parser, the modulator, and the
 * demodulator is a bug.
 */

#include "po32.h"
#include "fuzz_driver.h"

#include <string.h>

#define FUZZ_MAX_PACKETS 3u
#define FUZZ_FRAME_CAP   1024u
#define FUZZ_SAMPLE_CAP  330000u

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;
} fuzz_stream_t;

static uint8_t fuzz_next(fuzz_stream_t *s) {
  return s->pos < s->size ? s->data[s->pos++] : 0u;
}

typedef struct {
  po32_packet_t expected[FUZZ_MAX_PACKETS];
  size_t count;
  size_t seen;
} fuzz_roundtrip_ctx_t;

static int fuzz_verify_packet(const po32_packet_t *packet, void *user) {
  fuzz_roundtrip_ctx_t *ctx = (fuzz_roundtrip_ctx_t *)user;
  const po32_packet_t *expected;

  fuzz_check(ctx->seen < ctx->count);
  expected = &ctx->expected[ctx->seen];
  fuzz_check(packet->tag_code == expected->tag_code);
  fuzz_check(packet->payload_len == expected->payload_len);
  fuzz_check(memcmp(packet->payload, expected->payload, packet->payload_len) == 0);
  ctx->seen++;
  return 0;
}

static void fuzz_build_patch(fuzz_stream_t *s, po32_patch_packet_t *pkt) {
  memset(pkt, 0, sizeof(*pkt));
  pkt->instrument = (uint8_t)(1u + fuzz_next(s) % 16u);
  pkt->side = (fuzz_next(s) & 1u) ? PO32_PATCH_RIGHT : PO32_PATCH_LEFT;

#define FUZZ_SET_PARAM(name) pkt->params.name = (float)fuzz_next(s) / 255.0f;
  FUZZ_PATCH_PARAM_FIELDS(FUZZ_SET_PARAM)
#undef FUZZ_SET_PARAM
}

static void fuzz_build_knob(fuzz_stream_t *s, po32_knob_packet_t *pkt) {
  memset(pkt, 0, sizeof(*pkt));
  pkt->instrument = (uint8_t)(1u + fuzz_next(s) % 16u);
  pkt->kind = (fuzz_next(s) & 1u) ? PO32_KNOB_MORPH : PO32_KNOB_PITCH;
  pkt->value = fuzz_next(s);
}

static void fuzz_build_state(fuzz_stream_t *s, po32_state_packet_t *pkt) {
  memset(pkt, 0, sizeof(*pkt));
  po32_morph_pairs_default(pkt->morph_pairs, PO32_STATE_MORPH_PAIR_COUNT);
  for (size_t i = 0u; i < PO32_STATE_MORPH_PAIR_COUNT; ++i) {
    pkt->morph_pairs[i].flag = fuzz_next(s);
    pkt->morph_pairs[i].morph = fuzz_next(s);
  }
  pkt->tempo = fuzz_next(s);
  pkt->swing_times_12 = fuzz_next(s);
  pkt->pattern_count = fuzz_next(s) % (PO32_PATTERN_STEP_COUNT + 1u);
  for (size_t i = 0u; i < pkt->pattern_count; ++i) {
    pkt->pattern_numbers[i] = fuzz_next(s);
  }
}

static void fuzz_build_pattern(fuzz_stream_t *s, po32_pattern_packet_t *pkt) {
  uint8_t triggers = (uint8_t)(fuzz_next(s) % 12u);

  po32_pattern_init(pkt, fuzz_next(s));
  for (uint8_t i = 0u; i < triggers; ++i) {
    uint8_t step = (uint8_t)(fuzz_next(s) % PO32_PATTERN_STEP_COUNT);
    uint8_t instrument = (uint8_t)(1u + fuzz_next(s) % 16u);
    uint8_t fill_rate = (uint8_t)(1u + fuzz_next(s) % 15u);

    fuzz_check(po32_pattern_set_trigger(pkt, step, instrument, fill_rate) == PO32_OK);
    if (fuzz_next(s) & 1u) {
      fuzz_check(po32_pattern_set_accent(pkt, step, fuzz_next(s) & 1u) == PO32_OK);
    }
  }
}

static const uint32_t fuzz_rates[] = {22050u, 44100u, 48000u};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static uint8_t frame[FUZZ_FRAME_CAP];
  static uint8_t decoded_frame[FUZZ_FRAME_CAP];
  static float samples[FUZZ_SAMPLE_CAP];
  fuzz_stream_t stream = {data, size, 0u};
  fuzz_roundtrip_ctx_t ctx;
  po32_builder_t builder;
  po32_final_tail_t tail;
  po32_decode_result_t result;
  uint32_t sample_rate;
  size_t frame_len = 0u;
  size_t sample_count;
  size_t decoded_len = 0u;

  if (size < 2u) {
    return 0;
  }

  sample_rate = fuzz_rates[fuzz_next(&stream) % (sizeof(fuzz_rates) / sizeof(fuzz_rates[0]))];

  memset(&ctx, 0, sizeof(ctx));
  po32_builder_init(&builder, frame, sizeof(frame));

  while (stream.pos < stream.size && ctx.count < FUZZ_MAX_PACKETS) {
    union {
      po32_patch_packet_t patch;
      po32_knob_packet_t knob;
      po32_reset_packet_t reset;
      po32_state_packet_t state;
      po32_pattern_packet_t pattern;
    } typed;
    po32_packet_t *wire = &ctx.expected[ctx.count];
    uint16_t tag;

    switch (fuzz_next(&stream) % 5u) {
    case 0u:
      tag = PO32_TAG_PATCH;
      fuzz_build_patch(&stream, &typed.patch);
      break;
    case 1u:
      tag = PO32_TAG_KNOB;
      fuzz_build_knob(&stream, &typed.knob);
      break;
    case 2u:
      tag = PO32_TAG_RESET;
      memset(&typed.reset, 0, sizeof(typed.reset));
      typed.reset.instrument = (uint8_t)(1u + fuzz_next(&stream) % 16u);
      break;
    case 3u:
      tag = PO32_TAG_STATE;
      fuzz_build_state(&stream, &typed.state);
      break;
    default:
      tag = PO32_TAG_PATTERN;
      fuzz_build_pattern(&stream, &typed.pattern);
      break;
    }

    fuzz_check(po32_packet_encode(tag, &typed, wire) == PO32_OK);
    fuzz_check(po32_builder_append(&builder, wire) == PO32_OK);
    ctx.count++;
  }

  fuzz_check(po32_builder_finish(&builder, &frame_len) == PO32_OK);
  fuzz_check(frame_len <= sizeof(frame));

  /* Builder output must parse back to exactly the packets that went in. */
  fuzz_check(po32_frame_parse(frame, frame_len, fuzz_verify_packet, &ctx, &tail) == PO32_OK);
  fuzz_check(ctx.seen == ctx.count);

  /* Modem round trip must be lossless. */
  sample_count = po32_render_sample_count(frame_len, sample_rate);
  fuzz_check(sample_count <= FUZZ_SAMPLE_CAP);
  fuzz_check(po32_render_dpsk_f32(frame, frame_len, sample_rate, samples, sample_count) == PO32_OK);
  for (size_t i = 0u; i < sample_count; ++i) {
    /* Also rejects NaN: NaN fails both comparisons. */
    fuzz_check(samples[i] >= -1.0f && samples[i] <= 1.0f);
  }

  fuzz_check(po32_decode_f32(samples, sample_count, (float)sample_rate, &result, decoded_frame,
                             sizeof(decoded_frame), &decoded_len) == PO32_OK);
  fuzz_check(result.done == 1);
  fuzz_check((size_t)result.packet_count == ctx.count);
  fuzz_check(decoded_len == frame_len);
  fuzz_check(memcmp(decoded_frame, frame, frame_len) == 0);

  return 0;
}
