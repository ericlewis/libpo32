#include "po32.h"
#include "po32_lut.h"

#define PO32_PI 3.14159265358979323846f

/* ── freestanding helpers ──────────────────────────────── */

static void po32_zero(void *dst, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  while (n-- > 0u)
    *d++ = 0u;
}

static void po32_memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  while (n-- > 0u)
    *d++ = *s++;
}

static int po32_memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (n-- > 0u) {
    if (*pa != *pb)
      return (int)*pa - (int)*pb;
    ++pa;
    ++pb;
  }
  return 0;
}

#define PO32_PREAMBLE_FILL         0x55u
#define PO32_TAIL_MARKER_1         0xC3u
#define PO32_TAIL_MARKER_2         0x71u
#define PO32_PARAM_SCALE           65535.0f
#define PO32_PACKET_HEADER_BYTES   3u
#define PO32_PACKET_TRAILER_BYTES  2u
#define PO32_PACKET_OVERHEAD_BYTES (PO32_PACKET_HEADER_BYTES + PO32_PACKET_TRAILER_BYTES)
#define PO32_PATCH_PARAM_BYTES     ((size_t)PO32_PARAM_COUNT * 2u)
#define PO32_TIMING_RECOVERY_GAIN  0.01f

#define PO32_STATE_PAYLOAD_MIN_BYTES ((size_t)PO32_STATE_MORPH_PAIR_COUNT * 2u + 3u)
#define PO32_PATTERN_LANE_BYTES ((size_t)PO32_PATTERN_LANE_COUNT * (size_t)PO32_PATTERN_STEP_COUNT)

#define PO32_PATCH_SIDE_LEFT_PREFIX  0x10u
#define PO32_PATCH_SIDE_RIGHT_PREFIX 0x20u
#define PO32_RESET_PREFIX            0x30u
#define PO32_KNOB_KIND_PITCH_PREFIX  0x00u
#define PO32_KNOB_KIND_MORPH_PREFIX  0x10u

typedef struct {
  uint16_t tag_code;
  size_t min_payload_len;
  size_t max_payload_len;
} po32_tag_spec_t;

static const po32_tag_spec_t po32_tag_specs[] = {
    {PO32_TAG_PATCH, PO32_PATCH_PAYLOAD_BYTES, PO32_PATCH_PAYLOAD_BYTES},
    {PO32_TAG_PATTERN, PO32_PATTERN_PAYLOAD_BYTES, PO32_PATTERN_PAYLOAD_BYTES},
    {PO32_TAG_STATE, PO32_STATE_PAYLOAD_MIN_BYTES, 70u},
    {PO32_TAG_KNOB, 2u, 2u},
    {PO32_TAG_RESET, 1u, 1u},
    {PO32_TAG_ERASE, 1u, 1u},
};

#define PO32_TAG_SPEC_COUNT (sizeof(po32_tag_specs) / sizeof(po32_tag_specs[0]))

static const po32_tag_spec_t *po32_tag_spec_find(uint16_t tag_code) {
  for (size_t i = 0u; i < PO32_TAG_SPEC_COUNT; ++i) {
    if (po32_tag_specs[i].tag_code == tag_code) {
      return &po32_tag_specs[i];
    }
  }
  return NULL;
}

static int po32_tag_payload_len_is_valid(uint16_t tag_code, size_t payload_len) {
  const po32_tag_spec_t *spec = po32_tag_spec_find(tag_code);
  return spec != NULL && payload_len >= spec->min_payload_len &&
         payload_len <= spec->max_payload_len;
}

static const uint8_t PO32_PREAMBLE_SYNC_WORD[4] = {0x14u, 0x19u, 0x9Du, 0xCFu};

#define PO32_REPEAT_4(x)  x, x, x, x
#define PO32_REPEAT_8(x)  PO32_REPEAT_4(x), PO32_REPEAT_4(x)
#define PO32_REPEAT_16(x) PO32_REPEAT_8(x), PO32_REPEAT_8(x)
#define PO32_REPEAT_32(x) PO32_REPEAT_16(x), PO32_REPEAT_16(x)
#define PO32_REPEAT_64(x) PO32_REPEAT_32(x), PO32_REPEAT_32(x)

static const uint8_t PO32_PREAMBLE[PO32_PREAMBLE_BYTES] = {
    PO32_REPEAT_64(PO32_PREAMBLE_FILL), PO32_REPEAT_32(PO32_PREAMBLE_FILL),
    PO32_REPEAT_16(PO32_PREAMBLE_FILL), PO32_REPEAT_8(PO32_PREAMBLE_FILL),
    PO32_REPEAT_4(PO32_PREAMBLE_FILL),  PO32_PREAMBLE_SYNC_WORD[0],
    PO32_PREAMBLE_SYNC_WORD[1],         PO32_PREAMBLE_SYNC_WORD[2],
    PO32_PREAMBLE_SYNC_WORD[3],
};

/* clang-format off */
#define PO32_PATCH_PARAM_FIELDS(X) \
  X( 0, OscWave) X( 1, OscFreq) X( 2, OscAtk)  X( 3, OscDcy) \
  X( 4, ModMode) X( 5, ModRate) X( 6, ModAmt)                 \
  X( 7, NFilMod) X( 8, NFilFrq) X( 9, NFilQ)                  \
  X(10, NEnvMod) X(11, NEnvAtk) X(12, NEnvDcy)                 \
  X(13, Mix)     X(14, DistAmt) X(15, EQFreq) X(16, EQGain)    \
  X(17, Level)   X(18, OscVel)  X(19, NVel)   X(20, ModVel)
/* clang-format on */

const uint8_t *po32_preamble_bytes(void) {
  return PO32_PREAMBLE;
}

const char *po32_tag_name(uint16_t tag_code) {
  switch (tag_code) {
  case PO32_TAG_PATCH:
    return "patch";
  case PO32_TAG_RESET:
    return "reset";
  case PO32_TAG_PATTERN:
    return "pattern";
  case PO32_TAG_ERASE:
    return "erase";
  case PO32_TAG_STATE:
    return "state";
  case PO32_TAG_KNOB:
    return "knob";
  default:
    return "unknown";
  }
}

uint16_t po32_crc_mix_state(uint16_t state, uint8_t x) {
  uint16_t masked = (uint16_t)(x & 0xFFu);

  return (uint16_t)((((uint32_t)state << 8) | masked) ^ ((uint32_t)masked << 12) ^
                    ((uint32_t)masked << 5));
}

uint16_t po32_crc_update(uint16_t state, uint8_t raw_byte) {
  uint8_t x = (uint8_t)(((raw_byte & 0xFFu) ^ (state >> 8)) & 0xFFu);

  x ^= (uint8_t)(x >> 4);
  return po32_crc_mix_state(state, x);
}

uint8_t po32_whiten_byte(uint16_t state, uint8_t raw_byte) {
  return (uint8_t)((raw_byte + (state & 0xFFu)) & 0xFFu);
}

uint8_t po32_unwhiten_byte(uint16_t state, uint8_t coded_byte) {
  return (uint8_t)((coded_byte - (state & 0xFFu)) & 0xFFu);
}

static po32_status_t po32_final_tail_decode(uint16_t state, const uint8_t *data, size_t len,
                                            po32_final_tail_t *tail, size_t *consumed) {
  uint8_t raw1, raw2, raw4, raw5, x;
  uint16_t state1, state2, state3, state4, state5;

  if (data == NULL || len < PO32_FINAL_TAIL_BYTES) {
    return PO32_ERR_INVALID_ARG;
  }

  raw1 = po32_unwhiten_byte(state, data[0]);
  state1 = po32_crc_update(state, raw1);
  raw2 = po32_unwhiten_byte(state1, data[1]);
  state2 = po32_crc_update(state1, raw2);

  if (raw1 != PO32_TAIL_MARKER_1 || raw2 != PO32_TAIL_MARKER_2 ||
      data[2] != (uint8_t)(state2 & 0xFFu)) {
    return PO32_ERR_FRAME;
  }

  x = (uint8_t)(((state2 >> 12) ^ (state2 >> 8)) & 0xFFu);
  state3 = po32_crc_mix_state(state2, x);
  raw4 = po32_unwhiten_byte(state3, data[3]);
  state4 = po32_crc_update(state3, raw4);
  raw5 = po32_unwhiten_byte(state4, data[4]);
  state5 = po32_crc_update(state4, raw5);

  if (raw4 != (uint8_t)(state3 & 0xFFu) || raw5 != (uint8_t)((state3 >> 8) & 0xFFu)) {
    return PO32_ERR_FRAME;
  }

  if (tail != NULL) {
    *tail = (po32_final_tail_t){
        .marker_c3 = raw1,
        .marker_71 = raw2,
        .special_byte = data[2],
        .final_lo = raw4,
        .final_hi = raw5,
        .state_before_special = state2,
        .state_after_special = state3,
        .final_state = state5,
    };
  }
  if (consumed != NULL) {
    *consumed = PO32_FINAL_TAIL_BYTES;
  }

  return PO32_OK;
}

static po32_status_t po32_packet_decode_bytes(const uint8_t *src, size_t src_len, uint16_t *state,
                                              size_t *consumed_bytes, po32_packet_t *packet) {
  size_t pos = 0u;
  uint16_t s;
  uint8_t tag_lo, tag_hi, payload_len, trailer_lo, trailer_hi;
  uint16_t tag_code, packet_state;

  if (src == NULL || state == NULL || packet == NULL || consumed_bytes == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  s = *state;
  if (src_len < PO32_PACKET_OVERHEAD_BYTES)
    return PO32_ERR_FRAME;

  tag_lo = po32_unwhiten_byte(s, src[pos++]);
  s = po32_crc_update(s, tag_lo);
  tag_hi = po32_unwhiten_byte(s, src[pos++]);
  s = po32_crc_update(s, tag_hi);
  tag_code = (uint16_t)tag_lo | (uint16_t)((uint16_t)tag_hi << 8);

  payload_len = po32_unwhiten_byte(s, src[pos++]);
  s = po32_crc_update(s, payload_len);

  if (!po32_tag_payload_len_is_valid(tag_code, payload_len))
    return PO32_ERR_FRAME;
  if (src_len < PO32_PACKET_HEADER_BYTES + (size_t)payload_len + PO32_PACKET_TRAILER_BYTES) {
    return PO32_ERR_FRAME;
  }

  packet->tag_code = tag_code;
  packet->payload_len = payload_len;

  for (size_t i = 0u; i < payload_len; ++i) {
    uint8_t raw = po32_unwhiten_byte(s, src[pos + i]);
    packet->payload[i] = raw;
    s = po32_crc_update(s, raw);
  }
  pos += payload_len;

  packet_state = s;
  trailer_lo = po32_unwhiten_byte(s, src[pos++]);
  s = po32_crc_update(s, trailer_lo);
  trailer_hi = po32_unwhiten_byte(s, src[pos++]);
  s = po32_crc_update(s, trailer_hi);

  if (trailer_lo != (uint8_t)(packet_state & 0xFFu) ||
      trailer_hi != (uint8_t)((packet_state >> 8) & 0xFFu)) {
    return PO32_ERR_FRAME;
  }

  packet->trailer.lo = trailer_lo;
  packet->trailer.hi = trailer_hi;
  packet->trailer.state = packet_state;
  packet->trailer.matches_state = 1;

  *consumed_bytes = pos;
  *state = s;
  return PO32_OK;
}

/* ── frame parsing ──────────────────────────────────────────────── */

static int po32_frame_parse_packet_at(const uint8_t *frame, size_t frame_len, size_t *pos,
                                      uint16_t *state, po32_packet_t *packet) {
  size_t consumed = 0u;
  if (frame_len - *pos < PO32_PACKET_OVERHEAD_BYTES)
    return -1;
  if (po32_packet_decode_bytes(frame + *pos, frame_len - *pos, state, &consumed, packet) != PO32_OK)
    return -1;
  packet->offset = *pos;
  *pos += consumed;
  return 0;
}

static po32_status_t po32_packet_decode_header(const uint8_t *src, size_t src_len, uint16_t state,
                                               uint16_t *tag_code, size_t *packet_len) {
  uint8_t tag_lo, tag_hi, payload_len;

  if (src == NULL || tag_code == NULL || packet_len == NULL || src_len < PO32_PACKET_HEADER_BYTES) {
    return PO32_ERR_INVALID_ARG;
  }

  tag_lo = po32_unwhiten_byte(state, src[0]);
  state = po32_crc_update(state, tag_lo);
  tag_hi = po32_unwhiten_byte(state, src[1]);
  state = po32_crc_update(state, tag_hi);
  payload_len = po32_unwhiten_byte(state, src[2]);

  *tag_code = (uint16_t)tag_lo | (uint16_t)((uint16_t)tag_hi << 8);
  if (!po32_tag_payload_len_is_valid(*tag_code, payload_len)) {
    return PO32_ERR_FRAME;
  }

  *packet_len = PO32_PACKET_HEADER_BYTES + (size_t)payload_len + PO32_PACKET_TRAILER_BYTES;
  return PO32_OK;
}

po32_status_t po32_frame_parse(const uint8_t *frame, size_t frame_len,
                               po32_packet_callback_t callback, void *user,
                               po32_final_tail_t *out_tail) {
  size_t pos;
  uint16_t state;

  if (frame == NULL || frame_len < PO32_PREAMBLE_BYTES + PO32_FINAL_TAIL_BYTES) {
    return PO32_ERR_INVALID_ARG;
  }
  if (po32_memcmp(frame, po32_preamble_bytes(), PO32_PREAMBLE_BYTES) != 0) {
    return PO32_ERR_FRAME;
  }

  pos = PO32_PREAMBLE_BYTES;
  state = PO32_INITIAL_STATE;

  while (pos < frame_len) {
    po32_final_tail_t tail;
    po32_packet_t packet;

    if (po32_final_tail_decode(state, frame + pos, frame_len - pos, &tail, NULL) == PO32_OK) {
      if (out_tail != NULL)
        *out_tail = tail;
      return PO32_OK;
    }

    if (po32_frame_parse_packet_at(frame, frame_len, &pos, &state, &packet) != 0) {
      return PO32_ERR_FRAME;
    }

    if (callback != NULL && callback(&packet, user) != 0) {
      return PO32_OK;
    }
  }

  return PO32_ERR_FRAME;
}

/* ── frame building ─────────────────────────────────────────────── */

static po32_status_t po32_builder_write_byte(po32_builder_t *builder, uint8_t raw_byte) {
  if (builder == NULL || builder->buffer == NULL) {
    return PO32_ERR_INVALID_ARG;
  }
  if (builder->length >= builder->capacity) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }

  builder->buffer[builder->length++] = po32_whiten_byte(builder->state, raw_byte);
  builder->state = po32_crc_update(builder->state, raw_byte);
  return PO32_OK;
}

static po32_status_t po32_builder_write_bytes(po32_builder_t *builder, const uint8_t *raw,
                                              size_t len) {
  for (size_t i = 0u; i < len; ++i) {
    po32_status_t status = po32_builder_write_byte(builder, raw[i]);
    if (status != PO32_OK)
      return status;
  }
  return PO32_OK;
}

static po32_status_t po32_builder_write_state_trailer(po32_builder_t *builder, uint16_t state) {
  uint8_t trailer[2];
  trailer[0] = (uint8_t)(state & 0xFFu);
  trailer[1] = (uint8_t)((state >> 8) & 0xFFu);
  return po32_builder_write_bytes(builder, trailer, 2u);
}

void po32_builder_init(po32_builder_t *builder, uint8_t *buffer, size_t capacity) {
  if (builder == NULL) {
    return;
  }

  builder->buffer = buffer;
  builder->capacity = capacity;
  po32_builder_reset(builder);
}

void po32_builder_reset(po32_builder_t *builder) {
  if (builder == NULL) {
    return;
  }

  builder->length = 0u;
  builder->state = PO32_INITIAL_STATE;
  builder->finished = 0;

  if (builder->buffer == NULL || builder->capacity < PO32_PREAMBLE_BYTES) {
    return;
  }

  po32_memcpy(builder->buffer, po32_preamble_bytes(), PO32_PREAMBLE_BYTES);
  builder->length = PO32_PREAMBLE_BYTES;
}

void po32_patch_params_zero(po32_patch_params_t *params) {
  if (params == NULL)
    return;
  po32_zero(params, sizeof(*params));
}

void po32_morph_pairs_default(po32_morph_pair_t *pairs, size_t pair_count) {
  if (pairs == NULL)
    return;
  for (size_t i = 0u; i < pair_count; ++i) {
    pairs[i].flag = 0x80u;
    pairs[i].morph = 0x01u;
  }
}

static size_t po32_pattern_slot_index(uint8_t step_index, uint8_t lane_index) {
  return (size_t)lane_index * (size_t)PO32_PATTERN_STEP_COUNT + (size_t)step_index;
}

static int po32_pattern_step_has_trigger(const po32_pattern_packet_t *pattern, uint8_t step_index) {
  for (uint8_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    if (pattern->steps[po32_pattern_slot_index(step_index, lane)].instrument != 0u)
      return 1;
  }
  return 0;
}

static void po32_pattern_zero_slot(po32_pattern_packet_t *pattern, uint8_t step_index,
                                   uint8_t lane_index) {
  size_t index = po32_pattern_slot_index(step_index, lane_index);
  pattern->steps[index].instrument = 0u;
  pattern->steps[index].fill_rate = 0u;
  pattern->steps[index].accent = 0;
  pattern->morph_lanes[index].flag = 0u;
  pattern->morph_lanes[index].morph = 0u;
}

void po32_pattern_init(po32_pattern_packet_t *pattern, uint8_t pattern_number) {
  if (pattern == NULL)
    return;
  po32_zero(pattern, sizeof(*pattern));
  pattern->pattern_number = pattern_number;
}

void po32_pattern_clear(po32_pattern_packet_t *pattern) {
  uint8_t pattern_number;
  if (pattern == NULL)
    return;
  pattern_number = pattern->pattern_number;
  po32_zero(pattern, sizeof(*pattern));
  pattern->pattern_number = pattern_number;
}

po32_status_t po32_pattern_set_trigger(po32_pattern_packet_t *pattern, uint8_t step_index,
                                       uint8_t instrument, uint8_t fill_rate) {
  uint8_t lane_index = 0u;
  size_t index;
  if (pattern == NULL)
    return PO32_ERR_INVALID_ARG;
  if (step_index >= PO32_PATTERN_STEP_COUNT)
    return PO32_ERR_RANGE;
  if (fill_rate == 0u || fill_rate > 0x0Fu)
    return PO32_ERR_RANGE;
  if (po32_pattern_trigger_lane(instrument, &lane_index) != PO32_OK)
    return PO32_ERR_RANGE;

  index = po32_pattern_slot_index(step_index, lane_index);
  pattern->steps[index].instrument = instrument;
  pattern->steps[index].fill_rate = fill_rate;
  pattern->steps[index].accent = ((pattern->accent_bits >> step_index) & 1u) != 0u;
  po32_morph_pairs_default(&pattern->morph_lanes[index], 1u);
  return PO32_OK;
}

po32_status_t po32_pattern_clear_trigger(po32_pattern_packet_t *pattern, uint8_t step_index,
                                         uint8_t lane_index) {
  if (pattern == NULL)
    return PO32_ERR_INVALID_ARG;
  if (step_index >= PO32_PATTERN_STEP_COUNT || lane_index >= PO32_PATTERN_LANE_COUNT)
    return PO32_ERR_RANGE;

  po32_pattern_zero_slot(pattern, step_index, lane_index);
  if (!po32_pattern_step_has_trigger(pattern, step_index)) {
    pattern->accent_bits =
        (uint16_t)(pattern->accent_bits & (uint16_t)~(uint16_t)(1u << step_index));
  }
  return PO32_OK;
}

po32_status_t po32_pattern_clear_step(po32_pattern_packet_t *pattern, uint8_t step_index) {
  if (pattern == NULL)
    return PO32_ERR_INVALID_ARG;
  if (step_index >= PO32_PATTERN_STEP_COUNT)
    return PO32_ERR_RANGE;

  for (uint8_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    po32_pattern_zero_slot(pattern, step_index, lane);
  }
  pattern->accent_bits = (uint16_t)(pattern->accent_bits & (uint16_t)~(uint16_t)(1u << step_index));
  return PO32_OK;
}

po32_status_t po32_pattern_set_accent(po32_pattern_packet_t *pattern, uint8_t step_index,
                                      int enabled) {
  uint16_t mask;
  if (pattern == NULL)
    return PO32_ERR_INVALID_ARG;
  if (step_index >= PO32_PATTERN_STEP_COUNT)
    return PO32_ERR_RANGE;

  mask = (uint16_t)(1u << step_index);
  if (enabled) {
    pattern->accent_bits = (uint16_t)(pattern->accent_bits | mask);
  } else {
    pattern->accent_bits = (uint16_t)(pattern->accent_bits & (uint16_t)~mask);
  }

  for (uint8_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    size_t index = po32_pattern_slot_index(step_index, lane);
    if (pattern->steps[index].instrument != 0u)
      pattern->steps[index].accent = enabled ? 1 : 0;
  }
  return PO32_OK;
}

po32_status_t po32_pattern_trigger_lane(uint8_t instrument, uint8_t *out_lane) {
  if (out_lane == NULL)
    return PO32_ERR_INVALID_ARG;
  if (instrument < 1u || instrument > 16u)
    return PO32_ERR_RANGE;
  *out_lane = (uint8_t)((instrument - 1u) & 3u);
  return PO32_OK;
}

po32_status_t po32_pattern_trigger_encode(uint8_t instrument, uint8_t fill_rate, int accent,
                                          uint8_t *out_trigger) {
  uint8_t zero_based;
  uint8_t trigger;

  if (out_trigger == NULL)
    return PO32_ERR_INVALID_ARG;
  if (instrument < 1u || instrument > 16u || fill_rate == 0u || fill_rate > 0x0Fu)
    return PO32_ERR_RANGE;

  zero_based = (uint8_t)(instrument - 1u);
  trigger = fill_rate;
  if (accent)
    trigger = (uint8_t)(trigger | 0x80u);
  if ((zero_based & 7u) >= 4u)
    trigger = (uint8_t)(trigger | 0x10u);
  if (zero_based >= 8u)
    trigger = (uint8_t)(trigger | 0x20u);

  *out_trigger = trigger;
  return PO32_OK;
}

po32_status_t po32_pattern_trigger_decode(uint8_t lane_index, uint8_t trigger,
                                          uint8_t *out_instrument, uint8_t *out_fill_rate,
                                          int *out_accent) {
  uint8_t instrument;

  if (lane_index >= PO32_PATTERN_LANE_COUNT)
    return PO32_ERR_RANGE;
  if ((trigger & 0x40u) != 0u)
    return PO32_ERR_RANGE;

  if ((trigger & 0x0Fu) == 0u) {
    if (out_instrument != NULL)
      *out_instrument = 0u;
    if (out_fill_rate != NULL)
      *out_fill_rate = 0u;
    if (out_accent != NULL)
      *out_accent = 0;
    return PO32_OK;
  }

  instrument = (uint8_t)(lane_index + 1u);
  if ((trigger & 0x10u) != 0u)
    instrument = (uint8_t)(instrument + 4u);
  if ((trigger & 0x20u) != 0u)
    instrument = (uint8_t)(instrument + 8u);

  if (out_instrument != NULL)
    *out_instrument = instrument;
  if (out_fill_rate != NULL)
    *out_fill_rate = (uint8_t)(trigger & 0x0Fu);
  if (out_accent != NULL)
    *out_accent = ((trigger & 0x80u) != 0u) ? 1 : 0;
  return PO32_OK;
}

po32_status_t po32_builder_append_packet(po32_builder_t *builder, uint16_t tag_code,
                                         const uint8_t *payload, size_t payload_len,
                                         size_t *packet_offset) {
  size_t needed;
  uint8_t header[3];

  if (builder == NULL || builder->buffer == NULL) {
    return PO32_ERR_INVALID_ARG;
  }
  if (payload_len > 0u && payload == NULL) {
    return PO32_ERR_INVALID_ARG;
  }
  if (builder->capacity < PO32_PREAMBLE_BYTES || builder->length < PO32_PREAMBLE_BYTES) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }
  if (builder->finished) {
    return PO32_ERR_FRAME;
  }
  if (payload_len > 0xFFu) {
    return PO32_ERR_RANGE;
  }

  needed = builder->length + PO32_PACKET_OVERHEAD_BYTES + payload_len;
  if (needed > builder->capacity) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }

  if (packet_offset != NULL) {
    *packet_offset = builder->length;
  }

  header[0] = (uint8_t)(tag_code & 0xFFu);
  header[1] = (uint8_t)((tag_code >> 8) & 0xFFu);
  header[2] = (uint8_t)payload_len;

  /* Capacity was validated above — these writes cannot fail. */
  po32_builder_write_bytes(builder, header, 3u);
  po32_builder_write_bytes(builder, payload, payload_len);
  return po32_builder_write_state_trailer(builder, builder->state);
}

/* Caller must ensure capacity. Only invoked from po32_builder_finish after
   the capacity pre-check guarantees PO32_FINAL_TAIL_BYTES bytes remain. */
static void po32_final_tail_emit_special_byte(po32_builder_t *builder) {
  uint8_t x;
  builder->buffer[builder->length++] = (uint8_t)(builder->state & 0xFFu);
  x = (uint8_t)(((builder->state >> 12) ^ (builder->state >> 8)) & 0xFFu);
  builder->state = po32_crc_mix_state(builder->state, x);
}

po32_status_t po32_builder_finish(po32_builder_t *builder, size_t *frame_len) {
  size_t needed;

  if (builder == NULL || builder->buffer == NULL) {
    return PO32_ERR_INVALID_ARG;
  }
  if (builder->capacity < PO32_PREAMBLE_BYTES || builder->length < PO32_PREAMBLE_BYTES) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }
  if (builder->finished) {
    if (frame_len != NULL) {
      *frame_len = builder->length;
    }
    return PO32_OK;
  }

  needed = builder->length + PO32_FINAL_TAIL_BYTES;
  if (needed > builder->capacity) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }

  po32_builder_write_byte(builder, PO32_TAIL_MARKER_1);
  po32_builder_write_byte(builder, PO32_TAIL_MARKER_2);
  po32_final_tail_emit_special_byte(builder);
  po32_builder_write_state_trailer(builder, builder->state);

  builder->finished = 1;
  if (frame_len != NULL) {
    *frame_len = builder->length;
  }
  return PO32_OK;
}

po32_status_t po32_builder_append(po32_builder_t *builder, const po32_packet_t *pkt) {
  if (pkt == NULL)
    return PO32_ERR_INVALID_ARG;
  return po32_builder_append_packet(builder, pkt->tag_code, pkt->payload, pkt->payload_len, NULL);
}

po32_status_t po32_encode_patch(const po32_patch_params_t *params, uint8_t *out,
                                size_t out_capacity, size_t *out_len) {
  if (params == NULL || out == NULL || out_len == NULL)
    return PO32_ERR_INVALID_ARG;
  if (out_capacity < PO32_PATCH_PARAM_BYTES)
    return PO32_ERR_BUFFER_TOO_SMALL;

#define ENCODE_PATCH_PARAM(idx, name)                                                              \
  {                                                                                                \
    float _v = params->name;                                                                       \
    if (_v < 0.0f)                                                                                 \
      _v = 0.0f;                                                                                   \
    if (_v > 1.0f)                                                                                 \
      _v = 1.0f;                                                                                   \
    uint16_t raw = (uint16_t)(_v * PO32_PARAM_SCALE + 0.5f);                                       \
    out[(size_t)(idx) * 2u] = (uint8_t)(raw & 0xFFu);                                              \
    out[(size_t)(idx) * 2u + 1u] = (uint8_t)((raw >> 8) & 0xFFu);                                  \
  }
  PO32_PATCH_PARAM_FIELDS(ENCODE_PATCH_PARAM)
#undef ENCODE_PATCH_PARAM

  *out_len = PO32_PATCH_PARAM_BYTES;
  return PO32_OK;
}

po32_status_t po32_decode_patch(const uint8_t *data, size_t len, po32_patch_params_t *out) {
  if (data == NULL || out == NULL || len < PO32_PATCH_PARAM_BYTES)
    return PO32_ERR_INVALID_ARG;

#define DECODE_PATCH_PARAM(idx, name)                                                              \
  out->name = (float)((uint16_t)data[(size_t)(idx) * 2u] |                                         \
                      (uint16_t)((uint16_t)data[(size_t)(idx) * 2u + 1u] << 8)) /                  \
              PO32_PARAM_SCALE;
  PO32_PATCH_PARAM_FIELDS(DECODE_PATCH_PARAM)
#undef DECODE_PATCH_PARAM

  return PO32_OK;
}

/* ── typed packet helpers ───────────────────────────────────────── */

static po32_status_t po32_patch_packet_encode(const po32_patch_packet_t *pkt, po32_packet_t *out) {
  po32_zero(out, sizeof(*out));
  out->tag_code = PO32_TAG_PATCH;

  out->payload[0] = (uint8_t)((pkt->side == PO32_PATCH_LEFT ? PO32_PATCH_SIDE_LEFT_PREFIX
                                                            : PO32_PATCH_SIDE_RIGHT_PREFIX) |
                              (pkt->instrument - 1u));

#define ENCODE_PARAM(idx, name)                                                                    \
  {                                                                                                \
    float _v = pkt->params.name;                                                                   \
    if (_v < 0.0f)                                                                                 \
      _v = 0.0f;                                                                                   \
    if (_v > 1.0f)                                                                                 \
      _v = 1.0f;                                                                                   \
    uint16_t raw = (uint16_t)(_v * PO32_PARAM_SCALE + 0.5f);                                       \
    out->payload[1u + (size_t)(idx) * 2u] = (uint8_t)(raw & 0xFFu);                                \
    out->payload[1u + (size_t)(idx) * 2u + 1u] = (uint8_t)((raw >> 8) & 0xFFu);                    \
  }
  PO32_PATCH_PARAM_FIELDS(ENCODE_PARAM)
#undef ENCODE_PARAM

  out->payload_len = PO32_PATCH_PAYLOAD_BYTES;
  return PO32_OK;
}

static po32_status_t po32_patch_packet_decode(const uint8_t *data, size_t len,
                                              po32_patch_packet_t *out) {
  if (len < PO32_PATCH_PAYLOAD_BYTES)
    return PO32_ERR_FRAME;

  out->instrument = (uint8_t)((data[0] & 0x0Fu) + 1u);
  switch (data[0] & 0xF0u) {
  case PO32_PATCH_SIDE_LEFT_PREFIX:
    out->side = PO32_PATCH_LEFT;
    break;
  case PO32_PATCH_SIDE_RIGHT_PREFIX:
    out->side = PO32_PATCH_RIGHT;
    break;
  default:
    return PO32_ERR_FRAME;
  }

#define DECODE_PARAM(idx, name)                                                                    \
  out->params.name = (float)((uint16_t)data[1u + (size_t)(idx) * 2u] |                             \
                             (uint16_t)((uint16_t)data[1u + (size_t)(idx) * 2u + 1u] << 8)) /      \
                     PO32_PARAM_SCALE;
  PO32_PATCH_PARAM_FIELDS(DECODE_PARAM)
#undef DECODE_PARAM

  return PO32_OK;
}

static po32_status_t po32_knob_packet_encode(const po32_knob_packet_t *pkt, po32_packet_t *out) {
  uint8_t selector;
  po32_zero(out, sizeof(*out));
  out->tag_code = PO32_TAG_KNOB;
  switch (pkt->kind) {
  case PO32_KNOB_PITCH:
    selector = PO32_KNOB_KIND_PITCH_PREFIX;
    break;
  case PO32_KNOB_MORPH:
    selector = PO32_KNOB_KIND_MORPH_PREFIX;
    break;
  default:
    return PO32_ERR_INVALID_ARG;
  }
  out->payload[0] = (uint8_t)(selector | (pkt->instrument - 1u));
  out->payload[1] = pkt->value;
  out->payload_len = 2u;
  return PO32_OK;
}

static po32_status_t po32_knob_packet_decode(const uint8_t *data, size_t len,
                                             po32_knob_packet_t *out) {
  if (len < 2u)
    return PO32_ERR_FRAME;
  out->instrument = (uint8_t)((data[0] & 0x0Fu) + 1u);
  out->kind = (data[0] & PO32_KNOB_KIND_MORPH_PREFIX) ? PO32_KNOB_MORPH : PO32_KNOB_PITCH;
  out->value = data[1];
  return PO32_OK;
}

static po32_status_t po32_reset_packet_encode(const po32_reset_packet_t *pkt, po32_packet_t *out) {
  po32_zero(out, sizeof(*out));
  out->tag_code = PO32_TAG_RESET;
  out->payload[0] = (uint8_t)(PO32_RESET_PREFIX | (pkt->instrument - 1u));
  out->payload_len = 1u;
  return PO32_OK;
}

static po32_status_t po32_reset_packet_decode(const uint8_t *data, size_t len,
                                              po32_reset_packet_t *out) {
  if (len < 1u)
    return PO32_ERR_FRAME;
  out->instrument = (uint8_t)((data[0] & 0x0Fu) + 1u);
  return PO32_OK;
}

static po32_status_t po32_state_packet_encode(const po32_state_packet_t *pkt, po32_packet_t *out) {
  size_t pos = 0u;
  if (pkt->pattern_count > PO32_PATTERN_STEP_COUNT)
    return PO32_ERR_RANGE;
  po32_zero(out, sizeof(*out));
  out->tag_code = PO32_TAG_STATE;
  for (size_t i = 0u; i < PO32_STATE_MORPH_PAIR_COUNT; ++i) {
    out->payload[pos++] = pkt->morph_pairs[i].flag;
    out->payload[pos++] = pkt->morph_pairs[i].morph;
  }
  out->payload[pos++] = pkt->tempo;
  out->payload[pos++] = pkt->swing_times_12;
  out->payload[pos++] = (uint8_t)pkt->pattern_count;
  for (size_t i = 0u; i < pkt->pattern_count; ++i) {
    out->payload[pos++] = pkt->pattern_numbers[i];
  }
  out->payload_len = pos;
  return PO32_OK;
}

static po32_status_t po32_state_packet_decode(const uint8_t *data, size_t len,
                                              po32_state_packet_t *out) {
  size_t pos = 0u;
  if (len < PO32_STATE_PAYLOAD_MIN_BYTES)
    return PO32_ERR_FRAME;
  for (size_t i = 0u; i < PO32_STATE_MORPH_PAIR_COUNT; ++i) {
    out->morph_pairs[i].flag = data[pos++];
    out->morph_pairs[i].morph = data[pos++];
  }
  out->tempo = data[pos++];
  out->swing_times_12 = data[pos++];
  out->pattern_count = data[pos++];
  if (out->pattern_count > PO32_PATTERN_STEP_COUNT ||
      len < PO32_STATE_PAYLOAD_MIN_BYTES + out->pattern_count) {
    return PO32_ERR_FRAME;
  }
  for (size_t i = 0u; i < out->pattern_count; ++i) {
    out->pattern_numbers[i] = data[pos++];
  }
  return PO32_OK;
}

static po32_status_t po32_pattern_packet_encode(const po32_pattern_packet_t *pkt,
                                                po32_packet_t *out) {
  size_t pos = 0u;
  po32_zero(out, sizeof(*out));
  out->tag_code = PO32_TAG_PATTERN;
  out->payload[pos++] = pkt->pattern_number;

  /* The wire format is lane-chunked: 16 trigger bytes, then 32 morph bytes,
     repeated once per lane. */
  for (size_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    size_t lane_base = lane * (size_t)PO32_PATTERN_STEP_COUNT;
    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane_base + step;
      if (pkt->steps[index].instrument == 0u) {
        out->payload[pos++] = 0u;
      } else {
        uint8_t trigger = 0u;
        po32_status_t s =
            po32_pattern_trigger_encode(pkt->steps[index].instrument, pkt->steps[index].fill_rate,
                                        pkt->steps[index].accent, &trigger);
        if (s != PO32_OK)
          return s;
        out->payload[pos++] = trigger;
      }
    }
    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane_base + step;
      out->payload[pos++] = pkt->morph_lanes[index].flag;
      out->payload[pos++] = pkt->morph_lanes[index].morph;
    }
  }
  po32_memcpy(out->payload + pos, pkt->reserved, PO32_PATTERN_RESERVED_COUNT);
  pos += PO32_PATTERN_RESERVED_COUNT;
  out->payload[pos++] = (uint8_t)(pkt->accent_bits & 0xFFu);
  out->payload[pos++] = (uint8_t)((pkt->accent_bits >> 8) & 0xFFu);
  out->payload_len = pos;
  return PO32_OK;
}

static po32_status_t po32_pattern_packet_decode(const uint8_t *data, size_t len,
                                                po32_pattern_packet_t *out) {
  size_t pos = 0u;
  if (len < PO32_PATTERN_PAYLOAD_BYTES)
    return PO32_ERR_FRAME;
  out->pattern_number = data[pos++];

  for (size_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    size_t lane_base = lane * (size_t)PO32_PATTERN_STEP_COUNT;
    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane_base + step;
      po32_status_t s =
          po32_pattern_trigger_decode((uint8_t)lane, data[pos++], &out->steps[index].instrument,
                                      &out->steps[index].fill_rate, &out->steps[index].accent);
      if (s != PO32_OK) {
        out->steps[index].instrument = 0u;
        out->steps[index].fill_rate = 0u;
        out->steps[index].accent = 0;
      }
    }

    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane_base + step;
      out->morph_lanes[index].flag = data[pos++];
      out->morph_lanes[index].morph = data[pos++];
    }
  }
  po32_memcpy(out->reserved, data + pos, PO32_PATTERN_RESERVED_COUNT);
  pos += PO32_PATTERN_RESERVED_COUNT;
  out->accent_bits = (uint16_t)data[pos] | (uint16_t)((uint16_t)data[pos + 1u] << 8);
  return PO32_OK;
}

po32_status_t po32_packet_encode(uint16_t tag, const void *pkt, po32_packet_t *out) {
  if (pkt == NULL || out == NULL)
    return PO32_ERR_INVALID_ARG;
  switch (tag) {
  case PO32_TAG_PATCH:
    return po32_patch_packet_encode((const po32_patch_packet_t *)pkt, out);
  case PO32_TAG_KNOB:
    return po32_knob_packet_encode((const po32_knob_packet_t *)pkt, out);
  case PO32_TAG_RESET:
    return po32_reset_packet_encode((const po32_reset_packet_t *)pkt, out);
  case PO32_TAG_STATE:
    return po32_state_packet_encode((const po32_state_packet_t *)pkt, out);
  case PO32_TAG_PATTERN:
    return po32_pattern_packet_encode((const po32_pattern_packet_t *)pkt, out);
  default:
    return PO32_ERR_INVALID_ARG;
  }
}

po32_status_t po32_packet_decode(uint16_t tag, const uint8_t *data, size_t len, void *out) {
  if (data == NULL || out == NULL)
    return PO32_ERR_INVALID_ARG;
  switch (tag) {
  case PO32_TAG_PATCH:
    return po32_patch_packet_decode(data, len, (po32_patch_packet_t *)out);
  case PO32_TAG_KNOB:
    return po32_knob_packet_decode(data, len, (po32_knob_packet_t *)out);
  case PO32_TAG_RESET:
    return po32_reset_packet_decode(data, len, (po32_reset_packet_t *)out);
  case PO32_TAG_STATE:
    return po32_state_packet_decode(data, len, (po32_state_packet_t *)out);
  case PO32_TAG_PATTERN:
    return po32_pattern_packet_decode(data, len, (po32_pattern_packet_t *)out);
  default:
    return PO32_ERR_INVALID_ARG;
  }
}

/* ── render and streaming decode ───────────────────────────────── */

#define PO32_DEMOD_WORK_SIZE 280

typedef struct po32_demodulator {
  float sample_rate;
  float osc_sin, osc_cos;
  float rot_sin, rot_cos;
  float accum_i, accum_q;
  float prev_i, prev_q;
  float symbol_phase;
  float symbols_per_sample;
  int started;
  uint64_t sync_window;
  uint64_t sync_pattern;
  int synced;
  uint8_t current_byte;
  uint8_t bits_in_byte;
  size_t byte_offset;
  uint16_t crc_state;
  uint8_t work[PO32_DEMOD_WORK_SIZE];
  size_t work_len;
  int packet_count;
  int done;
  po32_final_tail_t tail;
} po32_demodulator_t;

size_t po32_render_sample_count(size_t frame_len, uint32_t sample_rate) {
  if (sample_rate == 0u)
    return 0u;
  {
    float count = (float)(frame_len * 8u) * ((float)sample_rate / (float)PO32_NATIVE_BAUD);
    size_t n = (size_t)count;
    return (count > (float)n) ? n + 1u : n;
  }
}

void po32_modulator_init(po32_modulator_t *m, const uint8_t *frame, size_t frame_len,
                         uint32_t sample_rate) {
  float carrier_step;
  if (m == NULL) {
    return;
  }

  po32_zero(m, sizeof(*m));
  if (frame == NULL || sample_rate == 0u) {
    return;
  }

  m->frame = frame;
  m->frame_len = frame_len;
  m->total_bits = frame_len * 8u;
  m->total_samples = po32_render_sample_count(frame_len, sample_rate);
  m->sample_rate = (float)sample_rate;
  m->symbol_phase = 1.0f;
  m->symbols_per_sample = (float)PO32_NATIVE_BAUD / (float)sample_rate;
  carrier_step = m->symbols_per_sample * (2.0f * PO32_DPSK_CARRIER_CYCLES_PER_SYMBOL * PO32_PI);
  m->rot_sin = po32_lut_sinf(carrier_step);
  m->rot_cos = po32_lut_cosf(carrier_step);
  m->osc_cos = 1.0f;
  m->state = 0;
}

void po32_modulator_reset(po32_modulator_t *m) {
  const uint8_t *frame;
  size_t frame_len;
  uint32_t sample_rate;

  if (m == NULL) {
    return;
  }

  frame = m->frame;
  frame_len = m->frame_len;
  sample_rate = (uint32_t)m->sample_rate;
  po32_modulator_init(m, frame, frame_len, sample_rate);
}

size_t po32_modulator_samples_remaining(const po32_modulator_t *m) {
  if (m == NULL || m->sample_index >= m->total_samples) {
    return 0u;
  }
  return m->total_samples - m->sample_index;
}

int po32_modulator_done(const po32_modulator_t *m) {
  return m == NULL || m->sample_index >= m->total_samples;
}

po32_status_t po32_modulator_render_f32(po32_modulator_t *m, float *out_samples,
                                        size_t out_capacity, size_t *out_len) {
  size_t count, i;
  float osc_s, osc_c, rot_s, rot_c;
  float sym_phase, sym_step;
  const uint8_t *frame;
  size_t bit_cursor, total_bits;
  int state;

  if (out_len != NULL) {
    *out_len = 0u;
  }
  if (m == NULL || out_samples == NULL || out_len == NULL) {
    return PO32_ERR_INVALID_ARG;
  }
  if (m->frame == NULL || m->sample_rate <= 0.0f) {
    return PO32_ERR_INVALID_ARG;
  }

  /* Single loop bound */
  count = m->total_samples - m->sample_index;
  if (count > out_capacity)
    count = out_capacity;

  /* Hoist hot fields into locals so the compiler can register-allocate
   * them without worrying about aliasing through out_samples. */
  osc_s = m->osc_sin;
  osc_c = m->osc_cos;
  rot_s = m->rot_sin;
  rot_c = m->rot_cos;
  sym_phase = m->symbol_phase;
  sym_step = m->symbols_per_sample;
  frame = m->frame;
  bit_cursor = m->bit_cursor;
  total_bits = m->total_bits;
  state = m->state;

  for (i = 0; i < count; ++i) {
    float ns, nc, sample;
    float sign;

    sym_phase += sym_step;
    if (sym_phase >= 1.0f) {
      int bit = 0;
      sym_phase -= 1.0f;
      if (bit_cursor < total_bits) {
        bit = (int)((frame[bit_cursor >> 3] >> (bit_cursor & 7u)) & 1u);
      }
      state ^= bit ^ 1;
      bit_cursor++;
    }

    ns = osc_s * rot_c + osc_c * rot_s;
    nc = osc_c * rot_c - osc_s * rot_s;
    osc_s = ns;
    osc_c = nc;

    /* Branchless sign flip: state is 0 or 1 */
    sign = (float)(2 * state - 1);
    sample = osc_s * sign;

    out_samples[i] = sample;
  }

  /* Write back to struct */
  m->osc_sin = osc_s;
  m->osc_cos = osc_c;
  m->symbol_phase = sym_phase;
  m->bit_cursor = bit_cursor;
  m->state = state;
  m->sample_index += count;

  *out_len = count;
  return PO32_OK;
}

po32_status_t po32_render_dpsk_f32(const uint8_t *frame, size_t frame_len, uint32_t sample_rate,
                                   float *out_samples, size_t out_sample_count) {
  po32_modulator_t modulator;
  size_t needed;
  size_t out_len = 0u;

  if (frame == NULL || out_samples == NULL || sample_rate == 0u) {
    return PO32_ERR_INVALID_ARG;
  }

  needed = po32_render_sample_count(frame_len, sample_rate);
  if (out_sample_count < needed) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }

  po32_modulator_init(&modulator, frame, frame_len, sample_rate);
  if (po32_modulator_render_f32(&modulator, out_samples, out_sample_count, &out_len) != PO32_OK) {
    return PO32_ERR_INVALID_ARG;
  }
  return out_len == needed ? PO32_OK : PO32_ERR_FRAME;
}

static void po32_demodulator_init(po32_demodulator_t *d, float sample_rate) {
  float carrier_step;
  if (d == NULL)
    return;

  po32_zero(d, sizeof(*d));
  if (sample_rate <= 0.0f)
    return;

  d->sample_rate = sample_rate;
  d->symbols_per_sample = PO32_NATIVE_BAUD / sample_rate;
  carrier_step = d->symbols_per_sample * (2.0f * PO32_DPSK_CARRIER_CYCLES_PER_SYMBOL * PO32_PI);
  d->rot_sin = po32_lut_sinf(carrier_step);
  d->rot_cos = po32_lut_cosf(carrier_step);
  d->osc_cos = 1.0f;
  d->symbol_phase = 1.0f;

  /* Build sync pattern: last 8 preamble bytes as a 64-bit word */
  {
    const uint8_t *preamble = po32_preamble_bytes();
    for (size_t i = 0u; i < 8u; ++i) {
      d->sync_pattern |= ((uint64_t)preamble[PO32_PREAMBLE_BYTES - 8u + i]) << (unsigned)(i * 8u);
    }
  }
}

static void po32_demodulator_desync(po32_demodulator_t *d) {
  if (d == NULL) {
    return;
  }

  d->synced = 0;
  d->work_len = 0u;
}

static void po32_demod_on_byte(po32_demodulator_t *d, po32_packet_callback_t cb, void *user,
                               int *stop) {
  uint8_t first_raw;

  if (d->work_len >= PO32_DEMOD_WORK_SIZE) {
    po32_demodulator_desync(d);
    return;
  }
  d->work[d->work_len++] = d->current_byte;
  first_raw = po32_unwhiten_byte(d->crc_state, d->work[0]);

  if (first_raw == PO32_TAIL_MARKER_1) {
    if (d->work_len < PO32_FINAL_TAIL_BYTES) {
      return;
    }
    if (d->work_len != PO32_FINAL_TAIL_BYTES ||
        po32_final_tail_decode(d->crc_state, d->work, d->work_len, &d->tail, NULL) != PO32_OK) {
      po32_demodulator_desync(d);
      return;
    }
    d->done = 1;
    *stop = 1;
    return;
  }

  if (d->work_len >= PO32_PACKET_HEADER_BYTES) {
    po32_packet_t pkt;
    size_t packet_len = 0u;
    size_t consumed = 0u;
    uint16_t s = d->crc_state;
    uint16_t tag_code = 0u;

    if (po32_packet_decode_header(d->work, d->work_len, d->crc_state, &tag_code, &packet_len) !=
        PO32_OK) {
      po32_demodulator_desync(d);
      return;
    }
    if (packet_len > PO32_DEMOD_WORK_SIZE) {
      po32_demodulator_desync(d);
      return;
    }
    if (d->work_len < packet_len) {
      return;
    }
    if (d->work_len != packet_len ||
        po32_packet_decode_bytes(d->work, d->work_len, &s, &consumed, &pkt) != PO32_OK ||
        consumed != packet_len || pkt.tag_code != tag_code) {
      po32_demodulator_desync(d);
      return;
    }

    pkt.offset = d->byte_offset - d->work_len;
    if (cb != NULL && cb(&pkt, user) != 0) {
      *stop = 1;
      return;
    }

    d->crc_state = s;
    d->packet_count++;
    d->work_len = 0u;
  }
}

typedef struct {
  po32_demodulator_t *demod;
  po32_packet_callback_t cb;
  void *user;
  float osc_sin, osc_cos;
  float accum_i, accum_q;
  float prev_i, prev_q;
  float symbol_phase;
  float rot_sin, rot_cos;
  float symbols_per_sample;
  uint64_t sync_window;
  uint64_t sync_pattern;
  uint8_t current_byte;
  uint8_t bits_in_byte;
  size_t byte_offset;
  int started;
  int synced;
} po32_demod_run_t;

static void po32_demod_run_init(po32_demod_run_t *run, po32_demodulator_t *d,
                                po32_packet_callback_t cb, void *user) {
  po32_zero(run, sizeof(*run));
  run->demod = d;
  run->cb = cb;
  run->user = user;
  run->osc_sin = d->osc_sin;
  run->osc_cos = d->osc_cos;
  run->accum_i = d->accum_i;
  run->accum_q = d->accum_q;
  run->prev_i = d->prev_i;
  run->prev_q = d->prev_q;
  run->symbol_phase = d->symbol_phase;
  run->rot_sin = d->rot_sin;
  run->rot_cos = d->rot_cos;
  run->symbols_per_sample = d->symbols_per_sample;
  run->sync_window = d->sync_window;
  run->sync_pattern = d->sync_pattern;
  run->current_byte = d->current_byte;
  run->bits_in_byte = d->bits_in_byte;
  run->byte_offset = d->byte_offset;
  run->started = d->started;
  run->synced = d->synced;
}

static void po32_demod_run_commit(const po32_demod_run_t *run) {
  po32_demodulator_t *d = run->demod;

  d->osc_sin = run->osc_sin;
  d->osc_cos = run->osc_cos;
  d->accum_i = run->accum_i;
  d->accum_q = run->accum_q;
  d->prev_i = run->prev_i;
  d->prev_q = run->prev_q;
  d->symbol_phase = run->symbol_phase;
  d->sync_window = run->sync_window;
  d->current_byte = run->current_byte;
  d->bits_in_byte = run->bits_in_byte;
  d->byte_offset = run->byte_offset;
  d->started = run->started;
  d->synced = run->synced;
}

static po32_status_t po32_demod_run_sample(po32_demod_run_t *run, float sample, int *stop) {
  float ns, nc;

  ns = run->osc_sin * run->rot_cos + run->osc_cos * run->rot_sin;
  nc = run->osc_cos * run->rot_cos - run->osc_sin * run->rot_sin;
  run->osc_sin = ns;
  run->osc_cos = nc;

  run->accum_i += sample * run->osc_sin;
  run->accum_q += sample * run->osc_cos;

  run->symbol_phase += run->symbols_per_sample;
  if (run->symbol_phase >= 1.0f) {
    run->symbol_phase -= 1.0f;

    if (run->started) {
      float dot = run->accum_i * run->prev_i + run->accum_q * run->prev_q;
      uint8_t bit = (uint8_t)(dot >= 0.0f ? 1u : 0u);

      if ((run->prev_i > 0.0f) != (run->accum_i > 0.0f)) {
        float ai = run->accum_i < 0.0f ? -run->accum_i : run->accum_i;
        float pi = run->prev_i < 0.0f ? -run->prev_i : run->prev_i;
        if (pi + ai > 0.0f) {
          float err = (ai - pi) / (ai + pi);
          run->symbol_phase += err * PO32_TIMING_RECOVERY_GAIN;
        }
      }

      if (!run->synced) {
        run->sync_window = (run->sync_window >> 1) | ((uint64_t)bit << 63);
        if (run->sync_window == run->sync_pattern) {
          run->synced = 1;
          run->demod->crc_state = PO32_INITIAL_STATE;
          run->current_byte = 0u;
          run->bits_in_byte = 0u;
          run->byte_offset = 0u;
          run->demod->work_len = 0u;
          run->demod->packet_count = 0;
        }
      } else {
        run->current_byte |= (uint8_t)(bit << run->bits_in_byte);
        run->bits_in_byte++;

        if (run->bits_in_byte == 8u) {
          run->byte_offset++;
          run->demod->current_byte = run->current_byte;
          run->demod->bits_in_byte = run->bits_in_byte;
          run->demod->byte_offset = run->byte_offset;
          run->demod->synced = run->synced;
          po32_demod_on_byte(run->demod, run->cb, run->user, stop);
          run->synced = run->demod->synced;
          run->current_byte = 0u;
          run->bits_in_byte = 0u;
          if (*stop) {
            return PO32_OK;
          }
        }
      }
    } else {
      run->started = 1;
    }

    run->prev_i = run->accum_i;
    run->prev_q = run->accum_q;
    run->accum_i = 0.0f;
    run->accum_q = 0.0f;
  }

  return PO32_OK;
}

static po32_status_t po32_demodulator_push(po32_demodulator_t *d, const float *samples,
                                           size_t count, po32_packet_callback_t cb, void *user) {
  po32_demod_run_t run;
  int stop = 0;

  if (d == NULL || samples == NULL)
    return PO32_ERR_INVALID_ARG;
  if (d->done)
    return PO32_OK;

  po32_demod_run_init(&run, d, cb, user);

  for (size_t i = 0u; i < count; ++i) {
    po32_status_t status = po32_demod_run_sample(&run, samples[i], &stop);
    if (status != PO32_OK) {
      return status;
    }
    if (stop) {
      break;
    }
  }

  po32_demod_run_commit(&run);

  return PO32_OK;
}

typedef struct {
  po32_builder_t *builder;
  po32_status_t status;
} po32_decode_ctx_t;

static int po32_decode_collect_packet(const po32_packet_t *packet, void *user) {
  po32_decode_ctx_t *ctx = (po32_decode_ctx_t *)user;

  if (ctx == NULL || ctx->builder == NULL || packet == NULL)
    return 1;
  if (ctx->status != PO32_OK)
    return 1;

  ctx->status = po32_builder_append(ctx->builder, packet);
  return ctx->status != PO32_OK;
}

static void po32_decode_result_clear(po32_decode_result_t *out_result, size_t *out_len) {
  if (out_result != NULL) {
    po32_zero(out_result, sizeof(*out_result));
  }
  if (out_len != NULL) {
    *out_len = 0u;
  }
}

static po32_status_t po32_decode_prepare(const float *samples, size_t count, float sample_rate,
                                         uint8_t *out_frame, size_t out_capacity,
                                         const size_t *out_len, po32_builder_t *builder,
                                         po32_decode_ctx_t *ctx) {
  if (samples == NULL || count == 0u || sample_rate <= 0.0f || out_frame == NULL ||
      out_len == NULL || builder == NULL || ctx == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  po32_builder_init(builder, out_frame, out_capacity);
  if (builder->length < PO32_PREAMBLE_BYTES) {
    return PO32_ERR_BUFFER_TOO_SMALL;
  }

  ctx->builder = builder;
  ctx->status = PO32_OK;
  return PO32_OK;
}

static po32_status_t po32_decode_finalize(const po32_demodulator_t *demod,
                                          const po32_decode_ctx_t *ctx,
                                          po32_decode_result_t *out_result, size_t *out_len) {
  po32_status_t status;

  if (demod == NULL || ctx == NULL || out_len == NULL) {
    return PO32_ERR_INVALID_ARG;
  }
  if (ctx->status != PO32_OK) {
    return ctx->status;
  }
  if (!demod->done) {
    if (out_result != NULL) {
      out_result->packet_count = demod->packet_count;
      out_result->done = 0;
    }
    return PO32_ERR_FRAME;
  }

  status = po32_builder_finish(ctx->builder, out_len);
  if (status != PO32_OK) {
    return status;
  }

  if (out_result != NULL) {
    out_result->packet_count = demod->packet_count;
    out_result->done = demod->done;
    out_result->tail = demod->tail;
  }

  return PO32_OK;
}

po32_status_t po32_decode_f32(const float *samples, size_t count, float sample_rate,
                              po32_decode_result_t *out_result, uint8_t *out_frame,
                              size_t out_capacity, size_t *out_len) {
  po32_demodulator_t demod;
  po32_builder_t builder;
  po32_decode_ctx_t ctx;
  po32_status_t status;

  po32_decode_result_clear(out_result, out_len);
  status = po32_decode_prepare(samples, count, sample_rate, out_frame, out_capacity, out_len,
                               &builder, &ctx);
  if (status != PO32_OK) {
    return status;
  }

  po32_demodulator_init(&demod, sample_rate);
  status = po32_demodulator_push(&demod, samples, count, po32_decode_collect_packet, &ctx);
  if (status != PO32_OK) {
    return status;
  }
  return po32_decode_finalize(&demod, &ctx, out_result, out_len);
}
