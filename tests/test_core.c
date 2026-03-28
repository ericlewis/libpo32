#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "po32.h"

typedef struct {
  int packet_count;
  uint16_t tags[8];
  size_t lens[8];
  uint8_t first_payload_byte[8];
} parse_state_t;

static int collect_packets(const po32_packet_t *packet, void *user) {
  parse_state_t *state = (parse_state_t *)user;
  int index = state->packet_count;

  assert(index < (int)(sizeof(state->tags) / sizeof(state->tags[0])));

  state->tags[index] = packet->tag_code;
  state->lens[index] = packet->payload_len;
  state->first_payload_byte[index] = packet->payload_len > 0u ? packet->payload[0] : 0u;
  state->packet_count += 1;
  return 0;
}

static void assert_frame_packets(const uint8_t *frame, size_t frame_len,
                                 const uint16_t *expected_tags, const size_t *expected_lens,
                                 int expected_packet_count) {
  parse_state_t state;
  po32_final_tail_t tail;
  po32_status_t status;

  (void)expected_tags;
  (void)expected_lens;

  memset(&state, 0, sizeof(state));
  status = po32_frame_parse(frame, frame_len, collect_packets, &state, &tail);
  (void)status;
  assert(status == PO32_OK);
  assert(state.packet_count == expected_packet_count);
  for (int i = 0; i < expected_packet_count; ++i) {
    assert(state.tags[i] == expected_tags[i]);
    assert(state.lens[i] == expected_lens[i]);
  }
  assert(tail.marker_c3 == 0xC3u);
  assert(tail.marker_71 == 0x71u);
}

static void test_patch_encode_decode(void) {
  po32_patch_params_t in_params;
  po32_patch_packet_t in_pkt;
  po32_patch_packet_t out_pkt;
  po32_packet_t dpkt;
  po32_status_t status;

  memset(&in_params, 0, sizeof(in_params));
  in_params.OscWave = 1.0f;
  in_params.OscFreq = 0.25f;
  in_params.Level = 0.875f;

  in_pkt.instrument = 1u;
  in_pkt.side = PO32_PATCH_LEFT;
  in_pkt.params = in_params;

  status = po32_packet_encode(PO32_TAG_PATCH, &in_pkt, &dpkt);
  (void)status;
  (void)dpkt;
  assert(status == PO32_OK);
  assert(dpkt.payload_len == PO32_PATCH_PAYLOAD_BYTES);

  status = po32_packet_decode(PO32_TAG_PATCH, dpkt.payload, dpkt.payload_len, &out_pkt);
  assert(status == PO32_OK);
  assert(fabsf(out_pkt.params.OscWave - 1.0f) < 0.0001f);
  assert(fabsf(out_pkt.params.OscFreq - 0.25f) < 0.0001f);
  assert(fabsf(out_pkt.params.Level - 0.875f) < 0.0001f);
}

static void test_patch_parse_mtdrum_text(void) {
#if PO32_ENABLE_MTDRUM_IMPORT
  static const char text[] =
      "OscWave: Triangle\n"
      "OscFreq: 536.918 Hz\n"
      "OscAtk: 0 ms\n"
      "OscDcy: 160.811 ms\n"
      "ModMode: Noise FM\n"
      "ModRate: 244.983 Hz\n"
      "ModAmt: 5.896 st\n"
      "NFilMod: HP (hi-pass)\n"
      "NFilFrq: 1840.23 Hz\n"
      "NFilQ: 11.407\n"
      "NEnvMod: Modulate\n"
      "NEnvAtk: 0 ms\n"
      "NEnvDcy: 31.522 ms\n"
      "Mix: 40.626% osc, 59.374% noise\n"
      "DistAmt: 27.083%\n"
      "EQFreq: 1497.21 Hz\n"
      "EQGain: 15.417 dB\n"
      "Level: -2.645 dB\n"
      "OscVel: 0.000%\n"
      "NVel: 0.000%\n"
      "ModVel: 0.000%\n";
  po32_patch_params_t params;
  po32_status_t status;

  memset(&params, 0, sizeof(params));
  status = po32_patch_parse_mtdrum_text(text, sizeof(text) - 1u, &params);
  assert(status == PO32_OK);
  assert(fabsf(params.OscWave - 0.5f) < 0.0001f);
  assert(fabsf(params.OscFreq - 0.47629266f) < 0.0001f);
  assert(fabsf(params.NFilFrq - 0.65461403f) < 0.0001f);
  assert(fabsf(params.Level - 0.79358548f) < 0.0002f);
#else
  printf("  skipping mtdrum importer test (disabled)\n");
#endif
}

static void test_builder_guards(void) {
  po32_builder_t builder;
  uint8_t tiny_buffer[32];
  uint8_t payload[1] = {0u};
  size_t frame_len = 0u;

  (void)payload;
  (void)frame_len;

  po32_builder_init(&builder, tiny_buffer, sizeof(tiny_buffer));
  assert(builder.length == 0u);
  assert(builder.state == PO32_INITIAL_STATE);
  assert(builder.finished == 0);

  assert(po32_builder_append_packet(&builder, PO32_TAG_PATCH, payload, sizeof(payload), NULL) ==
         PO32_ERR_BUFFER_TOO_SMALL);
  assert(po32_builder_finish(&builder, &frame_len) == PO32_ERR_BUFFER_TOO_SMALL);
}

static void test_null_payload_rejected(void) {
  po32_builder_t builder;
  uint8_t frame[256];

  po32_builder_init(&builder, frame, sizeof(frame));
  assert(po32_builder_append_packet(&builder, PO32_TAG_PATCH, NULL, 1u, NULL) ==
         PO32_ERR_INVALID_ARG);
}

static void test_frame_build_and_parse(void) {
  po32_patch_params_t patch;
  po32_builder_t builder;
  po32_final_tail_t tail;
  parse_state_t state;
  po32_packet_t dpkt;
  uint8_t frame[512];
  size_t frame_len = 0u;
  po32_status_t status;

  (void)status;
  (void)dpkt;
  (void)tail;

  memset(&state, 0, sizeof(state));
  memset(&patch, 0, sizeof(patch));
  patch.OscFreq = 0.18f;
  patch.OscDcy = 0.42f;
  patch.Level = 0.88f;

  {
    po32_patch_packet_t pkt;
    pkt.instrument = 1u;
    pkt.side = PO32_PATCH_LEFT;
    pkt.params = patch;
    status = po32_packet_encode(PO32_TAG_PATCH, &pkt, &dpkt);
  }
  assert(status == PO32_OK);
  assert(dpkt.payload_len == 43u);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert(frame_len > PO32_PREAMBLE_BYTES);
  assert(memcmp(frame, po32_preamble_bytes(), PO32_PREAMBLE_BYTES) == 0);

  status = po32_frame_parse(frame, frame_len, collect_packets, &state, &tail);
  assert(status == PO32_OK);
  assert(state.packet_count == 1);
  assert(state.tags[0] == PO32_TAG_PATCH);
  assert(state.lens[0] == 43u);
  assert(state.first_payload_byte[0] == 0x10u);
  assert(tail.marker_c3 == 0xC3u);
  assert(tail.marker_71 == 0x71u);
}

static void test_streaming_modulator_matches_block_render(void) {
  po32_patch_params_t patch;
  po32_builder_t builder;
  po32_packet_t dpkt;
  po32_modulator_t modulator;
  uint8_t frame[512];
  size_t frame_len = 0u;
  size_t sample_count;
  size_t streamed_total = 0u;
  float *block_samples;
  float *stream_samples;
  po32_status_t status;

  memset(&patch, 0, sizeof(patch));
  patch.OscFreq = 0.18f;
  patch.OscDcy = 0.42f;
  patch.Level = 0.88f;

  {
    po32_patch_packet_t pkt;
    pkt.instrument = 1u;
    pkt.side = PO32_PATCH_LEFT;
    pkt.params = patch;
    status = po32_packet_encode(PO32_TAG_PATCH, &pkt, &dpkt);
  }
  assert(status == PO32_OK);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);

  sample_count = po32_render_sample_count(frame_len, 44100u);
  block_samples = (float *)malloc(sample_count * sizeof(*block_samples));
  stream_samples = (float *)malloc(sample_count * sizeof(*stream_samples));
  assert(block_samples != NULL);
  assert(stream_samples != NULL);

  status = po32_render_dpsk_f32(frame, frame_len, 44100u, block_samples, sample_count);
  assert(status == PO32_OK);

  po32_modulator_init(&modulator, frame, frame_len, 44100u);
  assert(po32_modulator_done(&modulator) == 0);
  assert(po32_modulator_samples_remaining(&modulator) == sample_count);

  while (!po32_modulator_done(&modulator)) {
    size_t chunk_len = 0u;
    const size_t chunk_cap = 257u;
    status = po32_modulator_render_f32(&modulator,
                                       stream_samples + streamed_total,
                                       chunk_cap,
                                       &chunk_len);
    assert(status == PO32_OK);
    assert(chunk_len > 0u);
    streamed_total += chunk_len;
  }

  assert(streamed_total == sample_count);
  assert(po32_modulator_samples_remaining(&modulator) == 0u);

  for (size_t i = 0u; i < sample_count; ++i) {
    assert(fabsf(block_samples[i] - stream_samples[i]) < 1.0e-6f);
  }

  po32_modulator_reset(&modulator);
  assert(po32_modulator_done(&modulator) == 0);
  assert(po32_modulator_samples_remaining(&modulator) == sample_count);

  free(block_samples);
  free(stream_samples);
}

static void test_extended_payload_encoders(void) {
  po32_morph_pair_t state_pairs[PO32_STATE_MORPH_PAIR_COUNT];
  po32_morph_pair_t pattern_pairs[PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT];
  po32_packet_t dpkt;
  po32_status_t status;

  memset(state_pairs, 0, sizeof(state_pairs));
  memset(pattern_pairs, 0, sizeof(pattern_pairs));
  for (size_t _mi = 0; _mi < (size_t)(PO32_STATE_MORPH_PAIR_COUNT); _mi++) {
    state_pairs[_mi].flag = 0x80u;
    state_pairs[_mi].morph = 0x01u;
  }
  for (size_t _mi = 0; _mi < (size_t)(PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT); _mi++) {
    pattern_pairs[_mi].flag = 0x80u;
    pattern_pairs[_mi].morph = 0x01u;
  }

  assert(state_pairs[0].flag == 0x80u);
  assert(state_pairs[0].morph == 0x01u);
  assert(state_pairs[PO32_STATE_MORPH_PAIR_COUNT - 1u].flag == 0x80u);
  assert(state_pairs[PO32_STATE_MORPH_PAIR_COUNT - 1u].morph == 0x01u);

  {
    po32_knob_packet_t knob_pkt = {5u, PO32_KNOB_PITCH, 128u};
    status = po32_packet_encode(PO32_TAG_KNOB, &knob_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 2u);
  assert(dpkt.payload[0] == 0x04u);
  assert(dpkt.payload[1] == 128u);

  {
    po32_knob_packet_t knob_pkt = {5u, PO32_KNOB_MORPH, 1u};
    status = po32_packet_encode(PO32_TAG_KNOB, &knob_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 2u);
  assert(dpkt.payload[0] == 0x14u);
  assert(dpkt.payload[1] == 1u);

  {
    po32_reset_packet_t reset_pkt = {5u};
    status = po32_packet_encode(PO32_TAG_RESET, &reset_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 1u);
  assert(dpkt.payload[0] == 0x34u);

  {
    po32_state_packet_t state_pkt;
    memset(&state_pkt, 0, sizeof(state_pkt));
    for (size_t _si = 0; _si < PO32_STATE_MORPH_PAIR_COUNT; _si++) {
      state_pkt.morph_pairs[_si].flag = 0x80u;
      state_pkt.morph_pairs[_si].morph = 0x01u;
    }
    state_pkt.tempo = 120u;
    state_pkt.swing_times_12 = 12u;
    state_pkt.pattern_numbers[0] = 0u;
    state_pkt.pattern_numbers[1] = 3u;
    state_pkt.pattern_count = 2u;
    status = po32_packet_encode(PO32_TAG_STATE, &state_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 37u);
  assert(dpkt.payload[32] == 120u);
  assert(dpkt.payload[33] == 12u);
  assert(dpkt.payload[34] == 2u);
  assert(dpkt.payload[35] == 0u);
  assert(dpkt.payload[36] == 3u);

  {
    po32_pattern_packet_t pat_pkt;
    memset(&pat_pkt, 0, sizeof(pat_pkt));
    pat_pkt.pattern_number = 3u;
    /* steps[] left zeroed = all empty */
    memcpy(pat_pkt.morph_lanes, pattern_pairs, sizeof(pat_pkt.morph_lanes));
    pat_pkt.accent_bits = 0xA55Au;
    status = po32_packet_encode(PO32_TAG_PATTERN, &pat_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == PO32_PATTERN_PAYLOAD_BYTES);
  assert(dpkt.payload[0] == 3u);
  assert(dpkt.payload[PO32_PATTERN_PAYLOAD_BYTES - 2u] == 0x5Au);
  assert(dpkt.payload[PO32_PATTERN_PAYLOAD_BYTES - 1u] == 0xA5u);
  (void)status;
  (void)dpkt;
}

static void test_pattern_trigger_helpers(void) {
  for (uint8_t instrument = 1u; instrument <= 16u; ++instrument) {
    uint8_t lane = 0u;
    uint8_t trigger = 0u;
    uint8_t decoded_instrument = 0u;
    uint8_t fill_rate = 0u;
    int accent = 0;
    po32_status_t status = po32_pattern_trigger_lane(instrument, &lane);
    assert(status == PO32_OK);
    assert(lane == (uint8_t)((instrument - 1u) & 3u));

    status = po32_pattern_trigger_encode(instrument, 1u, 1, &trigger);
    assert(status == PO32_OK);

    status = po32_pattern_trigger_decode(lane, trigger, &decoded_instrument, &fill_rate, &accent);
    assert(status == PO32_OK);
    assert(decoded_instrument == instrument);
    assert(fill_rate == 1u);
    assert(accent == 1);
  }

  /* A low-nibble value of 0 means "empty" on the wire, so encode rejects it. */
  for (uint8_t instrument = 1u; instrument <= 16u; ++instrument) {
    uint8_t trigger = 0u;
    assert(po32_pattern_trigger_encode(instrument, 0u, 0, &trigger) == PO32_ERR_RANGE);
    assert(po32_pattern_trigger_encode(instrument, 0u, 1, &trigger) == PO32_ERR_RANGE);
  }

  {
    uint8_t instrument = 99u;
    uint8_t fill_rate = 99u;
    int accent = 99;
    assert(po32_pattern_trigger_encode(0u, 1u, 0, NULL) == PO32_ERR_INVALID_ARG);
    assert(po32_pattern_trigger_encode(0u, 1u, 0, &instrument) == PO32_ERR_RANGE);
    assert(po32_pattern_trigger_lane(17u, &instrument) == PO32_ERR_RANGE);
    assert(po32_pattern_trigger_decode(4u, 1u, &instrument, &fill_rate, &accent) == PO32_ERR_RANGE);
    assert(po32_pattern_trigger_decode(0u, 0x40u, &instrument, &fill_rate, &accent) == PO32_ERR_RANGE);
    /* Any zero low nibble is empty on the wire. */
    assert(po32_pattern_trigger_decode(0u, 0u, &instrument, &fill_rate, &accent) == PO32_OK);
    assert(instrument == 0u);
    assert(fill_rate == 0u);
    assert(accent == 0);
    instrument = 99u;
    fill_rate = 99u;
    accent = 99;
    assert(po32_pattern_trigger_decode(0u, 0x80u, &instrument, &fill_rate, &accent) == PO32_OK);
    assert(instrument == 0u);
    assert(fill_rate == 0u);
    assert(accent == 0);
  }

  /* steps[] round-trip through pattern packet encode/decode. */
  {
    po32_pattern_packet_t in_pkt, out_pkt;
    po32_packet_t dpkt;
    po32_status_t status;
    memset(&in_pkt, 0, sizeof(in_pkt));
    in_pkt.pattern_number = 7u;
    /* inst 9 (lane 0) fill=2, inst 2 (lane 1) fill=3, inst 13 (lane 0) fill=1 accent */
    in_pkt.steps[0 * 16 + 0].instrument = 9u;
    in_pkt.steps[0 * 16 + 0].fill_rate = 2u;
    in_pkt.steps[1 * 16 + 4].instrument = 2u;
    in_pkt.steps[1 * 16 + 4].fill_rate = 3u;
    in_pkt.steps[0 * 16 + 8].instrument = 13u;
    in_pkt.steps[0 * 16 + 8].fill_rate = 1u;
    in_pkt.steps[0 * 16 + 8].accent = 1;
    po32_morph_pairs_default(in_pkt.morph_lanes, PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT);
    in_pkt.accent_bits = (uint16_t)(1u << 8);

    status = po32_packet_encode(PO32_TAG_PATTERN, &in_pkt, &dpkt);
    assert(status == PO32_OK);
    assert(dpkt.payload_len == PO32_PATTERN_PAYLOAD_BYTES);

    memset(&out_pkt, 0, sizeof(out_pkt));
    status = po32_packet_decode(PO32_TAG_PATTERN, dpkt.payload, dpkt.payload_len, &out_pkt);
    assert(status == PO32_OK);
    assert(out_pkt.pattern_number == 7u);
    assert(out_pkt.steps[0 * 16 + 0].instrument == 9u);
    assert(out_pkt.steps[0 * 16 + 0].fill_rate == 2u);
    assert(out_pkt.steps[1 * 16 + 4].instrument == 2u);
    assert(out_pkt.steps[1 * 16 + 4].fill_rate == 3u);
    assert(out_pkt.steps[0 * 16 + 8].instrument == 13u);
    assert(out_pkt.steps[0 * 16 + 8].fill_rate == 1u);
    assert(out_pkt.steps[0 * 16 + 8].accent == 1);
    /* Empty positions stay empty. */
    assert(out_pkt.steps[2 * 16 + 0].instrument == 0u);
    assert(out_pkt.accent_bits == (uint16_t)(1u << 8));
    /* Pattern payload is lane-chunked: 16 triggers, then 32 morph bytes, per lane. */
    assert(dpkt.payload[1u + 0u] == 0x22u);
    assert(dpkt.payload[1u + 8u] == 0xB1u);
    assert(dpkt.payload[1u + 16u] == 0x80u);
    assert(dpkt.payload[1u + 17u] == 0x01u);
    assert(dpkt.payload[1u + 48u + 4u] == 0x03u);
    assert(dpkt.payload[1u + 48u + 16u] == 0x80u);
    assert(dpkt.payload[1u + 48u + 17u] == 0x01u);
  }
}

static void test_pattern_builder_helpers(void) {
  po32_pattern_packet_t pattern;
  size_t index;

  po32_pattern_init(&pattern, 12u);
  assert(pattern.pattern_number == 12u);
  assert(pattern.accent_bits == 0u);
  for (size_t i = 0u; i < PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT; ++i) {
    assert(pattern.steps[i].instrument == 0u);
    assert(pattern.steps[i].fill_rate == 0u);
    assert(pattern.steps[i].accent == 0);
    assert(pattern.morph_lanes[i].flag == 0u);
    assert(pattern.morph_lanes[i].morph == 0u);
  }

  assert(po32_pattern_set_accent(&pattern, 0u, 1) == PO32_OK);
  assert(pattern.accent_bits == 0x0001u);

  assert(po32_pattern_set_trigger(&pattern, 0u, 1u, 1u) == PO32_OK);
  assert(po32_pattern_set_trigger(&pattern, 0u, 2u, 3u) == PO32_OK);

  index = 0u * 16u + 0u;
  assert(pattern.steps[index].instrument == 1u);
  assert(pattern.steps[index].fill_rate == 1u);
  assert(pattern.steps[index].accent == 1);
  assert(pattern.morph_lanes[index].flag == 0x80u);
  assert(pattern.morph_lanes[index].morph == 0x01u);

  index = 1u * 16u + 0u;
  assert(pattern.steps[index].instrument == 2u);
  assert(pattern.steps[index].fill_rate == 3u);
  assert(pattern.steps[index].accent == 1);
  assert(pattern.morph_lanes[index].flag == 0x80u);
  assert(pattern.morph_lanes[index].morph == 0x01u);

  assert(po32_pattern_clear_trigger(&pattern, 0u, 0u) == PO32_OK);
  index = 0u * 16u + 0u;
  assert(pattern.steps[index].instrument == 0u);
  assert(pattern.steps[index].fill_rate == 0u);
  assert(pattern.steps[index].accent == 0);
  assert(pattern.morph_lanes[index].flag == 0u);
  assert(pattern.morph_lanes[index].morph == 0u);
  assert(pattern.accent_bits == 0x0001u);

  assert(po32_pattern_set_accent(&pattern, 0u, 0) == PO32_OK);
  index = 1u * 16u + 0u;
  assert(pattern.steps[index].accent == 0);
  assert(pattern.accent_bits == 0u);

  assert(po32_pattern_clear_step(&pattern, 0u) == PO32_OK);
  for (size_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    index = lane * 16u + 0u;
    assert(pattern.steps[index].instrument == 0u);
    assert(pattern.morph_lanes[index].flag == 0u);
  }

  assert(po32_pattern_set_trigger(&pattern, 15u, 16u, 1u) == PO32_OK);
  assert(po32_pattern_set_accent(&pattern, 15u, 1) == PO32_OK);
  index = 3u * 16u + 15u;
  assert(pattern.steps[index].instrument == 16u);
  assert(pattern.steps[index].fill_rate == 1u);
  assert(pattern.steps[index].accent == 1);
  assert(pattern.accent_bits == (uint16_t)(1u << 15));

  po32_pattern_clear(&pattern);
  assert(pattern.pattern_number == 12u);
  assert(pattern.accent_bits == 0u);
  for (size_t i = 0u; i < PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT; ++i) {
    assert(pattern.steps[i].instrument == 0u);
    assert(pattern.morph_lanes[i].flag == 0u);
  }

  assert(po32_pattern_set_trigger(NULL, 0u, 1u, 1u) == PO32_ERR_INVALID_ARG);
  assert(po32_pattern_set_trigger(&pattern, 16u, 1u, 1u) == PO32_ERR_RANGE);
  assert(po32_pattern_set_trigger(&pattern, 0u, 1u, 0u) == PO32_ERR_RANGE);
  assert(po32_pattern_clear_trigger(&pattern, 0u, 4u) == PO32_ERR_RANGE);
  assert(po32_pattern_clear_step(&pattern, 16u) == PO32_ERR_RANGE);
  assert(po32_pattern_set_accent(&pattern, 16u, 1) == PO32_ERR_RANGE);
}

static void test_builder_exact_capacity_boundary(void) {
  po32_patch_params_t patch;
  po32_builder_t builder;
  po32_packet_t dpkt = {0};
  uint8_t frame[512];
  uint8_t *exact_buffer = NULL;
  uint8_t *small_buffer = NULL;
  size_t frame_len = 0u;
  po32_status_t status;

  memset(&patch, 0, sizeof(patch));
  patch.OscFreq = 0.18f;
  patch.Level = 0.88f;

  {
    po32_patch_packet_t pkt;
    pkt.instrument = 1u;
    pkt.side = PO32_PATCH_LEFT;
    pkt.params = patch;
    status = po32_packet_encode(PO32_TAG_PATCH, &pkt, &dpkt);
    assert(status == PO32_OK);
  }

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);

  exact_buffer = (uint8_t *)malloc(frame_len);
  small_buffer = (uint8_t *)malloc(frame_len - 1u);
  assert(exact_buffer != NULL);
  assert(small_buffer != NULL);

  po32_builder_init(&builder, exact_buffer, frame_len);
  status = po32_builder_append(&builder, &dpkt);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);

  po32_builder_init(&builder, small_buffer, frame_len - 1u);
  status = po32_builder_append(&builder, &dpkt);
  if (status == PO32_OK) {
    status = po32_builder_finish(&builder, NULL);
  }
  assert(status == PO32_ERR_BUFFER_TOO_SMALL);

  free(exact_buffer);
  free(small_buffer);
}

static void test_state_payload_lengths(void) {
  po32_morph_pair_t pairs[PO32_STATE_MORPH_PAIR_COUNT];
  po32_packet_t dpkt = {0};
  po32_status_t status;
  uint8_t pattern_numbers[PO32_PATTERN_STEP_COUNT];
  size_t i;

  for (size_t _mi = 0; _mi < (size_t)(PO32_STATE_MORPH_PAIR_COUNT); _mi++) {
    pairs[_mi].flag = 0x80u;
    pairs[_mi].morph = 0x01u;
  }
  for (i = 0u; i < PO32_PATTERN_STEP_COUNT; ++i) {
    pattern_numbers[i] = (uint8_t)i;
  }

  {
    po32_state_packet_t state_pkt;
    memset(&state_pkt, 0, sizeof(state_pkt));
    memcpy(state_pkt.morph_pairs, pairs, sizeof(state_pkt.morph_pairs));
    state_pkt.tempo = 120u;
    state_pkt.swing_times_12 = 0u;
    state_pkt.pattern_count = 0u;
    status = po32_packet_encode(PO32_TAG_STATE, &state_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 35u);
  assert(dpkt.payload[32] == 120u);
  assert(dpkt.payload[34] == 0u);

  {
    po32_state_packet_t state_pkt;
    memset(&state_pkt, 0, sizeof(state_pkt));
    memcpy(state_pkt.morph_pairs, pairs, sizeof(state_pkt.morph_pairs));
    state_pkt.tempo = 120u;
    state_pkt.swing_times_12 = 0u;
    state_pkt.pattern_numbers[0] = pattern_numbers[0];
    state_pkt.pattern_count = 1u;
    status = po32_packet_encode(PO32_TAG_STATE, &state_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 36u);
  assert(dpkt.payload[34] == 1u);
  assert(dpkt.payload[35] == 0u);

  {
    po32_state_packet_t state_pkt;
    memset(&state_pkt, 0, sizeof(state_pkt));
    memcpy(state_pkt.morph_pairs, pairs, sizeof(state_pkt.morph_pairs));
    state_pkt.tempo = 120u;
    state_pkt.swing_times_12 = 0u;
    memcpy(state_pkt.pattern_numbers, pattern_numbers, PO32_PATTERN_STEP_COUNT);
    state_pkt.pattern_count = PO32_PATTERN_STEP_COUNT;
    status = po32_packet_encode(PO32_TAG_STATE, &state_pkt, &dpkt);
    assert(status == PO32_OK);
  }
  assert(dpkt.payload_len == 35u + PO32_PATTERN_STEP_COUNT);
  assert(dpkt.payload[34] == PO32_PATTERN_STEP_COUNT);
  assert(dpkt.payload[dpkt.payload_len - 1u] == 15u);
  (void)status;
  (void)dpkt;
}

static void test_known_transfer_shapes(void) {
  po32_patch_params_t patch_left;
  po32_patch_params_t patch_right;
  po32_morph_pair_t pattern_pairs[PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT];
  po32_builder_t builder;
  uint8_t frame[2048];
  po32_packet_t dpkt_patch_left;
  po32_packet_t dpkt_patch_right;
  po32_packet_t dpkt_knob_fixed;
  po32_packet_t dpkt_knob_morph;
  po32_packet_t dpkt_reset;
  po32_packet_t dpkt_pattern;
  po32_packet_t dpkt_state;
  size_t frame_len = 0u;
  po32_status_t status;
  const uint16_t sound_tags[4] = {PO32_TAG_PATCH, PO32_TAG_PATCH, PO32_TAG_KNOB, PO32_TAG_KNOB};
  const size_t sound_lens[4] = {43u, 43u, 2u, 2u};
  const uint16_t pattern_only_tags[1] = {PO32_TAG_PATTERN};
  const size_t pattern_only_lens[1] = {PO32_PATTERN_PAYLOAD_BYTES};
  const uint16_t pattern_state_tags[2] = {PO32_TAG_PATTERN, PO32_TAG_STATE};
  const size_t pattern_state_lens[2] = {PO32_PATTERN_PAYLOAD_BYTES, 36u};
  const uint16_t knob_only_tags[1] = {PO32_TAG_KNOB};
  const size_t knob_only_lens[1] = {2u};
  const uint16_t state_only_tags[1] = {PO32_TAG_STATE};
  const size_t state_only_lens[1] = {36u};
  const uint16_t patch_left_only_tags[1] = {PO32_TAG_PATCH};
  const size_t patch_left_only_lens[1] = {43u};
  const uint16_t patch_right_only_tags[1] = {PO32_TAG_PATCH};
  const size_t patch_right_only_lens[1] = {43u};
  const uint16_t reset_tags[1] = {PO32_TAG_RESET};
  const size_t reset_lens[1] = {1u};

  memset(&patch_left, 0, sizeof(patch_left));
  memset(&patch_right, 0, sizeof(patch_right));
  memset(pattern_pairs, 0, sizeof(pattern_pairs));
  patch_left.OscFreq = 0.18f;
  patch_left.Level = 0.88f;
  patch_right.OscFreq = 0.41f;
  patch_right.Level = 0.67f;
  for (size_t _mi = 0; _mi < (size_t)(PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT); _mi++) {
    pattern_pairs[_mi].flag = 0x80u;
    pattern_pairs[_mi].morph = 0x01u;
  }

  {
    po32_patch_packet_t pkt = {1u, PO32_PATCH_LEFT, patch_left};
    status = po32_packet_encode(PO32_TAG_PATCH, &pkt, &dpkt_patch_left);
    assert(status == PO32_OK);
  }
  {
    po32_patch_packet_t pkt = {1u, PO32_PATCH_RIGHT, patch_right};
    status = po32_packet_encode(PO32_TAG_PATCH, &pkt, &dpkt_patch_right);
    assert(status == PO32_OK);
  }
  {
    po32_knob_packet_t pkt = {1u, PO32_KNOB_PITCH, 128u};
    status = po32_packet_encode(PO32_TAG_KNOB, &pkt, &dpkt_knob_fixed);
    assert(status == PO32_OK);
  }
  {
    po32_knob_packet_t pkt = {1u, PO32_KNOB_MORPH, 1u};
    status = po32_packet_encode(PO32_TAG_KNOB, &pkt, &dpkt_knob_morph);
    assert(status == PO32_OK);
  }
  {
    po32_reset_packet_t pkt = {1u};
    status = po32_packet_encode(PO32_TAG_RESET, &pkt, &dpkt_reset);
    assert(status == PO32_OK);
  }
  {
    po32_pattern_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.pattern_number = 3u;
    /* steps[] left zeroed = all empty */
    memcpy(pkt.morph_lanes, pattern_pairs, sizeof(pkt.morph_lanes));
    pkt.accent_bits = 0u;
    status = po32_packet_encode(PO32_TAG_PATTERN, &pkt, &dpkt_pattern);
    assert(status == PO32_OK);
  }
  {
    po32_state_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    for (size_t _si = 0; _si < PO32_STATE_MORPH_PAIR_COUNT; _si++) {
      pkt.morph_pairs[_si].flag = 0x80u;
      pkt.morph_pairs[_si].morph = 0x01u;
    }
    pkt.tempo = 120u;
    pkt.swing_times_12 = 0u;
    pkt.pattern_numbers[0] = 3u;
    pkt.pattern_count = 1u;
    status = po32_packet_encode(PO32_TAG_STATE, &pkt, &dpkt_state);
    assert(status == PO32_OK);
  }

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_patch_left);
  assert(status == PO32_OK);
  status = po32_builder_append(&builder, &dpkt_patch_right);
  assert(status == PO32_OK);
  status = po32_builder_append(&builder, &dpkt_knob_fixed);
  assert(status == PO32_OK);
  status = po32_builder_append(&builder, &dpkt_knob_morph);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, sound_tags, sound_lens, 4);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_pattern);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, pattern_only_tags, pattern_only_lens, 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_pattern);
  assert(status == PO32_OK);
  status = po32_builder_append(&builder, &dpkt_state);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, pattern_state_tags, pattern_state_lens, 2);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_knob_fixed);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, knob_only_tags, knob_only_lens, 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_knob_morph);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, knob_only_tags, knob_only_lens, 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_state);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, state_only_tags, state_only_lens, 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_patch_left);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, patch_left_only_tags, patch_left_only_lens, 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_patch_right);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, patch_right_only_tags, patch_right_only_lens, 1);

  po32_builder_init(&builder, frame, sizeof(frame));
  status = po32_builder_append(&builder, &dpkt_reset);
  assert(status == PO32_OK);
  status = po32_builder_finish(&builder, &frame_len);
  assert(status == PO32_OK);
  assert_frame_packets(frame, frame_len, reset_tags, reset_lens, 1);
  (void)status;
}

int main(void) {
  printf("po32 core tests\n");
  printf("===============\n");
  test_patch_encode_decode();
  test_patch_parse_mtdrum_text();
  test_builder_guards();
  test_null_payload_rejected();
  test_frame_build_and_parse();
  test_streaming_modulator_matches_block_render();
  test_extended_payload_encoders();
  test_pattern_trigger_helpers();
  test_pattern_builder_helpers();
  test_builder_exact_capacity_boundary();
  test_state_payload_lengths();
  test_known_transfer_shapes();
  printf("core tests passed\n");
  return 0;
}
