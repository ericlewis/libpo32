#ifndef PO32_PO32_H
#define PO32_PO32_H

/*
 * PO-32 acoustic codec.
 *
 * C core for framing, packet encode/decode, rendering, and decode
 * of PO-32 acoustic transfers.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════
 * Protocol constants
 * ══════════════════════════════════════════════════════════ */

#define PO32_PREAMBLE_BYTES                 128
#define PO32_INITIAL_STATE                  0x1D0Fu
#define PO32_NATIVE_BAUD                    1201.923076923077f /* 14 MHz / 11648 */
#define PO32_DPSK_CARRIER_CYCLES_PER_SYMBOL 3
#define PO32_NATIVE_CARRIER_HZ      (PO32_NATIVE_BAUD * (float)PO32_DPSK_CARRIER_CYCLES_PER_SYMBOL)
#define PO32_NATIVE_BLOCK_SIZE      64
#define PO32_PARAM_COUNT            21
#define PO32_STATE_MORPH_PAIR_COUNT 16
#define PO32_PATTERN_LANE_COUNT     4
#define PO32_PATTERN_STEP_COUNT     16
#define PO32_PATTERN_RESERVED_COUNT 16
#define PO32_PATTERN_PAYLOAD_BYTES  211
#define PO32_PATCH_PAYLOAD_BYTES    43
#define PO32_MAX_PAYLOAD            255
#define PO32_FINAL_TAIL_BYTES       5

#define PO32_TAG_PATCH   0x37B2u
#define PO32_TAG_RESET   0x4AB4u
#define PO32_TAG_PATTERN 0xD022u
#define PO32_TAG_ERASE   0xB892u
#define PO32_TAG_STATE   0x505Au
#define PO32_TAG_KNOB    0xA354u

/* ══════════════════════════════════════════════════════════
 * Types
 * ══════════════════════════════════════════════════════════ */

typedef enum po32_status {
  PO32_OK = 0,
  PO32_ERR_INVALID_ARG = -1,
  PO32_ERR_RANGE = -2,
  PO32_ERR_BUFFER_TOO_SMALL = -3,
  PO32_ERR_FRAME = -4,
  PO32_ERR_PARSE = -5
} po32_status_t;

typedef enum po32_patch_side { PO32_PATCH_LEFT = 0, PO32_PATCH_RIGHT = 1 } po32_patch_side_t;

typedef enum po32_knob_kind { PO32_KNOB_PITCH = 0, PO32_KNOB_MORPH = 1 } po32_knob_kind_t;

typedef struct po32_morph_pair {
  uint8_t flag;
  uint8_t morph;
} po32_morph_pair_t;

typedef struct po32_patch_params {
  float OscWave, OscFreq, OscAtk, OscDcy;
  float ModMode, ModRate, ModAmt;
  float NFilMod, NFilFrq, NFilQ;
  float NEnvMod, NEnvAtk, NEnvDcy;
  float Mix, DistAmt, EQFreq, EQGain;
  float Level, OscVel, NVel, ModVel;
} po32_patch_params_t;

typedef struct po32_packet_trailer {
  uint8_t lo, hi;
  uint16_t state;
  int matches_state;
} po32_packet_trailer_t;

typedef struct po32_packet {
  uint16_t tag_code;
  size_t offset;
  size_t payload_len;
  uint8_t payload[PO32_MAX_PAYLOAD]; /* owned copy — safe to keep after callback returns */
  po32_packet_trailer_t trailer;
} po32_packet_t;

typedef struct po32_final_tail {
  uint8_t marker_c3, marker_71;
  uint8_t special_byte;
  uint8_t final_lo, final_hi;
  uint16_t state_before_special;
  uint16_t state_after_special;
  uint16_t final_state;
} po32_final_tail_t;

/*
 * Packet callback for po32_frame_parse(). The packet struct (including
 * its payload array) is owned and safe to keep after the callback returns.
 * Return 0 to continue parsing, nonzero to stop early.
 */
typedef int (*po32_packet_callback_t)(const po32_packet_t *packet, void *user);

typedef struct po32_builder {
  uint8_t *buffer;
  size_t capacity;
  size_t length;
  uint16_t state;
  int finished;
} po32_builder_t;

/* ══════════════════════════════════════════════════════════
 * Protocol primitives
 * ══════════════════════════════════════════════════════════ */

/* Return the static 128-byte preamble used by all transmitted frames. */
const uint8_t *po32_preamble_bytes(void);

/* Human-readable name for a known tag code, or "unknown". */
const char *po32_tag_name(uint16_t tag_code);

/* Update and whitening helpers used by the on-wire format. */
uint16_t po32_crc_mix_state(uint16_t state, uint8_t x);
uint16_t po32_crc_update(uint16_t state, uint8_t raw_byte);
uint8_t po32_whiten_byte(uint16_t state, uint8_t raw_byte);
uint8_t po32_unwhiten_byte(uint16_t state, uint8_t coded_byte);

/* ══════════════════════════════════════════════════════════
 * Frame parser (RX: frame bytes → packets)
 * ══════════════════════════════════════════════════════════ */

/*
 * Parse a full transmitted frame.
 *
 * The callback receives owned packet structs in wire order. If out_tail is
 * non-NULL, the decoded final tail is returned after a successful parse.
 */
po32_status_t po32_frame_parse(const uint8_t *frame, size_t frame_len,
                               po32_packet_callback_t callback, void *user,
                               po32_final_tail_t *out_tail);

/* ══════════════════════════════════════════════════════════
 * Frame builder (TX: packets → frame bytes)
 * ══════════════════════════════════════════════════════════ */

/* Initialize a frame builder over caller-owned storage. */
void po32_builder_init(po32_builder_t *builder, uint8_t *buffer, size_t capacity);

/* Reset the builder and write the preamble back to the output buffer. */
void po32_builder_reset(po32_builder_t *builder);

/* Append one packet payload in encoded body form. */
po32_status_t po32_builder_append_packet(po32_builder_t *builder, uint16_t tag_code,
                                         const uint8_t *payload, size_t payload_len,
                                         size_t *packet_offset);

/* Emit the final five-byte tail and return the full frame length. */
po32_status_t po32_builder_finish(po32_builder_t *builder, size_t *frame_len);

/* Append an already-decoded packet struct. */
po32_status_t po32_builder_append(po32_builder_t *builder, const po32_packet_t *pkt);

/* Initialize helpers for common packet defaults. */
void po32_patch_params_zero(po32_patch_params_t *params);
void po32_morph_pairs_default(po32_morph_pair_t *pairs, size_t pair_count);

/*
 * Parse an `.mtdrum` patch text blob into the normalized
 * 21-parameter PO-32 patch representation.
 *
 * This is a low-level text importer only: callers own file I/O and text
 * decoding, then pass the UTF-8 contents here.
 */
po32_status_t po32_patch_parse_mtdrum_text(const char *text, size_t text_len,
                                           po32_patch_params_t *out);

/* ══════════════════════════════════════════════════════════
 * Packet encode/decode
 * ══════════════════════════════════════════════════════════ */

typedef struct po32_patch_packet {
  uint8_t instrument;         /* 1-16 */
  po32_patch_side_t side;     /* left or right morph endpoint */
  po32_patch_params_t params; /* 21 normalized float parameters */
} po32_patch_packet_t;

typedef struct po32_knob_packet {
  uint8_t instrument;    /* 1-16 */
  po32_knob_kind_t kind; /* pitch or morph */
  uint8_t value;         /* 0-255 */
} po32_knob_packet_t;

typedef struct po32_reset_packet {
  uint8_t instrument; /* 1-16 */
} po32_reset_packet_t;

typedef struct po32_state_packet {
  po32_morph_pair_t morph_pairs[PO32_STATE_MORPH_PAIR_COUNT];
  uint8_t tempo;
  uint8_t swing_times_12;
  uint8_t pattern_numbers[PO32_PATTERN_STEP_COUNT];
  size_t pattern_count;
} po32_state_packet_t;

typedef struct po32_pattern_packet {
  uint8_t pattern_number;
  /* Raw packed lane bytes, not direct instrument numbers. */
  uint8_t trigger_lanes[PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT];
  po32_morph_pair_t morph_lanes[PO32_PATTERN_LANE_COUNT * PO32_PATTERN_STEP_COUNT];
  uint8_t reserved[PO32_PATTERN_RESERVED_COUNT];
  uint16_t accent_bits;
} po32_pattern_packet_t;

/*
 * Pattern trigger helpers.
 *
 * Instruments are grouped into four fixed trigger lanes:
 * lane 0 -> 1, 5, 9, 13
 * lane 1 -> 2, 6, 10, 14
 * lane 2 -> 3, 7, 11, 15
 * lane 3 -> 4, 8, 12, 16
 */
po32_status_t po32_pattern_trigger_lane(uint8_t instrument, uint8_t *out_lane);
po32_status_t po32_pattern_trigger_encode(uint8_t instrument, uint8_t fill_rate, int accent,
                                          uint8_t *out_trigger);
po32_status_t po32_pattern_trigger_decode(uint8_t lane_index, uint8_t trigger,
                                          uint8_t *out_instrument, uint8_t *out_fill_rate,
                                          int *out_accent);

/* Encode/decode between typed packet structs and wire payloads. */
po32_status_t po32_packet_encode(uint16_t tag, const void *pkt, po32_packet_t *out);
po32_status_t po32_packet_decode(uint16_t tag, const uint8_t *data, size_t len, void *out);

/* Convenience helpers for the raw 21-parameter patch byte format. */
po32_status_t po32_encode_patch(const po32_patch_params_t *params, uint8_t *out,
                                size_t out_capacity, size_t *out_len);
po32_status_t po32_decode_patch(const uint8_t *data, size_t len, po32_patch_params_t *out);

/* ══════════════════════════════════════════════════════════
 * DPSK renderer (TX: frame bytes → audio)
 * ══════════════════════════════════════════════════════════ */

typedef struct po32_modulator {
  const uint8_t *frame;
  size_t frame_len;
  size_t total_bits;
  size_t total_samples;
  size_t sample_index;
  size_t bit_cursor;
  float sample_rate;
  float symbol_phase;
  float symbols_per_sample;
  float osc_sin, osc_cos;
  float rot_sin, rot_cos;
  int state;
} po32_modulator_t;

/* Number of mono float samples needed to render a frame at sample_rate. */
size_t po32_render_sample_count(size_t frame_len, uint32_t sample_rate);

/* Initialize or reset the streaming modulator state over a caller-owned frame buffer. */
void po32_modulator_init(po32_modulator_t *m, const uint8_t *frame, size_t frame_len,
                         uint32_t sample_rate);
void po32_modulator_reset(po32_modulator_t *m);

/* Query streaming render progress. */
size_t po32_modulator_samples_remaining(const po32_modulator_t *m);
int po32_modulator_done(const po32_modulator_t *m);

/* Render the next chunk of mono float samples in [-1, 1]. */
po32_status_t po32_modulator_render_f32(po32_modulator_t *m, float *out_samples, size_t out_capacity,
                                        size_t *out_len);

/* Render a full transmitted frame to mono float samples in [-1, 1]. */
po32_status_t po32_render_dpsk_f32(const uint8_t *frame, size_t frame_len, uint32_t sample_rate,
                                   float *out_samples, size_t out_sample_count);

/* ══════════════════════════════════════════════════════════
 * Decode (RX: audio → normalized frame bytes)
 * ══════════════════════════════════════════════════════════ */

typedef struct po32_decode_result {
  int packet_count;
  int done;
  po32_final_tail_t tail;
} po32_decode_result_t;

/*
 * One-shot convenience decode.
 *
 * Reconstructs a normalized frame in out_frame from mono float audio.
 */
po32_status_t po32_decode_f32(const float *samples, size_t count, float sample_rate,
                              po32_decode_result_t *out_result, uint8_t *out_frame,
                              size_t out_capacity, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
