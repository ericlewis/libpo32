#include "po35.h"
#include "po_wire_internal.h"

static void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; ++i)
    dst[i] = src[i];
}

static uint16_t read_u16le(const uint8_t *data) {
  return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static void write_u16le(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

po_status_t po35_frame_parse(const uint8_t *frame, size_t frame_len, po_packet_callback_t callback,
                             void *user, po35_decode_result_t *result) {
  po35_decode_result_t local;
  uint16_t state = PO_INITIAL_STATE;
  size_t pos = PO35_PREAMBLE_BYTES;
  if (result == NULL)
    result = &local;
  *result = (po35_decode_result_t){0};
  result->final_state = state;
  if (frame == NULL)
    return PO_ERR_INVALID_ARG;
  if (!po_wire_counter_matches(frame, frame_len))
    return PO_ERR_FRAME;
  result->frame_bytes = pos;
  while (frame_len - pos >= PO_WIRE_OVERHEAD_BYTES) {
    po_final_tail_t tail;
    po_packet_t packet = {0};
    size_t consumed;
    if (po_wire_read_tail(state, frame + pos, frame_len - pos, &tail, NULL) == PO_OK) {
      result->frame_bytes = pos + PO_FINAL_TAIL_BYTES;
      result->final_state = tail.final_state;
      result->done = 1;
      return PO_OK;
    }
    if (po_wire_read_packet(frame + pos, frame_len - pos, &state, &consumed, &packet) != PO_OK ||
        packet.tag_code == PO_WIRE_TAIL_TAG)
      return PO_ERR_FRAME;
    packet.offset = pos;
    pos += consumed;
    ++result->packet_count;
    result->frame_bytes = pos;
    result->final_state = state;
    if (callback != NULL && callback(&packet, user) != 0) {
      result->stopped = 1;
      return PO_OK;
    }
  }
  return PO_ERR_FRAME;
}

static int builder_valid(const po35_builder_t *builder) {
  return builder != NULL && builder->buffer != NULL && builder->length >= PO35_PREAMBLE_BYTES &&
         builder->length <= builder->capacity;
}

po_status_t po35_builder_init(po35_builder_t *builder, uint8_t *buffer, size_t capacity) {
  if (builder == NULL)
    return PO_ERR_INVALID_ARG;
  *builder = (po35_builder_t){0};
  if (buffer == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO35_PREAMBLE_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  builder->buffer = buffer;
  builder->capacity = capacity;
  builder->length = PO35_PREAMBLE_BYTES;
  builder->state = PO_INITIAL_STATE;
  po_wire_counter_write(buffer);
  return PO_OK;
}

po_status_t po35_builder_append_packet(po35_builder_t *builder, uint16_t tag_code,
                                       const uint8_t *payload, size_t payload_len,
                                       size_t *packet_offset) {
  if (!builder_valid(builder) || (payload_len != 0u && payload == NULL) ||
      tag_code == PO_WIRE_TAIL_TAG)
    return PO_ERR_INVALID_ARG;
  if (builder->finished)
    return PO_ERR_FRAME;
  if (payload_len > PO_MAX_PAYLOAD)
    return PO_ERR_RANGE;
  if (builder->capacity - builder->length < payload_len + PO_WIRE_OVERHEAD_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  if (packet_offset != NULL)
    *packet_offset = builder->length;
  po_wire_write_packet(builder->buffer, &builder->length, &builder->state, tag_code, payload,
                       payload_len);
  return PO_OK;
}

po_status_t po35_builder_finish(po35_builder_t *builder, size_t *frame_len) {
  if (!builder_valid(builder))
    return PO_ERR_INVALID_ARG;
  if (!builder->finished) {
    if (builder->capacity - builder->length < PO_FINAL_TAIL_BYTES)
      return PO_ERR_BUFFER_TOO_SMALL;
    po_wire_write_tail(builder->buffer, &builder->length, &builder->state);
    builder->finished = 1;
  }
  if (frame_len != NULL)
    *frame_len = builder->length;
  return PO_OK;
}

po_status_t po35_table_7be7_decode(const uint8_t *data, size_t len, po35_table_7be7_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO35_TABLE_7BE7_BYTES)
    return PO_ERR_PARSE;
  out->index = data[0];
  copy_bytes(out->data_raw, data + 1, sizeof(out->data_raw));
  return PO_OK;
}

po_status_t po35_table_7be7_encode(const po35_table_7be7_t *value, uint8_t *out, size_t capacity) {
  if (value == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO35_TABLE_7BE7_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  out[0] = value->index;
  copy_bytes(out + 1, value->data_raw, sizeof(value->data_raw));
  return PO_OK;
}

po_status_t po35_table_1c7f_decode(const uint8_t *data, size_t len, po35_table_1c7f_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO35_TABLE_1C7F_BYTES)
    return PO_ERR_PARSE;
  out->index = data[0];
  copy_bytes(out->data_raw, data + 1, sizeof(out->data_raw));
  return PO_OK;
}

po_status_t po35_table_1c7f_encode(const po35_table_1c7f_t *value, uint8_t *out, size_t capacity) {
  if (value == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO35_TABLE_1C7F_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  out[0] = value->index;
  copy_bytes(out + 1, value->data_raw, sizeof(value->data_raw));
  return PO_OK;
}

po_status_t po35_speech_header_decode(const uint8_t *data, size_t len, po35_speech_header_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO35_SPEECH_HEADER_BYTES)
    return PO_ERR_PARSE;
  out->slot = data[0];
  out->record_count = (uint16_t)(((uint16_t)data[1] << 8) | data[2]);
  out->records_per_second = data[3];
  copy_bytes(out->metadata, data + 4, 5);
  return PO_OK;
}

po_status_t po35_speech_header_encode(const po35_speech_header_t *header, uint8_t *out,
                                      size_t capacity) {
  if (header == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO35_SPEECH_HEADER_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  out[0] = header->slot;
  out[1] = (uint8_t)(header->record_count >> 8);
  out[2] = (uint8_t)header->record_count;
  out[3] = header->records_per_second;
  copy_bytes(out + 4, header->metadata, 5);
  return PO_OK;
}

po_status_t po35_speech_record_decode(const uint8_t *data, size_t len, po35_speech_record_t *out) {
  uint16_t pitch;
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO35_SPEECH_RECORD_BYTES)
    return PO_ERR_PARSE;
  pitch = read_u16le(data);
  out->pitch = (int16_t)(pitch <= 32767u ? (int32_t)pitch : (int32_t)pitch - 65536);
  out->gain = data[2];
  out->unvoiced = data[3];
  for (size_t i = 0; i < 10; ++i)
    out->low_lar_raw[i] = (int8_t)(data[4 + i] <= 127u ? (int)data[4 + i] : (int)data[4 + i] - 256);
  for (size_t i = 0; i < 30; ++i)
    out->high_lar_raw[i] =
        (int8_t)(data[14 + i] <= 127u ? (int)data[14 + i] : (int)data[14 + i] - 256);
  return PO_OK;
}

po_status_t po35_speech_record_encode(const po35_speech_record_t *record, uint8_t *out,
                                      size_t capacity) {
  if (record == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO35_SPEECH_RECORD_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  write_u16le(out, (uint16_t)record->pitch);
  out[2] = record->gain;
  out[3] = record->unvoiced;
  for (size_t i = 0; i < 10; ++i)
    out[4 + i] = (uint8_t)record->low_lar_raw[i];
  for (size_t i = 0; i < 30; ++i)
    out[14 + i] = (uint8_t)record->high_lar_raw[i];
  return PO_OK;
}

po_status_t po35_settings_decode(const uint8_t *data, size_t len, po35_settings_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len < PO35_SETTINGS_BASE_BYTES || len > PO_MAX_PAYLOAD ||
      len != PO35_SETTINGS_BASE_BYTES + (size_t)data[155])
    return PO_ERR_PARSE;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    copy_bytes(out->speech[i].parameters, data + 8 * i, 4);
    copy_bytes(out->speech[i].state_raw, data + 8 * i + 4, 4);
  }
  for (size_t i = 0; i < PO35_DRUM_COUNT; ++i) {
    out->drums[i].pitch = data[120 + 2 * i];
    out->drums[i].morph = data[121 + 2 * i];
  }
  out->tempo = data[152];
  copy_bytes(out->state_raw, data + 153, 2);
  out->chain_count = data[155];
  copy_bytes(out->chain, data + 156, out->chain_count);
  return PO_OK;
}

po_status_t po35_settings_encode(const po35_settings_t *settings, uint8_t *out, size_t capacity,
                                 size_t *out_len) {
  size_t len;
  if (settings == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (settings->chain_count > PO35_CHAIN_MAX_COUNT)
    return PO_ERR_RANGE;
  len = PO35_SETTINGS_BASE_BYTES + settings->chain_count;
  if (capacity < len)
    return PO_ERR_BUFFER_TOO_SMALL;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    copy_bytes(out + 8 * i, settings->speech[i].parameters, 4);
    copy_bytes(out + 8 * i + 4, settings->speech[i].state_raw, 4);
  }
  for (size_t i = 0; i < PO35_DRUM_COUNT; ++i) {
    out[120 + 2 * i] = settings->drums[i].pitch;
    out[121 + 2 * i] = settings->drums[i].morph;
  }
  out[152] = settings->tempo;
  copy_bytes(out + 153, settings->state_raw, 2);
  out[155] = settings->chain_count;
  copy_bytes(out + 156, settings->chain, settings->chain_count);
  if (out_len != NULL)
    *out_len = len;
  return PO_OK;
}

po_status_t po35_pattern_decode(const uint8_t *data, size_t len, po35_pattern_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO35_PATTERN_BYTES)
    return PO_ERR_PARSE;
  out->pattern_index = data[0];
  copy_bytes(out->speech_events, data + 1, 16);
  copy_bytes(out->speech_secondary_raw, data + 17, 16);
  for (size_t i = 0; i < PO35_PATTERN_STEP_COUNT; ++i)
    copy_bytes(out->speech_parameters[i], data + 33 + 4 * i, 4);
  copy_bytes(out->drum_events, data + 97, 16);
  for (size_t i = 0; i < PO35_PATTERN_STEP_COUNT; ++i) {
    out->drums[i].pitch = data[113 + 2 * i];
    out->drums[i].morph = data[114 + 2 * i];
  }
  copy_bytes(out->index_pairs_raw, data + 145, 16);
  copy_bytes(out->step_state_raw, data + 161, 16);
  copy_bytes(out->tail_raw, data + 177, 4);
  out->derived_mask = read_u16le(data + 181);
  return PO_OK;
}

po_status_t po35_pattern_encode(const po35_pattern_t *pattern, uint8_t *out, size_t capacity) {
  if (pattern == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO35_PATTERN_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  out[0] = pattern->pattern_index;
  copy_bytes(out + 1, pattern->speech_events, 16);
  copy_bytes(out + 17, pattern->speech_secondary_raw, 16);
  for (size_t i = 0; i < PO35_PATTERN_STEP_COUNT; ++i)
    copy_bytes(out + 33 + 4 * i, pattern->speech_parameters[i], 4);
  copy_bytes(out + 97, pattern->drum_events, 16);
  for (size_t i = 0; i < PO35_PATTERN_STEP_COUNT; ++i) {
    out[113 + 2 * i] = pattern->drums[i].pitch;
    out[114 + 2 * i] = pattern->drums[i].morph;
  }
  copy_bytes(out + 145, pattern->index_pairs_raw, 16);
  copy_bytes(out + 161, pattern->step_state_raw, 16);
  copy_bytes(out + 177, pattern->tail_raw, 4);
  write_u16le(out + 181, pattern->derived_mask);
  return PO_OK;
}
