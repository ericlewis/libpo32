#ifndef PO_WIRE_INTERNAL_H
#define PO_WIRE_INTERNAL_H

#include "po_common.h"

/* Mechanisms only: model modules retain tag, ordering and completion policy.
 * Unchecked read/write helpers require the caller to validate the whole span. */
#define PO_WIRE_HEADER_BYTES   3u
#define PO_WIRE_TRAILER_BYTES  2u
#define PO_WIRE_OVERHEAD_BYTES 5u
#define PO_WIRE_COUNTER_BYTES  4096u
#define PO_WIRE_TAIL_TAG       0x71c3u

static inline uint16_t po_wire_crc_mix_state(uint16_t state, uint8_t x) {
  uint16_t masked = (uint16_t)(x & 0xffu);
  return (uint16_t)((((uint32_t)state << 8) | masked) ^ ((uint32_t)masked << 12) ^
                    ((uint32_t)masked << 5));
}

static inline uint16_t po_wire_crc_update(uint16_t state, uint8_t raw) {
  uint8_t x = (uint8_t)(raw ^ (state >> 8));
  x ^= (uint8_t)(x >> 4);
  return po_wire_crc_mix_state(state, x);
}

static inline uint8_t po_wire_whiten(uint16_t state, uint8_t raw) {
  return (uint8_t)(raw + (state & 0xffu));
}

static inline uint8_t po_wire_unwhiten(uint16_t state, uint8_t coded) {
  return (uint8_t)(coded - (state & 0xffu));
}

static inline uint8_t po_wire_read_byte(const uint8_t *src, size_t *pos, uint16_t *state) {
  uint8_t raw = po_wire_unwhiten(*state, src[(*pos)++]);
  *state = po_wire_crc_update(*state, raw);
  return raw;
}

static inline void po_wire_write_byte(uint8_t *dst, size_t *pos, uint16_t *state, uint8_t raw) {
  dst[(*pos)++] = po_wire_whiten(*state, raw);
  *state = po_wire_crc_update(*state, raw);
}

static inline void po_wire_write_trailer(uint8_t *dst, size_t *pos, uint16_t *state,
                                         uint16_t trailer) {
  po_wire_write_byte(dst, pos, state, (uint8_t)trailer);
  po_wire_write_byte(dst, pos, state, (uint8_t)(trailer >> 8));
}

static inline void po_wire_write_packet(uint8_t *dst, size_t *pos, uint16_t *state, uint16_t tag,
                                        const uint8_t *payload, size_t len) {
  po_wire_write_byte(dst, pos, state, (uint8_t)tag);
  po_wire_write_byte(dst, pos, state, (uint8_t)(tag >> 8));
  po_wire_write_byte(dst, pos, state, (uint8_t)len);
  for (size_t i = 0; i < len; ++i)
    po_wire_write_byte(dst, pos, state, payload[i]);
  po_wire_write_trailer(dst, pos, state, *state);
}

static inline po_status_t po_wire_packet_header(const uint8_t *src, size_t len, uint16_t state,
                                                uint16_t *tag, size_t *packet_len) {
  size_t pos = 0;
  uint8_t lo, hi, count;
  if (src == NULL || tag == NULL || packet_len == NULL || len < PO_WIRE_HEADER_BYTES)
    return PO_ERR_INVALID_ARG;
  lo = po_wire_read_byte(src, &pos, &state);
  hi = po_wire_read_byte(src, &pos, &state);
  count = po_wire_unwhiten(state, src[pos]);
  *tag = (uint16_t)(lo | ((uint16_t)hi << 8));
  *packet_len = PO_WIRE_OVERHEAD_BYTES + count;
  return PO_OK;
}

static inline po_status_t po_wire_read_packet(const uint8_t *src, size_t len, uint16_t *state,
                                              size_t *consumed, po_packet_t *packet) {
  size_t pos = 0;
  uint16_t s, trailer;
  uint8_t lo, hi;
  if (src == NULL || state == NULL || consumed == NULL || packet == NULL)
    return PO_ERR_INVALID_ARG;
  if (len < PO_WIRE_OVERHEAD_BYTES)
    return PO_ERR_FRAME;
  s = *state;
  lo = po_wire_read_byte(src, &pos, &s);
  hi = po_wire_read_byte(src, &pos, &s);
  packet->tag_code = (uint16_t)(lo | ((uint16_t)hi << 8));
  packet->payload_len = po_wire_read_byte(src, &pos, &s);
  if (len - pos < packet->payload_len + PO_WIRE_TRAILER_BYTES)
    return PO_ERR_FRAME;
  for (size_t i = 0; i < packet->payload_len; ++i)
    packet->payload[i] = po_wire_read_byte(src, &pos, &s);
  trailer = s;
  lo = po_wire_read_byte(src, &pos, &s);
  hi = po_wire_read_byte(src, &pos, &s);
  if (lo != (uint8_t)trailer || hi != (uint8_t)(trailer >> 8))
    return PO_ERR_FRAME;
  packet->trailer.lo = lo;
  packet->trailer.hi = hi;
  packet->trailer.state = trailer;
  packet->trailer.matches_state = 1;
  *state = s;
  *consumed = pos;
  return PO_OK;
}

static inline void po_wire_counter_write(uint8_t *dst) {
  static const uint8_t sync[4] = {0x4d, 0xb8, 0x16, 0x5d};
  for (size_t i = 0; i < PO_WIRE_COUNTER_BYTES - 4u; ++i)
    dst[i] = (uint8_t)i;
  for (size_t i = 0; i < 4u; ++i)
    dst[PO_WIRE_COUNTER_BYTES - 4u + i] = sync[i];
}

static inline int po_wire_counter_matches(const uint8_t *src, size_t len) {
  static const uint8_t sync[4] = {0x4d, 0xb8, 0x16, 0x5d};
  if (src == NULL || len < PO_WIRE_COUNTER_BYTES)
    return 0;
  for (size_t i = 0; i < PO_WIRE_COUNTER_BYTES - 4u; ++i)
    if (src[i] != (uint8_t)i)
      return 0;
  for (size_t i = 0; i < 4u; ++i)
    if (src[PO_WIRE_COUNTER_BYTES - 4u + i] != sync[i])
      return 0;
  return 1;
}

static inline void po_wire_write_special(uint8_t *dst, size_t *pos, uint16_t *state) {
  uint8_t x;
  dst[(*pos)++] = (uint8_t)*state;
  x = (uint8_t)((*state >> 12) ^ (*state >> 8));
  *state = po_wire_crc_mix_state(*state, x);
}

static inline void po_wire_write_tail(uint8_t *dst, size_t *pos, uint16_t *state) {
  po_wire_write_byte(dst, pos, state, 0xc3);
  po_wire_write_byte(dst, pos, state, 0x71);
  po_wire_write_special(dst, pos, state);
  po_wire_write_trailer(dst, pos, state, *state);
}

static inline po_status_t po_wire_read_tail(uint16_t state, const uint8_t *src, size_t len,
                                            po_final_tail_t *tail, size_t *consumed) {
  uint8_t raw1, raw2, raw4, raw5, x;
  uint16_t state1, state2, state3, state4, state5;
  if (src == NULL || len < PO_FINAL_TAIL_BYTES)
    return PO_ERR_INVALID_ARG;
  raw1 = po_wire_unwhiten(state, src[0]);
  state1 = po_wire_crc_update(state, raw1);
  raw2 = po_wire_unwhiten(state1, src[1]);
  state2 = po_wire_crc_update(state1, raw2);
  if (raw1 != 0xc3u || raw2 != 0x71u || src[2] != (uint8_t)state2)
    return PO_ERR_FRAME;
  x = (uint8_t)((state2 >> 12) ^ (state2 >> 8));
  state3 = po_wire_crc_mix_state(state2, x);
  raw4 = po_wire_unwhiten(state3, src[3]);
  state4 = po_wire_crc_update(state3, raw4);
  raw5 = po_wire_unwhiten(state4, src[4]);
  state5 = po_wire_crc_update(state4, raw5);
  if (raw4 != (uint8_t)state3 || raw5 != (uint8_t)(state3 >> 8))
    return PO_ERR_FRAME;
  if (tail != NULL)
    *tail = (po_final_tail_t){raw1, raw2, src[2], raw4, raw5, state2, state3, state5};
  if (consumed != NULL)
    *consumed = PO_FINAL_TAIL_BYTES;
  return PO_OK;
}

#endif
