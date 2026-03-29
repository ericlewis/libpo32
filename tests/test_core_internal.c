#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../src/po32.c"

static void make_demo_patch_packet(po32_packet_t *out) {
  po32_patch_params_t patch;
  po32_patch_packet_t packet;

  po32_patch_params_zero(&patch);
  patch.OscFreq = 0.18f;
  patch.OscDcy = 0.42f;
  patch.Level = 0.88f;

  packet.instrument = 1u;
  packet.side = PO32_PATCH_LEFT;
  packet.params = patch;

  assert(po32_packet_encode(PO32_TAG_PATCH, &packet, out) == PO32_OK);
}

static size_t build_demo_frame(uint8_t *frame, size_t capacity, po32_packet_t *packet) {
  po32_builder_t builder;
  size_t frame_len = 0u;
  po32_status_t status;

  po32_builder_init(&builder, frame, capacity);
  status = po32_builder_append(&builder, packet);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  return frame_len;
}

static void test_tag_and_tail_helpers(void) {
  uint8_t frame[512];
  po32_packet_t packet;
  po32_final_tail_t tail;
  size_t frame_len;
  size_t consumed = 0u;

  make_demo_patch_packet(&packet);
  frame_len = build_demo_frame(frame, sizeof(frame), &packet);

  assert(strcmp(po32_tag_name(PO32_TAG_PATCH), "patch") == 0);
  assert(strcmp(po32_tag_name(PO32_TAG_PATTERN), "pattern") == 0);
  assert(strcmp(po32_tag_name(0xFFFFu), "unknown") == 0);

  assert(po32_tag_spec_find(PO32_TAG_PATCH) != NULL);
  assert(po32_tag_spec_find(0xFFFFu) == NULL);
  assert(po32_tag_payload_len_is_valid(PO32_TAG_PATCH, PO32_PATCH_PAYLOAD_BYTES));
  assert(!po32_tag_payload_len_is_valid(PO32_TAG_PATCH, 1u));
  assert(!po32_tag_payload_len_is_valid(0xFFFFu, 1u));

  assert(po32_final_tail_decode(PO32_INITIAL_STATE, NULL, PO32_FINAL_TAIL_BYTES, NULL, NULL) ==
         PO32_ERR_INVALID_ARG);
  assert(po32_final_tail_decode(PO32_INITIAL_STATE, frame + frame_len - PO32_FINAL_TAIL_BYTES,
                                PO32_FINAL_TAIL_BYTES - 1u, NULL, NULL) == PO32_ERR_INVALID_ARG);

  assert(po32_final_tail_decode(PO32_INITIAL_STATE, frame + frame_len - PO32_FINAL_TAIL_BYTES,
                                PO32_FINAL_TAIL_BYTES, &tail, &consumed) == PO32_ERR_FRAME);

  {
    uint16_t state = PO32_INITIAL_STATE;
    size_t body_len = frame_len - PO32_PREAMBLE_BYTES - PO32_FINAL_TAIL_BYTES;
    size_t consumed_bytes = 0u;
    po32_packet_t decoded;
    po32_status_t status = po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, body_len, &state,
                                                    &consumed_bytes, &decoded);
    assert(status == PO32_OK);
    assert(po32_final_tail_decode(state, frame + frame_len - PO32_FINAL_TAIL_BYTES,
                                  PO32_FINAL_TAIL_BYTES, &tail, &consumed) == PO32_OK);
    assert(consumed == PO32_FINAL_TAIL_BYTES);
    assert(tail.final_state != 0u);
  }
}

static void test_packet_decode_internals(void) {
  uint8_t frame[512];
  uint8_t mutated[512];
  po32_packet_t packet;
  size_t frame_len;
  size_t body_len;
  uint16_t state;
  uint16_t tag_code = 0u;
  size_t packet_len = 0u;
  size_t consumed = 0u;
  po32_status_t status;

  make_demo_patch_packet(&packet);
  frame_len = build_demo_frame(frame, sizeof(frame), &packet);
  body_len = frame_len - PO32_PREAMBLE_BYTES - PO32_FINAL_TAIL_BYTES;

  status = po32_packet_decode_header(NULL, PO32_PACKET_HEADER_BYTES, PO32_INITIAL_STATE, &tag_code,
                                     &packet_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_packet_decode_header(frame + PO32_PREAMBLE_BYTES, PO32_PACKET_HEADER_BYTES - 1u,
                                     PO32_INITIAL_STATE, &tag_code, &packet_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_packet_decode_header(frame + PO32_PREAMBLE_BYTES, body_len, PO32_INITIAL_STATE,
                                     &tag_code, &packet_len);
  assert(status == PO32_OK);
  assert(tag_code == PO32_TAG_PATCH);
  assert(packet_len ==
         PO32_PACKET_HEADER_BYTES + PO32_PATCH_PAYLOAD_BYTES + PO32_PACKET_TRAILER_BYTES);

  memcpy(mutated, frame + PO32_PREAMBLE_BYTES, body_len);
  {
    uint16_t s = PO32_INITIAL_STATE;
    uint8_t tag_lo = (uint8_t)(PO32_TAG_PATCH & 0xFFu);
    uint8_t tag_hi = (uint8_t)((PO32_TAG_PATCH >> 8) & 0xFFu);
    s = po32_crc_update(s, tag_lo);
    s = po32_crc_update(s, tag_hi);
    mutated[2] = po32_whiten_byte(s, 0u);
  }
  status = po32_packet_decode_header(mutated, body_len, PO32_INITIAL_STATE, &tag_code, &packet_len);
  assert(status == PO32_ERR_FRAME);

  state = PO32_INITIAL_STATE;
  status = po32_packet_decode_bytes(NULL, body_len, &state, &consumed, &packet);
  assert(status == PO32_ERR_INVALID_ARG);
  status =
      po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, body_len, NULL, &consumed, &packet);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, body_len, &state, NULL, &packet);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, body_len, &state, &consumed, NULL);
  assert(status == PO32_ERR_INVALID_ARG);

  state = PO32_INITIAL_STATE;
  status =
      po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, body_len, &state, &consumed, &packet);
  assert(status == PO32_OK);
  assert(consumed == packet_len);
  assert(packet.payload_len == PO32_PATCH_PAYLOAD_BYTES);

  memcpy(mutated, frame + PO32_PREAMBLE_BYTES, body_len);
  mutated[body_len - 1u] ^= 0x01u;
  state = PO32_INITIAL_STATE;
  status = po32_packet_decode_bytes(mutated, body_len, &state, &consumed, &packet);
  assert(status == PO32_ERR_FRAME);
}

static void test_builder_internal_helpers(void) {
  uint8_t preamble_only[PO32_PREAMBLE_BYTES];
  po32_builder_t builder;
  uint8_t byte = 0x12u;
  po32_status_t status;

  status = po32_builder_write_byte(NULL, byte);
  assert(status == PO32_ERR_INVALID_ARG);

  po32_zero(&builder, sizeof(builder));
  status = po32_builder_write_byte(&builder, byte);
  assert(status == PO32_ERR_INVALID_ARG);

  po32_builder_init(&builder, preamble_only, sizeof(preamble_only));
  status = po32_builder_write_byte(&builder, byte);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);
  status = po32_builder_write_bytes(&builder, &byte, 1u);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);
  status = po32_builder_write_state_trailer(&builder, 0x1234u);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);
}

static void test_decode_internal_helpers(void) {
  uint8_t frame[512];
  uint8_t small_frame[PO32_PREAMBLE_BYTES - 1u];
  po32_packet_t packet;
  po32_builder_t builder;
  po32_decode_ctx_t ctx;
  po32_demodulator_t demod;
  po32_decode_result_t result;
  size_t out_len = 99u;
  float sample = 0.0f;
  int collect_result;
  po32_status_t status;

  make_demo_patch_packet(&packet);

  collect_result = po32_decode_collect_packet(&packet, NULL);
  assert(collect_result == 1);

  po32_zero(&ctx, sizeof(ctx));
  collect_result = po32_decode_collect_packet(&packet, &ctx);
  assert(collect_result == 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  ctx.builder = &builder;
  ctx.status = PO32_ERR_FRAME;
  collect_result = po32_decode_collect_packet(&packet, &ctx);
  assert(collect_result == 1);

  po32_builder_init(&builder, frame, PO32_PREAMBLE_BYTES);
  ctx.builder = &builder;
  ctx.status = PO32_OK;
  collect_result = po32_decode_collect_packet(&packet, &ctx);
  assert(collect_result == 1);
  assert(ctx.status == PO32_ERR_BUFFER_TOO_SMALL);

  result.packet_count = 55;
  result.done = 1;
  out_len = 88u;
  po32_decode_result_clear(&result, &out_len);
  assert(result.packet_count == 0);
  assert(result.done == 0);
  assert(out_len == 0u);
  po32_decode_result_clear(NULL, NULL);

  status = po32_decode_prepare(NULL, 1u, 44100.0f, frame, sizeof(frame), &out_len, &builder, &ctx);
  assert(status == PO32_ERR_INVALID_ARG);
  status =
      po32_decode_prepare(&sample, 0u, 44100.0f, frame, sizeof(frame), &out_len, &builder, &ctx);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_prepare(&sample, 1u, 0.0f, frame, sizeof(frame), &out_len, &builder, &ctx);
  assert(status == PO32_ERR_INVALID_ARG);
  status =
      po32_decode_prepare(&sample, 1u, 44100.0f, NULL, sizeof(frame), &out_len, &builder, &ctx);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_prepare(&sample, 1u, 44100.0f, frame, sizeof(frame), NULL, &builder, &ctx);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_prepare(&sample, 1u, 44100.0f, frame, sizeof(frame), &out_len, NULL, &ctx);
  assert(status == PO32_ERR_INVALID_ARG);
  status =
      po32_decode_prepare(&sample, 1u, 44100.0f, frame, sizeof(frame), &out_len, &builder, NULL);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_prepare(&sample, 1u, 44100.0f, small_frame, sizeof(small_frame), &out_len,
                               &builder, &ctx);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);
  status =
      po32_decode_prepare(&sample, 1u, 44100.0f, frame, sizeof(frame), &out_len, &builder, &ctx);
  assert(status == PO32_OK);
  assert(ctx.builder == &builder);
  assert(ctx.status == PO32_OK);

  status = po32_decode_finalize(NULL, &ctx, &result, &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_finalize(&demod, NULL, &result, &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_finalize(&demod, &ctx, &result, NULL);
  assert(status == PO32_ERR_INVALID_ARG);

  ctx.status = PO32_ERR_BUFFER_TOO_SMALL;
  status = po32_decode_finalize(&demod, &ctx, &result, &out_len);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);

  ctx.status = PO32_OK;
  po32_zero(&demod, sizeof(demod));
  demod.packet_count = 7;
  demod.done = 0;
  status = po32_decode_finalize(&demod, &ctx, &result, &out_len);
  assert(status == PO32_ERR_FRAME);
  assert(result.packet_count == 7);
  assert(result.done == 0);

  po32_builder_init(&builder, frame, sizeof(frame));
  ctx.builder = &builder;
  demod.done = 1;
  demod.packet_count = 3;
  demod.tail.final_state = 0x1234u;
  status = po32_decode_finalize(&demod, &ctx, &result, &out_len);
  assert(status == PO32_OK);
  assert(out_len > PO32_PREAMBLE_BYTES);
  assert(result.packet_count == 3);
  assert(result.done == 1);
  assert(result.tail.final_state == 0x1234u);

  po32_demodulator_init(NULL, 44100.0f);
  po32_demodulator_init(&demod, 0.0f);
  assert(demod.sample_rate == 0.0f);
  po32_demodulator_init(&demod, 44100.0f);
  assert(demod.sample_rate == 44100.0f);

  status = po32_demodulator_push(NULL, &sample, 1u, NULL, NULL);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_demodulator_push(&demod, NULL, 1u, NULL, NULL);
  assert(status == PO32_ERR_INVALID_ARG);
  demod.done = 1;
  status = po32_demodulator_push(&demod, &sample, 1u, NULL, NULL);
  assert(status == PO32_OK);
}

int main(void) {
  test_tag_and_tail_helpers();
  test_packet_decode_internals();
  test_builder_internal_helpers();
  test_decode_internal_helpers();
  return 0;
}
