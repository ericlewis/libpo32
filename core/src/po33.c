#include "po33.h"
#include "po_wire_internal.h"

uint8_t po33_sample_encode(int16_t sample) {
  int32_t magnitude = sample < 0 ? -(int32_t)sample : (int32_t)sample;
  unsigned int lo = 0u, hi = 127u;
  while (lo < hi) {
    unsigned int mid = lo + (hi - lo) / 2u;
    if ((int32_t)po33_sample_decode((uint8_t)mid) < magnitude)
      lo = mid + 1u;
    else
      hi = mid;
  }
  if (lo != 0u) {
    int32_t upper = po33_sample_decode((uint8_t)lo);
    int32_t lower = po33_sample_decode((uint8_t)(lo - 1u));
    if (magnitude - lower <= upper - magnitude)
      --lo;
  }
  if (lo == 0u)
    return 0u;
  return (uint8_t)(sample < 0 ? 255u - lo : lo);
}

/* Call only after checking the complete packet fits. */
static void builder_packet(po33_builder_t *builder, uint16_t tag, const uint8_t *payload,
                           size_t payload_len) {
  po_wire_write_packet(builder->buffer, &builder->length, &builder->state, tag, payload,
                       payload_len);
}

static int builder_valid(const po33_builder_t *builder) {
  return builder != NULL && builder->buffer != NULL &&
         builder->length >= PO33_PREAMBLE_BYTES + 5u && builder->length <= builder->capacity;
}

po_status_t po33_builder_init(po33_builder_t *builder, uint8_t *buffer, size_t capacity) {
  if (builder == NULL)
    return PO_ERR_INVALID_ARG;
  *builder = (po33_builder_t){0};
  if (buffer == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO33_PREAMBLE_BYTES + 5u)
    return PO_ERR_BUFFER_TOO_SMALL;
  builder->buffer = buffer;
  builder->capacity = capacity;
  builder->state = PO_INITIAL_STATE;
  po_wire_counter_write(buffer);
  builder->length = PO33_PREAMBLE_BYTES;
  builder_packet(builder, PO33_TAG_BEGIN, NULL, 0u);
  return PO_OK;
}

po_status_t po33_builder_append_packet(po33_builder_t *builder, uint16_t tag_code,
                                       const uint8_t *payload, size_t payload_len,
                                       size_t *packet_offset) {
  if (!builder_valid(builder) || (payload_len != 0u && payload == NULL) ||
      tag_code == PO33_TAG_BEGIN || tag_code == PO33_TAG_END)
    return PO_ERR_INVALID_ARG;
  if (builder->finished)
    return PO_ERR_FRAME;
  if (payload_len > 255u)
    return PO_ERR_RANGE;
  if (builder->capacity - builder->length < payload_len + 5u)
    return PO_ERR_BUFFER_TOO_SMALL;
  if (packet_offset != NULL)
    *packet_offset = builder->length;
  builder_packet(builder, tag_code, payload, payload_len);
  return PO_OK;
}

po_status_t po33_builder_finish(po33_builder_t *builder, size_t *frame_len) {
  if (!builder_valid(builder))
    return PO_ERR_INVALID_ARG;
  if (!builder->finished) {
    if (builder->capacity - builder->length < 5u)
      return PO_ERR_BUFFER_TOO_SMALL;
    builder_packet(builder, PO33_TAG_END, NULL, 0u);
    builder->finished = 1;
  }
  if (frame_len != NULL)
    *frame_len = builder->length;
  return PO_OK;
}

int16_t po33_sample_decode(uint8_t encoded) {
  unsigned int negative = encoded & 0x80u;
  unsigned int magnitude = negative ? (unsigned int)(~encoded) & 0x7fu : encoded;
  int value = (int)((((magnitude & 15u) * 8u + 132u) << (magnitude >> 4u)) - 132u);
  return (int16_t)(negative ? -value : value);
}

po_status_t po33_pack_symbols(const uint8_t *phases, size_t symbol_count, unsigned int parity,
                              uint8_t *out_wire, size_t capacity, size_t *out_len) {
  size_t required;
  if (out_len == NULL || parity > 1u || (phases == NULL && symbol_count != 0u))
    return PO_ERR_INVALID_ARG;
  *out_len = 0u;
  if (symbol_count > (size_t)-1 / 2u)
    return PO_ERR_RANGE;
  required = symbol_count > 1u + parity ? (symbol_count - 1u - parity) / 2u : 0u;
  *out_len = required;
  if (capacity < required)
    return PO_ERR_BUFFER_TOO_SMALL;
  if (required != 0u && out_wire == NULL)
    return PO_ERR_INVALID_ARG;
  for (size_t i = 0; i < 2u * symbol_count; ++i)
    if (phases[i] > 3u)
      return PO_ERR_RANGE;
  for (size_t i = 0; i < required; ++i) {
    uint8_t value = 0u;
    for (size_t j = 0; j < 2u; ++j) {
      size_t k = 1u + parity + 2u * i + j;
      unsigned int left = (phases[2u * k] + 4u - phases[2u * (k - 1u)]) & 3u;
      unsigned int right = (phases[2u * k + 1u] + 4u - phases[2u * (k - 1u) + 1u]) & 3u;
      value |= (uint8_t)((right | (left << 2u)) << (4u * j));
    }
    out_wire[i] = value;
  }
  return PO_OK;
}

po_status_t po33_frame_parse(const uint8_t *frame, size_t frame_len, po_packet_callback_t callback,
                             void *user, po33_decode_result_t *result) {
  po33_decode_result_t local = {0};
  uint16_t state = PO_INITIAL_STATE;
  size_t pos = PO33_PREAMBLE_BYTES;
  if (result == NULL)
    result = &local;
  *result = (po33_decode_result_t){0};
  result->final_state = state;
  if (frame == NULL)
    return PO_ERR_INVALID_ARG;
  if (!po_wire_counter_matches(frame, frame_len))
    return PO_ERR_FRAME;
  result->frame_bytes = pos;
  while (frame_len - pos >= 5u) {
    po_packet_t packet = {0};
    size_t consumed;
    packet.offset = pos;
    if (po_wire_read_packet(frame + pos, frame_len - pos, &state, &consumed, &packet) != PO_OK)
      return PO_ERR_FRAME;
    if ((packet.tag_code == PO33_TAG_BEGIN || packet.tag_code == PO33_TAG_END) &&
        packet.payload_len != 0u)
      return PO_ERR_FRAME;
    if ((result->packet_count == 0u) != (packet.tag_code == PO33_TAG_BEGIN))
      return PO_ERR_FRAME;
    pos += consumed;
    ++result->packet_count;
    result->frame_bytes = pos;
    result->final_state = state;
    if (callback != NULL && callback(&packet, user) != 0) {
      result->stopped = 1;
      return PO_OK;
    }
    if (packet.tag_code == PO33_TAG_END) {
      result->done = 1;
      return PO_OK;
    }
  }
  return PO_ERR_FRAME;
}
