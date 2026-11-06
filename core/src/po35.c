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

typedef struct po35_speech_edit_scan {
  const size_t *edit_index_by_slot;
  uint16_t seen_slots;
  size_t removed_data_packet_bytes;
  size_t active_expected;
  size_t active_received;
  size_t failed_edit_index;
  uint8_t failed_slot;
  int active_slot;
  po35_speech_edit_reason_t reason;
  po_status_t status;
} po35_speech_edit_scan_t;

static void speech_edit_plan_clear(po35_speech_edit_plan_t *plan) {
  *plan = (po35_speech_edit_plan_t){
      .failed_edit_index = SIZE_MAX,
      .failed_slot = UINT8_MAX,
      .reason = PO35_SPEECH_EDIT_REASON_NONE,
  };
}

static void speech_edit_plan_reject(po35_speech_edit_plan_t *plan, size_t edit_index, uint8_t slot,
                                    po35_speech_edit_reason_t reason) {
  speech_edit_plan_clear(plan);
  plan->failed_edit_index = edit_index;
  plan->failed_slot = slot;
  plan->reason = reason;
}

static int speech_edit_ranges_overlap(const void *left, size_t left_bytes, const void *right,
                                      size_t right_bytes) {
  uintptr_t left_start, right_start;
  if (left_bytes == 0u || right_bytes == 0u)
    return 0;
  left_start = (uintptr_t)left;
  right_start = (uintptr_t)right;
  if (left_start > UINTPTR_MAX - left_bytes || right_start > UINTPTR_MAX - right_bytes)
    return 1;
  return left_start < right_start + right_bytes && right_start < left_start + left_bytes;
}

static void speech_edit_scan_fail(po35_speech_edit_scan_t *scan, po_status_t status) {
  scan->status = status;
}

static void speech_edit_scan_reject_active(po35_speech_edit_scan_t *scan,
                                           po35_speech_edit_reason_t reason) {
  size_t edit_index = SIZE_MAX;
  if (scan->active_slot >= 0)
    edit_index = scan->edit_index_by_slot[scan->active_slot];
  scan->status = PO_ERR_PARSE;
  if (edit_index != SIZE_MAX) {
    scan->failed_edit_index = edit_index;
    scan->failed_slot = (uint8_t)scan->active_slot;
    scan->reason = reason;
  }
}

static int speech_edit_known_group_interruption(uint16_t tag_code) {
  switch (tag_code) {
  case PO35_TAG_PATCH:
  case PO35_TAG_TABLE_7BE7:
  case PO35_TAG_TABLE_1C7F:
  case PO35_TAG_PATTERN_EMPTY:
  case PO35_TAG_PATTERN:
  case PO35_TAG_SETTINGS:
    return 1;
  default:
    return 0;
  }
}

static int speech_edit_scan_packet(const po_packet_t *packet, void *user) {
  po35_speech_edit_scan_t *scan = (po35_speech_edit_scan_t *)user;
  switch (packet->tag_code) {
  case PO35_TAG_SPEECH_HEADER: {
    po35_speech_header_t header;
    if (scan->active_slot >= 0) {
      speech_edit_scan_reject_active(scan, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);
      return 1;
    }
    if (po35_speech_header_decode(packet->payload, packet->payload_len, &header) != PO_OK ||
        header.slot >= PO35_SPEECH_SLOT_COUNT ||
        (scan->seen_slots & (uint16_t)(1u << header.slot))) {
      speech_edit_scan_fail(scan, PO_ERR_PARSE);
      return 1;
    }
    scan->seen_slots |= (uint16_t)(1u << header.slot);
    scan->active_slot = header.slot;
    scan->active_expected = (size_t)header.record_count * PO35_SPEECH_RECORD_BYTES;
    scan->active_received = 0u;
    return 0;
  }
  case PO35_TAG_SPEECH_DATA:
    if (scan->active_slot < 0 || packet->payload_len == 0u ||
        packet->payload_len > scan->active_expected - scan->active_received) {
      speech_edit_scan_reject_active(scan, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);
      return 1;
    }
    scan->active_received += packet->payload_len;
    if (scan->edit_index_by_slot[scan->active_slot] != SIZE_MAX) {
      size_t packet_bytes = packet->payload_len + PO_WIRE_OVERHEAD_BYTES;
      if (scan->removed_data_packet_bytes > SIZE_MAX - packet_bytes) {
        speech_edit_scan_fail(scan, PO_ERR_RANGE);
        return 1;
      }
      scan->removed_data_packet_bytes += packet_bytes;
    }
    return 0;
  case PO35_TAG_SPEECH_END:
    if (scan->active_slot < 0 || packet->payload_len != 0u ||
        scan->active_received != scan->active_expected) {
      speech_edit_scan_reject_active(scan, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);
      return 1;
    }
    scan->active_slot = -1;
    return 0;
  default:
    if (scan->active_slot >= 0) {
      if (scan->edit_index_by_slot[scan->active_slot] != SIZE_MAX) {
        speech_edit_scan_reject_active(scan,
                                       PO35_SPEECH_EDIT_REASON_UNKNOWN_SELECTED_GROUP_EXTENSION);
        return 1;
      }
      if (speech_edit_known_group_interruption(packet->tag_code)) {
        speech_edit_scan_fail(scan, PO_ERR_PARSE);
        return 1;
      }
    }
    return 0;
  }
}

po_status_t po35_speech_edit_plan(const uint8_t *frame, size_t frame_len,
                                  const po35_speech_edit_t *edits, size_t edit_count,
                                  po35_speech_edit_plan_t *plan) {
  size_t edit_index_by_slot[PO35_SPEECH_SLOT_COUNT];
  po35_speech_edit_scan_t scan = {
      .edit_index_by_slot = edit_index_by_slot,
      .failed_edit_index = SIZE_MAX,
      .failed_slot = UINT8_MAX,
      .active_slot = -1,
      .reason = PO35_SPEECH_EDIT_REASON_NONE,
      .status = PO_OK,
  };
  po35_decode_result_t result;
  po_status_t status;
  uint16_t selected_slots = 0u;
  size_t required_frame_bytes;

  if (plan == NULL)
    return PO_ERR_INVALID_ARG;
  if (frame != NULL && speech_edit_ranges_overlap(plan, sizeof(*plan), frame, frame_len))
    return PO_ERR_INVALID_ARG;
  if (edits != NULL && edit_count > SIZE_MAX / sizeof(*edits)) {
    speech_edit_plan_clear(plan);
    return PO_ERR_RANGE;
  }
  if (edits != NULL &&
      speech_edit_ranges_overlap(plan, sizeof(*plan), edits, edit_count * sizeof(*edits)))
    return PO_ERR_INVALID_ARG;
  if (edits != NULL) {
    for (size_t i = 0; i < edit_count; ++i)
      if (edits[i].records != NULL &&
          speech_edit_ranges_overlap(plan, sizeof(*plan), edits[i].records, edits[i].record_bytes))
        return PO_ERR_INVALID_ARG;
  }

  speech_edit_plan_clear(plan);
  if (frame == NULL || (edit_count != 0u && edits == NULL))
    return PO_ERR_INVALID_ARG;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i)
    edit_index_by_slot[i] = SIZE_MAX;
  for (size_t i = 0; i < edit_count; ++i) {
    const po35_speech_edit_t *edit = edits + i;
    uint16_t slot_bit;
    if (edit->slot >= PO35_SPEECH_SLOT_COUNT) {
      speech_edit_plan_reject(plan, i, edit->slot, PO35_SPEECH_EDIT_REASON_SLOT_OUT_OF_RANGE);
      return PO_ERR_RANGE;
    }
    slot_bit = (uint16_t)(1u << edit->slot);
    if (selected_slots & slot_bit) {
      speech_edit_plan_reject(plan, i, edit->slot, PO35_SPEECH_EDIT_REASON_DUPLICATE_SLOT);
      return PO_ERR_RANGE;
    }
    if (edit->record_bytes != 0u && edit->records == NULL)
      return PO_ERR_INVALID_ARG;
    if (edit->record_bytes % PO35_SPEECH_RECORD_BYTES != 0u) {
      speech_edit_plan_reject(plan, i, edit->slot, PO35_SPEECH_EDIT_REASON_INVALID_RECORD_SIZE);
      return PO_ERR_RANGE;
    }
    if (edit->record_bytes / PO35_SPEECH_RECORD_BYTES > UINT16_MAX) {
      speech_edit_plan_reject(plan, i, edit->slot, PO35_SPEECH_EDIT_REASON_INVALID_RECORD_COUNT);
      return PO_ERR_RANGE;
    }
    selected_slots |= slot_bit;
    edit_index_by_slot[edit->slot] = i;
  }

  status = po35_frame_parse(frame, frame_len, speech_edit_scan_packet, &scan, &result);
  if (scan.status != PO_OK) {
    if (scan.reason != PO35_SPEECH_EDIT_REASON_NONE)
      speech_edit_plan_reject(plan, scan.failed_edit_index, scan.failed_slot, scan.reason);
    else
      speech_edit_plan_clear(plan);
    return scan.status;
  }
  if (status != PO_OK || !result.done || result.stopped || result.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  if (scan.active_slot >= 0) {
    speech_edit_scan_reject_active(&scan, PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP);
    if (scan.reason != PO35_SPEECH_EDIT_REASON_NONE)
      speech_edit_plan_reject(plan, scan.failed_edit_index, scan.failed_slot, scan.reason);
    return PO_ERR_PARSE;
  }
  for (size_t i = 0; i < edit_count; ++i) {
    if (!(scan.seen_slots & (uint16_t)(1u << edits[i].slot))) {
      speech_edit_plan_reject(plan, i, edits[i].slot, PO35_SPEECH_EDIT_REASON_ABSENT_SLOT);
      return PO_ERR_PARSE;
    }
  }
  if (scan.removed_data_packet_bytes > frame_len)
    return PO_ERR_RANGE;
  required_frame_bytes = frame_len - scan.removed_data_packet_bytes;
  for (size_t i = 0; i < edit_count; ++i) {
    size_t record_count = edits[i].record_bytes / PO35_SPEECH_RECORD_BYTES;
    size_t packet_count = record_count / 5u + (record_count % 5u != 0u);
    size_t replacement_bytes = edits[i].record_bytes + packet_count * PO_WIRE_OVERHEAD_BYTES;
    if (replacement_bytes > SIZE_MAX - required_frame_bytes)
      return PO_ERR_RANGE;
    required_frame_bytes += replacement_bytes;
  }
  *plan = (po35_speech_edit_plan_t){
      .required_workspace_bytes = 0u,
      .required_frame_bytes = required_frame_bytes,
      .affected_slot_count = edit_count,
      .failed_edit_index = SIZE_MAX,
      .selected_slot_mask = selected_slots,
      .failed_slot = UINT8_MAX,
      .reason = PO35_SPEECH_EDIT_REASON_NONE,
  };
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
