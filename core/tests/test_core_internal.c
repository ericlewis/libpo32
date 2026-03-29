#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../src/po32.c"

static int count_packet_callback(const po32_packet_t *packet, void *user) {
  int *count = (int *)user;
  (void)packet;
  if (count != NULL) {
    *count += 1;
  }
  return 0;
}

static int stop_packet_callback(const po32_packet_t *packet, void *user) {
  int *count = (int *)user;
  (void)packet;
  if (count != NULL) {
    *count += 1;
  }
  return 1;
}

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
  assert(strcmp(po32_tag_name(PO32_TAG_ERASE), "erase") == 0);
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
  status = po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, PO32_PACKET_OVERHEAD_BYTES - 1u,
                                    &state, &consumed, &packet);
  assert(status == PO32_ERR_FRAME);
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

  state = PO32_INITIAL_STATE;
  status = po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES,
                                    PO32_PACKET_HEADER_BYTES + PO32_PATCH_PAYLOAD_BYTES - 1u,
                                    &state, &consumed, &packet);
  assert(status == PO32_ERR_FRAME);

  {
    size_t pos = 0u;
    status = (po32_status_t)po32_frame_parse_packet_at(
        frame + PO32_PREAMBLE_BYTES, PO32_PACKET_OVERHEAD_BYTES - 1u, &pos, &state, &packet);
    assert(status == (po32_status_t)-1);
  }
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

static void test_final_tail_and_trailer_corruption(void) {
  uint8_t frame[512];
  uint8_t mutated[512];
  po32_packet_t packet;
  po32_final_tail_t tail;
  size_t frame_len;
  size_t body_len;
  size_t consumed = 0u;
  uint16_t state;
  po32_status_t status;

  make_demo_patch_packet(&packet);
  frame_len = build_demo_frame(frame, sizeof(frame), &packet);
  body_len = frame_len - PO32_PREAMBLE_BYTES - PO32_FINAL_TAIL_BYTES;

  /* Compute the CRC state after decoding the body */
  {
    uint16_t body_state = PO32_INITIAL_STATE;
    size_t decoded = 0u;
    po32_packet_t tmp;
    status = po32_packet_decode_bytes(frame + PO32_PREAMBLE_BYTES, body_len, &body_state, &decoded,
                                      &tmp);
    assert(status == PO32_OK);

    /* Good tail decode — verify consumed output path (line 198) */
    consumed = 0u;
    status = po32_final_tail_decode(body_state, frame + frame_len - PO32_FINAL_TAIL_BYTES,
                                    PO32_FINAL_TAIL_BYTES, &tail, &consumed);
    assert(status == PO32_OK);
    assert(consumed == PO32_FINAL_TAIL_BYTES);

    /* Good tail decode with tail=NULL (line 185 false branch) */
    status = po32_final_tail_decode(body_state, frame + frame_len - PO32_FINAL_TAIL_BYTES,
                                    PO32_FINAL_TAIL_BYTES, NULL, NULL);
    assert(status == PO32_OK);

    /* Corrupt the 4th byte of the tail (raw4) to trigger line 181-182 */
    memcpy(mutated, frame, frame_len);
    mutated[frame_len - 2u] ^= 0x01u;
    status = po32_final_tail_decode(body_state, mutated + frame_len - PO32_FINAL_TAIL_BYTES,
                                    PO32_FINAL_TAIL_BYTES, &tail, NULL);
    assert(status == PO32_ERR_FRAME);
  }

  /* Corrupt the packet trailer (last 2 bytes of the body) to trigger line 250-252 */
  memcpy(mutated, frame + PO32_PREAMBLE_BYTES, body_len);
  mutated[body_len - 1u] ^= 0x01u;
  state = PO32_INITIAL_STATE;
  consumed = 0u;
  status = po32_packet_decode_bytes(mutated, body_len, &state, &consumed, &packet);
  assert(status == PO32_ERR_FRAME);
}

static void test_builder_finish_edge_cases(void) {
  po32_builder_t builder;
  po32_packet_t packet;
  po32_status_t status;
  size_t frame_len = 0u;

  make_demo_patch_packet(&packet);

  /* builder_finish with frame_len = NULL (line 688 false branch) */
  {
    uint8_t frame[512];
    po32_builder_init(&builder, frame, sizeof(frame));
    status = po32_builder_append(&builder, &packet);
    assert(status == PO32_OK);
    status = po32_builder_finish(&builder, NULL);
    assert(status == PO32_OK);
    assert(builder.finished == 1);
  }

  /* builder_finish when already finished with frame_len = NULL (line 661 branch) */
  {
    uint8_t frame[512];
    po32_builder_init(&builder, frame, sizeof(frame));
    status = po32_builder_append(&builder, &packet);
    assert(status == PO32_OK);
    status = po32_builder_finish(&builder, &frame_len);
    assert(status == PO32_OK);
    status = po32_builder_finish(&builder, NULL);
    assert(status == PO32_OK);
  }

  /* builder_reset with buffer = NULL (line 391-392) */
  {
    po32_zero(&builder, sizeof(builder));
    builder.capacity = 256u;
    po32_builder_reset(&builder);
    assert(builder.length == 0u);
  }

  /* builder_reset with capacity too small for preamble (line 391-392) */
  {
    uint8_t tiny[4];
    po32_zero(&builder, sizeof(builder));
    builder.buffer = tiny;
    builder.capacity = sizeof(tiny);
    po32_builder_reset(&builder);
    assert(builder.length == 0u);
  }

  /* builder_finish where capacity is exactly exhausted during tail writing (lines 673-685) */
  {
    /* Build a frame, then set capacity to allow only partial tail */
    uint8_t frame[512];
    po32_builder_init(&builder, frame, sizeof(frame));
    status = po32_builder_append(&builder, &packet);
    assert(status == PO32_OK);
    /* Shrink capacity to leave room for only 1 tail byte (need 5) */
    builder.capacity = builder.length + 1u;
    status = po32_builder_finish(&builder, &frame_len);
    assert(status == PO32_ERR_BUFFER_TOO_SMALL);
  }
}

static void test_public_guard_paths(void) {
  uint8_t frame[512];
  uint8_t mutated[512];
  uint8_t short_buffer[PO32_PREAMBLE_BYTES - 1u];
  uint8_t patch_bytes[PO32_PATCH_PARAM_BYTES];
  uint8_t state_bytes[70];
  uint8_t pattern_bytes[PO32_PATTERN_PAYLOAD_BYTES];
  po32_packet_t packet;
  po32_packet_t encoded;
  po32_builder_t builder;
  po32_patch_params_t patch_params;
  po32_patch_packet_t patch_packet;
  po32_knob_packet_t knob_packet;
  po32_reset_packet_t reset_packet;
  po32_state_packet_t state_packet;
  po32_pattern_packet_t pattern_packet;
  po32_morph_pair_t morph_pair;
  po32_modulator_t modulator;
  float sample = 0.0f;
  size_t frame_len;
  size_t out_len = 0u;
  size_t packet_offset = 0u;
  int callback_count = 0;
  po32_status_t status;

  make_demo_patch_packet(&packet);
  frame_len = build_demo_frame(frame, sizeof(frame), &packet);

  assert(po32_frame_parse(NULL, frame_len, NULL, NULL, NULL) == PO32_ERR_INVALID_ARG);
  assert(po32_frame_parse(frame, PO32_PREAMBLE_BYTES + PO32_FINAL_TAIL_BYTES - 1u, NULL, NULL,
                          NULL) == PO32_ERR_INVALID_ARG);

  memcpy(mutated, frame, frame_len);
  mutated[0] ^= 0x01u;
  assert(po32_frame_parse(mutated, frame_len, NULL, NULL, NULL) == PO32_ERR_FRAME);

  callback_count = 0;
  assert(po32_frame_parse(frame, frame_len, stop_packet_callback, &callback_count, NULL) ==
         PO32_OK);
  assert(callback_count == 1);

  assert(po32_frame_parse(frame, frame_len - PO32_FINAL_TAIL_BYTES, NULL, NULL, NULL) ==
         PO32_ERR_FRAME);

  memcpy(mutated, frame, frame_len);
  mutated[PO32_PREAMBLE_BYTES + 2u] = 0u;
  assert(po32_frame_parse(mutated, frame_len, NULL, NULL, NULL) == PO32_ERR_FRAME);

  po32_builder_reset(NULL);
  po32_builder_init(NULL, frame, sizeof(frame));
  po32_builder_init(&builder, short_buffer, sizeof(short_buffer));
  assert(builder.length == 0u);

  po32_patch_params_zero(NULL);
  memset(&patch_params, 0xFF, sizeof(patch_params));
  po32_patch_params_zero(&patch_params);
  assert(patch_params.Level == 0.0f);

  po32_morph_pairs_default(NULL, 1u);
  morph_pair.flag = 0u;
  morph_pair.morph = 0u;
  po32_morph_pairs_default(&morph_pair, 1u);
  assert(morph_pair.flag == 0x80u);
  assert(morph_pair.morph == 0x01u);

  po32_pattern_init(NULL, 1u);
  po32_pattern_clear(NULL);

  po32_pattern_init(&pattern_packet, 7u);
  assert(po32_pattern_clear_trigger(NULL, 0u, 0u) == PO32_ERR_INVALID_ARG);
  status = po32_pattern_clear_step(NULL, 0u);
  assert(status == PO32_ERR_INVALID_ARG);
  assert(po32_pattern_set_accent(NULL, 0u, 1) == PO32_ERR_INVALID_ARG);
  status = po32_pattern_trigger_lane(1u, NULL);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_pattern_trigger_encode(1u, 1u, 0, NULL);
  assert(status == PO32_ERR_INVALID_ARG);

  assert(po32_builder_append_packet(NULL, PO32_TAG_PATCH, frame, 1u, NULL) == PO32_ERR_INVALID_ARG);
  po32_zero(&builder, sizeof(builder));
  builder.buffer = frame;
  builder.capacity = sizeof(frame);
  assert(po32_builder_append_packet(&builder, PO32_TAG_PATCH, frame, 1u, NULL) ==
         PO32_ERR_BUFFER_TOO_SMALL);

  po32_builder_init(&builder, frame, sizeof(frame));
  builder.finished = 1;
  assert(po32_builder_append_packet(&builder, PO32_TAG_PATCH, frame, 1u, NULL) == PO32_ERR_FRAME);

  po32_builder_init(&builder, frame, sizeof(frame));
  assert(po32_builder_append(&builder, NULL) == PO32_ERR_INVALID_ARG);
  assert(po32_builder_append_packet(&builder, PO32_TAG_PATCH, NULL, 1u, NULL) ==
         PO32_ERR_INVALID_ARG);
  assert(po32_builder_append_packet(&builder, PO32_TAG_PATCH, frame, 256u, NULL) == PO32_ERR_RANGE);
  assert(po32_builder_append_packet(&builder, PO32_TAG_RESET, frame, 1u, &packet_offset) ==
         PO32_OK);
  assert(packet_offset == PO32_PREAMBLE_BYTES);
  status = po32_builder_finish(NULL, &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_builder_finish(&builder, &out_len);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &out_len);
  assert(status == PO32_OK);
  assert(out_len == builder.length);

  status = po32_encode_patch(NULL, patch_bytes, sizeof(patch_bytes), &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
  po32_patch_params_zero(&patch_params);
  status = po32_encode_patch(&patch_params, patch_bytes, sizeof(patch_bytes) - 1u, &out_len);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);
  status = po32_decode_patch(NULL, sizeof(patch_bytes), &patch_params);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_decode_patch(patch_bytes, sizeof(patch_bytes) - 1u, &patch_params);
  assert(status == PO32_ERR_INVALID_ARG);

  assert(po32_packet_encode(PO32_TAG_PATCH, NULL, &encoded) == PO32_ERR_INVALID_ARG);
  assert(po32_packet_encode(0xFFFFu, &patch_params, &encoded) == PO32_ERR_INVALID_ARG);
  assert(po32_packet_decode(PO32_TAG_PATCH, NULL, 1u, &patch_packet) == PO32_ERR_INVALID_ARG);
  assert(po32_packet_decode(0xFFFFu, patch_bytes, 1u, &patch_packet) == PO32_ERR_INVALID_ARG);

  memset(patch_bytes, 0u, sizeof(patch_bytes));
  status = po32_patch_packet_decode(patch_bytes, sizeof(patch_bytes) - 1u, &patch_packet);
  assert(status == PO32_ERR_FRAME);
  patch_packet.instrument = 1u;
  patch_packet.side = PO32_PATCH_RIGHT;
  po32_patch_params_zero(&patch_packet.params);
  status = po32_packet_encode(PO32_TAG_PATCH, &patch_packet, &encoded);
  assert(status == PO32_OK);
  status = po32_patch_packet_decode(encoded.payload, encoded.payload_len, &patch_packet);
  assert(status == PO32_OK);
  assert(patch_packet.side == PO32_PATCH_RIGHT);
  patch_bytes[0] = 0x00u;
  status = po32_patch_packet_decode(patch_bytes, sizeof(patch_bytes), &patch_packet);
  assert(status == PO32_ERR_FRAME);

  /* Invalid side prefix (upper nibble not 0x10 or 0x20) with valid length */
  {
    uint8_t bad_side[PO32_PATCH_PAYLOAD_BYTES];
    memset(bad_side, 0u, sizeof(bad_side));
    bad_side[0] = 0x00u;
    status = po32_patch_packet_decode(bad_side, sizeof(bad_side), &patch_packet);
    assert(status == PO32_ERR_FRAME);
  }

  knob_packet.instrument = 1u;
  knob_packet.kind = (po32_knob_kind_t)99;
  knob_packet.value = 42u;
  status = po32_knob_packet_encode(&knob_packet, &encoded);
  assert(status == PO32_ERR_INVALID_ARG);
  status = po32_knob_packet_decode(patch_bytes, 1u, &knob_packet);
  assert(status == PO32_ERR_FRAME);

  status = po32_reset_packet_decode(patch_bytes, 0u, &reset_packet);
  assert(status == PO32_ERR_FRAME);

  memset(&state_packet, 0u, sizeof(state_packet));
  state_packet.pattern_count = PO32_PATTERN_STEP_COUNT + 1u;
  status = po32_state_packet_encode(&state_packet, &encoded);
  assert(status == PO32_ERR_RANGE);

  memset(state_bytes, 0u, sizeof(state_bytes));
  status = po32_state_packet_decode(state_bytes, PO32_STATE_PAYLOAD_MIN_BYTES - 1u, &state_packet);
  assert(status == PO32_ERR_FRAME);
  state_bytes[PO32_STATE_PAYLOAD_MIN_BYTES - 1u] = PO32_PATTERN_STEP_COUNT + 1u;
  status = po32_state_packet_decode(state_bytes, PO32_STATE_PAYLOAD_MIN_BYTES, &state_packet);
  assert(status == PO32_ERR_FRAME);

  po32_pattern_init(&pattern_packet, 3u);
  pattern_packet.steps[0].instrument = 1u;
  pattern_packet.steps[0].fill_rate = 0u;
  pattern_packet.steps[0].accent = 0;
  status = po32_pattern_packet_encode(&pattern_packet, &encoded);
  assert(status == PO32_ERR_RANGE);
  status = po32_pattern_set_trigger(&pattern_packet, 0u, 17u, 1u);
  assert(status == PO32_ERR_RANGE);

  memset(pattern_bytes, 0u, sizeof(pattern_bytes));
  status = po32_pattern_packet_decode(pattern_bytes, sizeof(pattern_bytes) - 1u, &pattern_packet);
  assert(status == PO32_ERR_FRAME);
  patch_bytes[0] = 0x00u;
  status = po32_patch_packet_decode(patch_bytes, sizeof(patch_bytes), &patch_packet);
  assert(status == PO32_ERR_FRAME);
  pattern_bytes[0] = 9u;
  pattern_bytes[1] = 0x40u;
  status = po32_pattern_packet_decode(pattern_bytes, sizeof(pattern_bytes), &pattern_packet);
  assert(status == PO32_OK);
  assert(pattern_packet.steps[0].instrument == 0u);
  assert(pattern_packet.steps[0].fill_rate == 0u);
  assert(pattern_packet.steps[0].accent == 0);

  assert(po32_render_sample_count(4u, 0u) == 0u);
  po32_modulator_init(NULL, frame, frame_len, 44100u);
  po32_modulator_init(&modulator, NULL, frame_len, 44100u);
  assert(modulator.frame == NULL);
  po32_modulator_reset(NULL);
  status = po32_modulator_render_f32(NULL, &sample, 1u, &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
  po32_zero(&modulator, sizeof(modulator));
  status = po32_modulator_render_f32(&modulator, &sample, 1u, &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
  po32_modulator_init(&modulator, frame, frame_len, 44100u);
  status = po32_modulator_render_f32(&modulator, &sample, 0u, &out_len);
  assert(status == PO32_OK);
  assert(out_len == 0u);
  assert(po32_render_dpsk_f32(NULL, frame_len, 44100u, &sample, 1u) == PO32_ERR_INVALID_ARG);
  assert(po32_render_dpsk_f32(frame, frame_len, 0u, &sample, 1u) == PO32_ERR_INVALID_ARG);
  assert(po32_render_dpsk_f32(frame, frame_len, 44100u, &sample, 1u) == PO32_ERR_BUFFER_TOO_SMALL);
}

static void test_demod_on_byte_paths(void) {
  uint8_t frame[512];
  uint8_t body[256];
  uint8_t tail[PO32_FINAL_TAIL_BYTES];
  po32_packet_t packet;
  po32_demodulator_t demod;
  size_t frame_len;
  size_t body_len;
  size_t consumed = 0u;
  uint16_t tail_state = PO32_INITIAL_STATE;
  int callback_count = 0;
  int stop = 0;
  po32_status_t status;

  make_demo_patch_packet(&packet);
  frame_len = build_demo_frame(frame, sizeof(frame), &packet);
  body_len = frame_len - PO32_PREAMBLE_BYTES - PO32_FINAL_TAIL_BYTES;
  memcpy(body, frame + PO32_PREAMBLE_BYTES, body_len);
  memcpy(tail, frame + frame_len - PO32_FINAL_TAIL_BYTES, PO32_FINAL_TAIL_BYTES);

  status = po32_packet_decode_bytes(body, body_len, &tail_state, &consumed, &packet);
  assert(status == PO32_OK);
  assert(consumed == body_len);

  po32_demodulator_desync(NULL);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.work_len = PO32_DEMOD_WORK_SIZE;
  demod.current_byte = 0u;
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.synced == 0);
  assert(demod.work_len == 0u);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = tail_state;
  memcpy(demod.work, tail, 3u);
  demod.work_len = 3u;
  demod.current_byte = tail[3];
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.done == 0);
  assert(demod.work_len == 4u);
  assert(demod.synced == 1);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = tail_state;
  memcpy(demod.work, tail, 4u);
  demod.work_len = 4u;
  demod.current_byte = (uint8_t)(tail[4] ^ 0x01u);
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.synced == 0);
  assert(demod.work_len == 0u);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = tail_state;
  memcpy(demod.work, tail, 4u);
  demod.work_len = 4u;
  demod.current_byte = tail[4];
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.done == 1);
  assert(stop == 1);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = PO32_INITIAL_STATE;
  demod.work[0] = body[0];
  demod.work[1] = body[1];
  demod.work_len = 2u;
  demod.current_byte = 0u;
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.synced == 0);
  assert(demod.work_len == 0u);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = PO32_INITIAL_STATE;
  demod.work[0] = body[0];
  demod.work[1] = body[1];
  demod.work_len = 2u;
  demod.current_byte = body[2];
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.synced == 1);
  assert(demod.work_len == 3u);
  assert(stop == 0);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = PO32_INITIAL_STATE;
  memcpy(demod.work, body, body_len);
  demod.work_len = body_len;
  demod.current_byte = 0xAAu;
  stop = 0;
  po32_demod_on_byte(&demod, NULL, NULL, &stop);
  assert(demod.synced == 0);
  assert(demod.work_len == 0u);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = PO32_INITIAL_STATE;
  memcpy(demod.work, body, body_len - 1u);
  demod.work_len = body_len - 1u;
  demod.current_byte = body[body_len - 1u];
  callback_count = 0;
  stop = 0;
  po32_demod_on_byte(&demod, stop_packet_callback, &callback_count, &stop);
  assert(callback_count == 1);
  assert(stop == 1);
  assert(demod.packet_count == 0);

  po32_demodulator_init(&demod, 44100.0f);
  demod.synced = 1;
  demod.crc_state = PO32_INITIAL_STATE;
  memcpy(demod.work, body, body_len - 1u);
  demod.work_len = body_len - 1u;
  demod.current_byte = body[body_len - 1u];
  callback_count = 0;
  stop = 0;
  po32_demod_on_byte(&demod, count_packet_callback, &callback_count, &stop);
  assert(callback_count == 1);
  assert(stop == 0);
  assert(demod.packet_count == 1);
  assert(demod.work_len == 0u);
  assert(demod.crc_state == tail_state);
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

  po32_builder_init(&builder, frame, PO32_PREAMBLE_BYTES);
  ctx.builder = &builder;
  ctx.status = PO32_OK;
  demod.done = 1;
  demod.packet_count = 1;
  status = po32_decode_finalize(&demod, &ctx, &result, &out_len);
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);

  status = po32_decode_f32(NULL, 1u, 44100.0f, &result, frame, sizeof(frame), &out_len);
  assert(status == PO32_ERR_INVALID_ARG);
}

int main(void) {
  test_tag_and_tail_helpers();
  test_packet_decode_internals();
  test_final_tail_and_trailer_corruption();
  test_builder_internal_helpers();
  test_builder_finish_edge_cases();
  test_public_guard_paths();
  test_decode_internal_helpers();
  test_demod_on_byte_paths();
  return 0;
}
