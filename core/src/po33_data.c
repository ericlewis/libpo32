#include "po33.h"
static void copy_bytes(uint8_t *out, const uint8_t *in, size_t count) {
  for (size_t i = 0; i < count; ++i)
    out[i] = in[i];
}
static uint32_t read32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(value >> (8u * i));
}
po_status_t po33_settings_decode(const uint8_t *data, size_t len, po33_settings_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO33_SETTINGS_BYTES)
    return PO_ERR_FRAME;
  if (data[383] > PO33_CHAIN_CAPACITY)
    return PO_ERR_RANGE;
  copy_bytes(out->prefix_raw, data, 255);
  copy_bytes(out->chain, data + 255, PO33_CHAIN_CAPACITY);
  out->chain_length = data[383];
  out->tempo = data[384];
  out->swing_raw = data[385];
  copy_bytes(out->state_raw, data + 386, 16);
  copy_bytes(out->suffix_raw, data + 402, 108);
  return PO_OK;
}
po_status_t po33_settings_encode(const po33_settings_t *value, uint8_t *out, size_t capacity) {
  if (value == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO33_SETTINGS_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  if (value->chain_length > PO33_CHAIN_CAPACITY)
    return PO_ERR_RANGE;
  copy_bytes(out, value->prefix_raw, 255);
  copy_bytes(out + 255, value->chain, PO33_CHAIN_CAPACITY);
  out[383] = value->chain_length;
  out[384] = value->tempo;
  out[385] = value->swing_raw;
  copy_bytes(out + 386, value->state_raw, 16);
  copy_bytes(out + 402, value->suffix_raw, 108);
  return PO_OK;
}
po_status_t po33_pattern_decode(const uint8_t *data, size_t len, po33_pattern_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO33_PATTERN_BYTES)
    return PO_ERR_FRAME;
  for (size_t step = 0; step < PO33_STEP_COUNT; ++step)
    for (size_t sound = 0; sound < PO33_SOUND_COUNT; ++sound) {
      size_t index = step * PO33_SOUND_COUNT + sound;
      copy_bytes(out->events[step][sound].event_raw, data + 3u * index, 3);
      out->events[step][sound].control_raw = data[768 + index];
    }
  copy_bytes(out->footer_raw, data + 1024, 8);
  return PO_OK;
}
po_status_t po33_pattern_encode(const po33_pattern_t *value, uint8_t *out, size_t capacity) {
  if (value == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO33_PATTERN_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  for (size_t step = 0; step < PO33_STEP_COUNT; ++step)
    for (size_t sound = 0; sound < PO33_SOUND_COUNT; ++sound) {
      size_t index = step * PO33_SOUND_COUNT + sound;
      copy_bytes(out + 3u * index, value->events[step][sound].event_raw, 3);
      out[768 + index] = value->events[step][sound].control_raw;
    }
  copy_bytes(out + 1024, value->footer_raw, 8);
  return PO_OK;
}
po_status_t po33_sound_header_decode(const uint8_t *data, size_t len, po33_sound_header_t *out) {
  if (data == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (len != PO33_SOUND_HEADER_BYTES)
    return PO_ERR_FRAME;
  out->range_start_raw = read32(data);
  out->range_end_raw = read32(data + 4);
  out->source_selector = data[8];
  return PO_OK;
}
po_status_t po33_sound_header_encode(const po33_sound_header_t *value, uint8_t *out,
                                     size_t capacity) {
  if (value == NULL || out == NULL)
    return PO_ERR_INVALID_ARG;
  if (capacity < PO33_SOUND_HEADER_BYTES)
    return PO_ERR_BUFFER_TOO_SMALL;
  write32(out, value->range_start_raw);
  write32(out + 4, value->range_end_raw);
  out[8] = value->source_selector;
  return PO_OK;
}
