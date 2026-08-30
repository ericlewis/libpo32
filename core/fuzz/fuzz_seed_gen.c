/*
 * Seed corpus generator for the fuzz harnesses.
 *
 * Usage: po32_fuzz_seed_gen <corpus-root>
 *
 * Writes one subdirectory per harness under <corpus-root>, each holding a
 * few valid inputs so the fuzzers start from deep, structure-aware seeds
 * instead of rediscovering the wire format byte by byte.
 */

#include "po32.h"
#include "po32_synth.h"
#include "fuzz_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SEED_FRAME_CAP  1024u
#define SEED_SAMPLE_CAP 330000u
#define SEED_RATE_HZ    22050u

static const char *seed_root;

static int seed_write(const char *harness, const char *name, const void *data, size_t size) {
  char path[512];
  FILE *f;

  snprintf(path, sizeof(path), "%s/%s", seed_root, harness);
  (void)mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/%s/%s", seed_root, harness, name);
  f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "cannot write %s\n", path);
    return 1;
  }
  if (size > 0u && fwrite(data, 1u, size, f) != size) {
    fprintf(stderr, "short write on %s\n", path);
    fclose(f);
    return 1;
  }
  fclose(f);
  printf("wrote %s (%zu bytes)\n", path, size);
  return 0;
}

static void seed_fill_patch(po32_patch_packet_t *patch) {
  unsigned index = 0u;

  memset(patch, 0, sizeof(*patch));
  patch->instrument = 1u;
  patch->side = PO32_PATCH_LEFT;
#define SEED_SET_PARAM(name) patch->params.name = (float)index++ / (float)PO32_PARAM_COUNT;
  FUZZ_PATCH_PARAM_FIELDS(SEED_SET_PARAM)
#undef SEED_SET_PARAM
}

static void seed_fill_knob(po32_knob_packet_t *knob) {
  memset(knob, 0, sizeof(*knob));
  knob->instrument = 2u;
  knob->kind = PO32_KNOB_MORPH;
  knob->value = 0x40u;
}

static void seed_fill_reset(po32_reset_packet_t *reset) {
  memset(reset, 0, sizeof(*reset));
  reset->instrument = 3u;
}

static void seed_fill_state(po32_state_packet_t *state) {
  memset(state, 0, sizeof(*state));
  po32_morph_pairs_default(state->morph_pairs, PO32_STATE_MORPH_PAIR_COUNT);
  state->tempo = 120u;
  state->swing_times_12 = 6u;
  state->pattern_count = 2u;
  state->pattern_numbers[0] = 1u;
  state->pattern_numbers[1] = 2u;
}

static void seed_fill_pattern(po32_pattern_packet_t *pattern) {
  po32_pattern_init(pattern, 1u);
  po32_pattern_set_trigger(pattern, 0u, 1u, 15u);
  po32_pattern_set_trigger(pattern, 4u, 5u, 8u);
  po32_pattern_set_trigger(pattern, 8u, 9u, 4u);
  po32_pattern_set_accent(pattern, 0u, 1);
}

static size_t seed_build_frame(uint8_t *frame, size_t capacity) {
  po32_builder_t builder;
  po32_patch_packet_t patch;
  po32_knob_packet_t knob;
  po32_reset_packet_t reset;
  po32_state_packet_t state;
  po32_pattern_packet_t pattern;
  po32_packet_t wire;
  size_t frame_len = 0u;

  po32_builder_init(&builder, frame, capacity);

  seed_fill_patch(&patch);
  po32_packet_encode(PO32_TAG_PATCH, &patch, &wire);
  po32_builder_append(&builder, &wire);

  seed_fill_knob(&knob);
  po32_packet_encode(PO32_TAG_KNOB, &knob, &wire);
  po32_builder_append(&builder, &wire);

  seed_fill_reset(&reset);
  po32_packet_encode(PO32_TAG_RESET, &reset, &wire);
  po32_builder_append(&builder, &wire);

  seed_fill_state(&state);
  po32_packet_encode(PO32_TAG_STATE, &state, &wire);
  po32_builder_append(&builder, &wire);

  seed_fill_pattern(&pattern);
  po32_packet_encode(PO32_TAG_PATTERN, &pattern, &wire);
  po32_builder_append(&builder, &wire);

  po32_builder_finish(&builder, &frame_len);
  return frame_len;
}

static const char seed_mtdrum_text[] = "OscWave: Sine\n"
                                       "OscFreq: 55.0 Hz\n"
                                       "OscAtk: 0.0 ms\n"
                                       "OscDcy: 350 ms\n"
                                       "ModMode: Decay\n"
                                       "ModRate: 80 ms\n"
                                       "ModAmt: 24.0\n"
                                       "NFilMod: LP\n"
                                       "NFilFrq: 2000 Hz\n"
                                       "NFilQ: 0.7\n"
                                       "NEnvMod: Exp\n"
                                       "NEnvAtk: 0.5 ms\n"
                                       "NEnvDcy: 120 ms\n"
                                       "Mix: 40\n"
                                       "DistAmt: 15\n"
                                       "EQFreq: 100 Hz\n"
                                       "EQGain: 6.0 dB\n"
                                       "Level: -6.0 dB\n"
                                       "OscVel: 100\n"
                                       "NVel: 50\n"
                                       "ModVel: 0\n";

static int seed_packet_decode(void) {
  po32_patch_packet_t patch;
  po32_knob_packet_t knob;
  po32_reset_packet_t reset;
  po32_state_packet_t state;
  po32_pattern_packet_t pattern;
  po32_packet_t wire;
  uint8_t seed[3u + PO32_MAX_PAYLOAD];
  int failures = 0;

  /* Selector index i maps to fuzz_known_tags[i] in fuzz_packet_decode.c. */
  seed[1] = 0u;
  seed[2] = 0u;

  seed_fill_patch(&patch);
  po32_packet_encode(PO32_TAG_PATCH, &patch, &wire);
  seed[0] = 0u;
  memcpy(seed + 3u, wire.payload, wire.payload_len);
  failures += seed_write("packet_decode", "patch", seed, 3u + wire.payload_len);

  seed_fill_knob(&knob);
  po32_packet_encode(PO32_TAG_KNOB, &knob, &wire);
  seed[0] = 1u;
  memcpy(seed + 3u, wire.payload, wire.payload_len);
  failures += seed_write("packet_decode", "knob", seed, 3u + wire.payload_len);

  seed_fill_reset(&reset);
  po32_packet_encode(PO32_TAG_RESET, &reset, &wire);
  seed[0] = 2u;
  memcpy(seed + 3u, wire.payload, wire.payload_len);
  failures += seed_write("packet_decode", "reset", seed, 3u + wire.payload_len);

  seed_fill_state(&state);
  po32_packet_encode(PO32_TAG_STATE, &state, &wire);
  seed[0] = 3u;
  memcpy(seed + 3u, wire.payload, wire.payload_len);
  failures += seed_write("packet_decode", "state", seed, 3u + wire.payload_len);

  seed_fill_pattern(&pattern);
  po32_packet_encode(PO32_TAG_PATTERN, &pattern, &wire);
  seed[0] = 4u;
  memcpy(seed + 3u, wire.payload, wire.payload_len);
  failures += seed_write("packet_decode", "pattern", seed, 3u + wire.payload_len);

  return failures;
}

static int seed_synth_render(void) {
  po32_patch_params_t params;
  uint8_t seed[7u + PO32_PARAM_COUNT * 2u];
  size_t encoded_len = 0u;

  seed[0] = 2u;   /* 44100 Hz */
  seed[1] = 100u; /* velocity: little-endian 100 */
  seed[2] = 0u;
  seed[3] = 0u;
  seed[4] = 0u;
  seed[5] = 0xFFu; /* duration: half of the harness cap */
  seed[6] = 0x7Fu;

  if (po32_patch_parse_mtdrum_text(seed_mtdrum_text, sizeof(seed_mtdrum_text) - 1u, &params) !=
      PO32_OK) {
    fprintf(stderr, "mtdrum seed text failed to parse\n");
    return 1;
  }
  if (po32_encode_patch(&params, seed + 7u, PO32_PARAM_COUNT * 2u, &encoded_len) != PO32_OK ||
      encoded_len != PO32_PARAM_COUNT * 2u) {
    fprintf(stderr, "patch encode failed\n");
    return 1;
  }
  return seed_write("synth_render", "kick", seed, sizeof(seed));
}

int main(int argc, char **argv) {
  static uint8_t frame[SEED_FRAME_CAP];
  static float samples[SEED_SAMPLE_CAP];
  static uint8_t blob[1u + sizeof(samples)];
  size_t frame_len;
  size_t sample_count;
  int failures = 0;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <corpus-root>\n", argv[0]);
    return 64;
  }
  seed_root = argv[1];
  (void)mkdir(seed_root, 0755);

  frame_len = seed_build_frame(frame, sizeof(frame));
  if (frame_len == 0u) {
    fprintf(stderr, "frame build failed\n");
    return 1;
  }

  /* frame_parse: the full frame, and the body alone (the harness also
     parses each input behind a fresh preamble). */
  failures += seed_write("frame_parse", "full_frame", frame, frame_len);
  failures += seed_write("frame_parse", "frame_body", frame + PO32_PREAMBLE_BYTES,
                         frame_len - PO32_PREAMBLE_BYTES);

  failures += seed_packet_decode();

  failures +=
      seed_write("patch_import", "kick_mtdrum", seed_mtdrum_text, sizeof(seed_mtdrum_text) - 1u);

  failures += seed_synth_render();

  /* audio_decode: rate selector byte + rendered transfer audio. */
  sample_count = po32_render_sample_count(frame_len, SEED_RATE_HZ);
  if (sample_count > SEED_SAMPLE_CAP ||
      po32_render_dpsk_f32(frame, frame_len, SEED_RATE_HZ, samples, sample_count) != PO32_OK) {
    fprintf(stderr, "render failed\n");
    return 1;
  }
  blob[0] = 1u; /* fuzz_rates[1] == 22050 in fuzz_audio_decode.c */
  memcpy(blob + 1u, samples, sample_count * sizeof(float));
  failures += seed_write("audio_decode", "transfer_22050", blob, 1u + sample_count * sizeof(float));

  /* modem_roundtrip: small build scripts (rate selector + packet picks). */
  {
    static const uint8_t script_knob[] = {1u, 1u, 4u, 1u, 0x40u};
    static const uint8_t script_mixed[] = {0u,  0u,  2u,  0x55u, 1u,  2u,  3u,  4u,  5u,  6u,  7u,
                                           8u,  9u,  10u, 11u,   12u, 13u, 14u, 15u, 16u, 17u, 18u,
                                           19u, 20u, 4u,  7u,    3u,  0u,  5u,  2u,  1u};
    failures += seed_write("modem_roundtrip", "knob_only", script_knob, sizeof(script_knob));
    failures += seed_write("modem_roundtrip", "mixed", script_mixed, sizeof(script_mixed));
  }

  if (failures != 0) {
    return 1;
  }
  printf("seed corpus complete\n");
  return 0;
}
