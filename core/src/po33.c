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

typedef struct po33_topology_record_state {
  uint8_t selector;
  uint8_t header_bytes[PO33_SOUND_HEADER_BYTES];
  size_t header_length;
  size_t data_bytes;
  size_t selection_offset;
  size_t selection_length;
  size_t first_data_offset;
  size_t last_data_end;
  int active;
  int have_data;
  int unknown_extension;
} po33_topology_record_state_t;

typedef struct po33_topology_observed_record {
  uint8_t selector;
  po33_sound_header_t header;
  po33_frame_span_t selection_span;
  po33_frame_span_t data_span;
  size_t data_bytes;
  unsigned int flags;
} po33_topology_observed_record_t;

typedef po_status_t (*po33_topology_record_callback_t)(
    const po33_topology_observed_record_t *record, void *user);

typedef struct po33_topology_stream {
  po33_topology_record_state_t record;
  po33_topology_record_callback_t callback;
  void *user;
  po_status_t status;
} po33_topology_stream_t;

static po_status_t topology_finish_record(po33_topology_stream_t *stream) {
  po33_topology_observed_record_t observed;
  po_status_t status;
  if (!stream->record.active)
    return PO_OK;
  if (!stream->record.have_data) {
    stream->record = (po33_topology_record_state_t){0};
    return PO_OK;
  }
  if (stream->record.header_length != PO33_SOUND_HEADER_BYTES)
    return PO_ERR_FRAME;
  observed = (po33_topology_observed_record_t){0};
  status = po33_sound_header_decode(stream->record.header_bytes, PO33_SOUND_HEADER_BYTES,
                                    &observed.header);
  if (status != PO_OK)
    return status;
  observed.selector = stream->record.selector;
  observed.selection_span =
      (po33_frame_span_t){stream->record.selection_offset, stream->record.selection_length};
  observed.data_span =
      (po33_frame_span_t){stream->record.first_data_offset,
                          stream->record.last_data_end - stream->record.first_data_offset};
  observed.data_bytes = stream->record.data_bytes;
  if (stream->record.data_bytes == PO33_SOUND_HEADER_BYTES)
    observed.flags |= PO33_SOUND_RECORD_FLAG_HEADER_ONLY;
  if (stream->record.unknown_extension)
    observed.flags |= PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION;
  status = stream->callback(&observed, stream->user);
  if (status != PO_OK)
    return status;
  stream->record = (po33_topology_record_state_t){0};
  return PO_OK;
}

static po_status_t topology_start_record(po33_topology_stream_t *stream,
                                         const po_packet_t *packet) {
  po_status_t status = topology_finish_record(stream);
  if (status != PO_OK)
    return status;
  if (packet->payload_len != 1u)
    return PO_ERR_FRAME;
  stream->record = (po33_topology_record_state_t){
      .selector = packet->payload[0],
      .selection_offset = packet->offset,
      .selection_length = PO_WIRE_OVERHEAD_BYTES + packet->payload_len,
      .active = 1,
  };
  return PO_OK;
}

static po_status_t topology_append_data(po33_topology_stream_t *stream, const po_packet_t *packet) {
  size_t copied = 0u;
  if (!stream->record.active)
    return PO_ERR_FRAME;
  if (packet->payload_len > SIZE_MAX - stream->record.data_bytes)
    return PO_ERR_RANGE;
  if (!stream->record.have_data) {
    stream->record.first_data_offset = packet->offset;
    stream->record.have_data = 1;
  }
  stream->record.last_data_end = packet->offset + PO_WIRE_OVERHEAD_BYTES + packet->payload_len;
  if (stream->record.header_length < PO33_SOUND_HEADER_BYTES) {
    copied = PO33_SOUND_HEADER_BYTES - stream->record.header_length;
    if (copied > packet->payload_len)
      copied = packet->payload_len;
    for (size_t i = 0; i < copied; ++i)
      stream->record.header_bytes[stream->record.header_length + i] = packet->payload[i];
    stream->record.header_length += copied;
  }
  stream->record.data_bytes += packet->payload_len;
  return PO_OK;
}

static int topology_packet(const po_packet_t *packet, void *user) {
  po33_topology_stream_t *stream = (po33_topology_stream_t *)user;
  po_status_t status = PO_OK;
  switch (packet->tag_code) {
  case PO33_TAG_SOUND_SELECT:
    status = topology_start_record(stream, packet);
    break;
  case PO33_TAG_SOUND_DATA:
    status = topology_append_data(stream, packet);
    break;
  case PO33_TAG_END:
    status = topology_finish_record(stream);
    break;
  default:
    if (stream->record.active)
      stream->record.unknown_extension = 1;
    break;
  }
  stream->status = status;
  return status != PO_OK;
}

static po_status_t topology_parse_records(const uint8_t *frame, size_t frame_len,
                                          po33_topology_record_callback_t callback, void *user) {
  po33_decode_result_t result;
  po33_topology_stream_t stream = {
      .callback = callback,
      .user = user,
      .status = PO_OK,
  };
  po_status_t status = po33_frame_parse(frame, frame_len, topology_packet, &stream, &result);
  if (stream.status != PO_OK)
    return stream.status;
  if (status != PO_OK || !result.done)
    return PO_ERR_FRAME;
  return PO_OK;
}

typedef struct po33_topology_count_pass {
  size_t sound_record_count;
  size_t pool_count;
  uint8_t owner_count[UINT8_MAX + 1u];
} po33_topology_count_pass_t;

static po_status_t topology_count_record(const po33_topology_observed_record_t *record,
                                         void *user) {
  po33_topology_count_pass_t *pass = (po33_topology_count_pass_t *)user;
  if (pass->sound_record_count == SIZE_MAX)
    return PO_ERR_RANGE;
  ++pass->sound_record_count;
  if (record->data_bytes > PO33_SOUND_HEADER_BYTES) {
    if (pass->pool_count == SIZE_MAX)
      return PO_ERR_RANGE;
    ++pass->pool_count;
    if (pass->owner_count[record->selector] < 2u)
      ++pass->owner_count[record->selector];
  }
  return PO_OK;
}

typedef struct po33_topology_reference_pass {
  const uint8_t *owner_count;
  size_t reference_count;
} po33_topology_reference_pass_t;

static po_status_t topology_count_reference(const po33_topology_observed_record_t *record,
                                            void *user) {
  po33_topology_reference_pass_t *pass = (po33_topology_reference_pass_t *)user;
  if (pass->owner_count[record->header.source_selector] != 1u)
    return PO_OK;
  if (pass->reference_count == SIZE_MAX)
    return PO_ERR_RANGE;
  ++pass->reference_count;
  return PO_OK;
}

typedef struct po33_topology_measurement {
  size_t sound_record_count;
  size_t pool_count;
  size_t reference_count;
} po33_topology_measurement_t;

static po_status_t topology_measure(const uint8_t *frame, size_t frame_len,
                                    po33_topology_measurement_t *measurement) {
  po33_topology_count_pass_t count_pass = {0};
  po33_topology_reference_pass_t reference_pass;
  po_status_t status = topology_parse_records(frame, frame_len, topology_count_record, &count_pass);
  if (status != PO_OK)
    return status;
  reference_pass = (po33_topology_reference_pass_t){
      .owner_count = count_pass.owner_count,
  };
  status = topology_parse_records(frame, frame_len, topology_count_reference, &reference_pass);
  if (status != PO_OK)
    return status;
  *measurement = (po33_topology_measurement_t){
      .sound_record_count = count_pass.sound_record_count,
      .pool_count = count_pass.pool_count,
      .reference_count = reference_pass.reference_count,
  };
  return PO_OK;
}

static int po33_ranges_overlap(const void *left, size_t left_bytes, const void *right,
                               size_t right_bytes);

po_status_t po33_pool_topology_requirements(const uint8_t *frame, size_t frame_len,
                                            size_t *sound_record_count, size_t *pool_count,
                                            size_t *reference_count) {
  po33_topology_measurement_t measurement;
  po_status_t status;
  if (frame == NULL || sound_record_count == NULL || pool_count == NULL || reference_count == NULL)
    return PO_ERR_INVALID_ARG;
  if (po33_ranges_overlap(sound_record_count, sizeof(*sound_record_count), pool_count,
                          sizeof(*pool_count)) ||
      po33_ranges_overlap(sound_record_count, sizeof(*sound_record_count), reference_count,
                          sizeof(*reference_count)) ||
      po33_ranges_overlap(pool_count, sizeof(*pool_count), reference_count,
                          sizeof(*reference_count)) ||
      po33_ranges_overlap(sound_record_count, sizeof(*sound_record_count), frame, frame_len) ||
      po33_ranges_overlap(pool_count, sizeof(*pool_count), frame, frame_len) ||
      po33_ranges_overlap(reference_count, sizeof(*reference_count), frame, frame_len))
    return PO_ERR_INVALID_ARG;
  status = topology_measure(frame, frame_len, &measurement);
  if (status != PO_OK)
    return status;
  *sound_record_count = measurement.sound_record_count;
  *pool_count = measurement.pool_count;
  *reference_count = measurement.reference_count;
  return PO_OK;
}

typedef struct po33_topology_build_pass {
  po33_sound_record_t *sound_records;
  po33_pool_t *pools;
  size_t sound_record_count;
  size_t pool_count;
} po33_topology_build_pass_t;

static int po33_ranges_overlap(const void *left, size_t left_bytes, const void *right,
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

static int topology_build_has_invalid_aliases(const uint8_t *frame, size_t frame_len,
                                              po33_sound_record_t *sound_records,
                                              size_t sound_record_capacity, po33_pool_t *pools,
                                              size_t pool_capacity,
                                              po33_pool_reference_t *references,
                                              size_t reference_capacity) {
  size_t sound_record_bytes, pool_bytes, reference_bytes;
  sound_record_bytes = sound_record_capacity * sizeof(*sound_records);
  pool_bytes = pool_capacity * sizeof(*pools);
  reference_bytes = reference_capacity * sizeof(*references);
  return po33_ranges_overlap(frame, frame_len, sound_records, sound_record_bytes) ||
         po33_ranges_overlap(frame, frame_len, pools, pool_bytes) ||
         po33_ranges_overlap(frame, frame_len, references, reference_bytes) ||
         po33_ranges_overlap(sound_records, sound_record_bytes, pools, pool_bytes) ||
         po33_ranges_overlap(sound_records, sound_record_bytes, references, reference_bytes) ||
         po33_ranges_overlap(pools, pool_bytes, references, reference_bytes);
}

static po_status_t topology_build_record(const po33_topology_observed_record_t *record,
                                         void *user) {
  po33_topology_build_pass_t *pass = (po33_topology_build_pass_t *)user;
  po33_sound_record_t *output = pass->sound_records + pass->sound_record_count;
  *output = (po33_sound_record_t){
      .selector = record->selector,
      .range_start_raw = record->header.range_start_raw,
      .range_end_raw = record->header.range_end_raw,
      .source_selector = record->header.source_selector,
      .flags = record->flags,
      .selection_span = record->selection_span,
      .data_span = record->data_span,
      .owned_pool_index = SIZE_MAX,
      .referenced_pool_index = SIZE_MAX,
  };
  if (record->data_bytes > PO33_SOUND_HEADER_BYTES) {
    output->owned_pool_index = pass->pool_count;
    pass->pools[pass->pool_count] = (po33_pool_t){
        .owner_record_index = pass->sound_record_count,
        .data_span = record->data_span,
        .data_bytes = record->data_bytes,
    };
    ++pass->pool_count;
  }
  ++pass->sound_record_count;
  return PO_OK;
}

static void topology_resolve_references(po33_sound_record_t *sound_records,
                                        size_t sound_record_count,
                                        po33_pool_reference_t *references) {
  uint8_t selector_count[UINT8_MAX + 1u] = {0};
  uint8_t owner_count[UINT8_MAX + 1u] = {0};
  size_t owner_pool[UINT8_MAX + 1u];
  size_t reference_index = 0u;
  for (size_t i = 0; i <= UINT8_MAX; ++i)
    owner_pool[i] = SIZE_MAX;
  for (size_t i = 0; i < sound_record_count; ++i) {
    po33_sound_record_t *record = sound_records + i;
    if (selector_count[record->selector] < 2u)
      ++selector_count[record->selector];
    if (record->owned_pool_index != SIZE_MAX) {
      if (owner_count[record->selector] == 0u)
        owner_pool[record->selector] = record->owned_pool_index;
      if (owner_count[record->selector] < 2u)
        ++owner_count[record->selector];
    }
  }
  for (size_t i = 0; i < sound_record_count; ++i) {
    po33_sound_record_t *record = sound_records + i;
    if (selector_count[record->selector] > 1u)
      record->flags |= PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR;
    if (owner_count[record->source_selector] == 0u) {
      record->flags |= PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED;
    } else if (owner_count[record->source_selector] > 1u) {
      record->flags |= PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS;
    } else {
      record->referenced_pool_index = owner_pool[record->source_selector];
      references[reference_index++] = (po33_pool_reference_t){
          .pool_index = record->referenced_pool_index,
          .sound_record_index = i,
      };
    }
  }
}

po_status_t po33_pool_topology_build(const uint8_t *frame, size_t frame_len,
                                     po33_sound_record_t *sound_records,
                                     size_t sound_record_capacity, po33_pool_t *pools,
                                     size_t pool_capacity, po33_pool_reference_t *references,
                                     size_t reference_capacity) {
  po33_topology_measurement_t measurement;
  po33_topology_build_pass_t build_pass;
  po_status_t status;
  if (frame == NULL || (sound_records == NULL && sound_record_capacity != 0u) ||
      (pools == NULL && pool_capacity != 0u) || (references == NULL && reference_capacity != 0u))
    return PO_ERR_INVALID_ARG;
  if (sound_record_capacity > SIZE_MAX / sizeof(*sound_records) ||
      pool_capacity > SIZE_MAX / sizeof(*pools) ||
      reference_capacity > SIZE_MAX / sizeof(*references))
    return PO_ERR_RANGE;
  if (topology_build_has_invalid_aliases(frame, frame_len, sound_records, sound_record_capacity,
                                         pools, pool_capacity, references, reference_capacity))
    return PO_ERR_INVALID_ARG;
  status = topology_measure(frame, frame_len, &measurement);
  if (status != PO_OK)
    return status;
  if ((measurement.sound_record_count != 0u && sound_records == NULL) ||
      (measurement.pool_count != 0u && pools == NULL) ||
      (measurement.reference_count != 0u && references == NULL))
    return PO_ERR_INVALID_ARG;
  if (sound_record_capacity < measurement.sound_record_count ||
      pool_capacity < measurement.pool_count || reference_capacity < measurement.reference_count)
    return PO_ERR_BUFFER_TOO_SMALL;
  build_pass = (po33_topology_build_pass_t){
      .sound_records = sound_records,
      .pools = pools,
  };
  status = topology_parse_records(frame, frame_len, topology_build_record, &build_pass);
  if (status != PO_OK || build_pass.sound_record_count != measurement.sound_record_count ||
      build_pass.pool_count != measurement.pool_count)
    return status == PO_OK ? PO_ERR_FRAME : status;
  topology_resolve_references(sound_records, measurement.sound_record_count, references);
  return PO_OK;
}

static int pool_replacement_group_boundary_valid(const po33_sound_record_t *record,
                                                 const po33_pool_t *pool) {
  size_t selection_end;
  if (record->selection_span.length == 0u || record->data_span.length == 0u ||
      record->selection_span.offset > SIZE_MAX - record->selection_span.length ||
      record->data_span.offset > SIZE_MAX - record->data_span.length ||
      record->data_span.offset != pool->data_span.offset ||
      record->data_span.length != pool->data_span.length ||
      pool->data_bytes > SIZE_MAX - PO_WIRE_OVERHEAD_BYTES ||
      pool->data_span.length < pool->data_bytes + PO_WIRE_OVERHEAD_BYTES)
    return 0;
  selection_end = record->selection_span.offset + record->selection_span.length;
  return selection_end <= record->data_span.offset;
}

po_status_t po33_pool_replacement_plan(const po33_sound_record_t *sound_records,
                                       size_t sound_record_count, const po33_pool_t *pools,
                                       size_t pool_count, size_t target_sound_record_index,
                                       size_t requested_sample_bytes,
                                       po33_pool_replacement_plan_t *plan) {
  po33_pool_replacement_plan_t result = {
      .target_pool_index = SIZE_MAX,
      .reason = PO33_POOL_REPLACEMENT_REASON_NONE,
  };
  const po33_sound_record_t *target;
  const po33_pool_t *pool;
  size_t target_pool_index;
  po33_pool_replacement_reason_t topology_reason = PO33_POOL_REPLACEMENT_REASON_NONE;
  po_status_t status = PO_OK;

  if (plan == NULL)
    return PO_ERR_INVALID_ARG;
  if (sound_record_count > SIZE_MAX / sizeof(*sound_records) ||
      pool_count > SIZE_MAX / sizeof(*pools))
    return PO_ERR_INVALID_ARG;
  if ((sound_records != NULL && po33_ranges_overlap(plan, sizeof(*plan), sound_records,
                                                    sound_record_count * sizeof(*sound_records))) ||
      (pools != NULL &&
       po33_ranges_overlap(plan, sizeof(*plan), pools, pool_count * sizeof(*pools))))
    return PO_ERR_INVALID_ARG;
  if ((sound_records == NULL && sound_record_count != 0u) || (pools == NULL && pool_count != 0u)) {
    status = PO_ERR_INVALID_ARG;
    goto finish;
  }
  result.requested_sample_bytes = requested_sample_bytes;
  if (target_sound_record_index >= sound_record_count) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_TARGET_RECORD_OUT_OF_RANGE;
    status = PO_ERR_RANGE;
    goto finish;
  }
  target = sound_records + target_sound_record_index;
  if ((target->flags & PO33_SOUND_RECORD_FLAG_HEADER_ONLY) != 0u ||
      target->owned_pool_index == SIZE_MAX) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_TARGET_NOT_OWNED;
    status = PO_ERR_RANGE;
    goto finish;
  }
  if (target->owned_pool_index >= pool_count) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_GROUP_BOUNDARY;
    status = PO_ERR_PARSE;
    goto finish;
  }
  target_pool_index = target->owned_pool_index;
  pool = pools + target_pool_index;
  result.target_pool_index = target_pool_index;
  result.affected_sound_record_count = 1u;
  for (size_t i = 0; i < sound_record_count; ++i) {
    const po33_sound_record_t *record = sound_records + i;
    if (record->referenced_pool_index == target_pool_index) {
      if (result.reference_count == SIZE_MAX ||
          (i != target_sound_record_index && result.affected_sound_record_count == SIZE_MAX)) {
        result.reason = PO33_POOL_REPLACEMENT_REASON_COUNT_OVERFLOW;
        status = PO_ERR_RANGE;
        goto finish;
      }
      ++result.reference_count;
      if (i != target_sound_record_index)
        ++result.affected_sound_record_count;
    }
    if (topology_reason == PO33_POOL_REPLACEMENT_REASON_NONE &&
        record->source_selector == target->selector &&
        record->referenced_pool_index != target_pool_index) {
      topology_reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
    }
    if (topology_reason == PO33_POOL_REPLACEMENT_REASON_NONE &&
        record->referenced_pool_index == target_pool_index &&
        (record->source_selector != target->selector ||
         (record->flags &
          (PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR | PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED |
           PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS)) != 0u)) {
      topology_reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
    }
  }
  if (pool->owner_record_index != target_sound_record_index ||
      pool->data_bytes <= PO33_SOUND_HEADER_BYTES ||
      !pool_replacement_group_boundary_valid(target, pool)) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_GROUP_BOUNDARY;
    status = PO_ERR_PARSE;
    goto finish;
  }
  for (size_t i = 0; i < pool_count; ++i)
    if (i != target_pool_index && pools[i].owner_record_index == target_sound_record_index) {
      result.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_GROUP_BOUNDARY;
      status = PO_ERR_PARSE;
      goto finish;
    }

  result.existing_allocation_bytes = pool->data_bytes - PO33_SOUND_HEADER_BYTES;
  result.existing_allocation_fits = requested_sample_bytes <= result.existing_allocation_bytes;
  result.growth_required = !result.existing_allocation_fits;
  if (result.existing_allocation_fits) {
    result.required_allocation_bytes = result.existing_allocation_bytes;
  } else {
    if (requested_sample_bytes > SIZE_MAX - (PO33_POOL_ALLOCATION_BLOCK_BYTES - 1u)) {
      result.reason = PO33_POOL_REPLACEMENT_REASON_REQUIRED_ALLOCATION_OVERFLOW;
      status = PO_ERR_RANGE;
      goto finish;
    }
    result.required_allocation_bytes =
        ((requested_sample_bytes + PO33_POOL_ALLOCATION_BLOCK_BYTES - 1u) /
         PO33_POOL_ALLOCATION_BLOCK_BYTES) *
        PO33_POOL_ALLOCATION_BLOCK_BYTES;
    result.required_growth_bytes =
        result.required_allocation_bytes - result.existing_allocation_bytes;
  }
  if ((target->flags & PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR) != 0u) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_DUPLICATE_TARGET_SELECTOR;
    status = PO_ERR_PARSE;
    goto finish;
  }
  if ((target->flags & PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS) != 0u) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
    status = PO_ERR_PARSE;
    goto finish;
  }
  if ((target->flags & PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION) != 0u) {
    result.reason = PO33_POOL_REPLACEMENT_REASON_UNKNOWN_TARGET_EXTENSION;
    status = PO_ERR_PARSE;
    goto finish;
  }
  if (topology_reason != PO33_POOL_REPLACEMENT_REASON_NONE) {
    result.reason = topology_reason;
    status = PO_ERR_PARSE;
    goto finish;
  }
  if (result.existing_allocation_bytes % PO33_POOL_ALLOCATION_BLOCK_BYTES != 0u) {
    if (result.growth_required) {
      result.reason = PO33_POOL_REPLACEMENT_REASON_EXISTING_ALLOCATION_UNALIGNED;
      status = PO_ERR_RANGE;
      goto finish;
    }
  } else {
    result.growth_structurally_eligible = 1;
  }

finish:
  *plan = result;
  return status;
}

typedef struct po33_pool_edit_measure {
  size_t target_sound_record_index;
  size_t sound_record_count;
  size_t pool_count;
  size_t target_pool_index;
  uint8_t selector_count[UINT8_MAX + 1u];
  uint8_t owner_count[UINT8_MAX + 1u];
  size_t owner_pool_index[UINT8_MAX + 1u];
  po33_topology_observed_record_t target;
  int target_found;
} po33_pool_edit_measure_t;

typedef struct po33_pool_edit_resolution {
  const po33_pool_edit_measure_t *measurement;
  size_t sound_record_index;
  size_t pool_index;
  size_t reference_count;
  size_t affected_sound_record_count;
  po33_pool_replacement_reason_t topology_reason;
} po33_pool_edit_resolution_t;

static po_status_t topology_parse_exact_records(const uint8_t *frame, size_t frame_len,
                                                po33_topology_record_callback_t callback,
                                                void *user) {
  po33_decode_result_t result;
  po33_topology_stream_t stream = {
      .callback = callback,
      .user = user,
      .status = PO_OK,
  };
  po_status_t status = po33_frame_parse(frame, frame_len, topology_packet, &stream, &result);
  if (stream.status != PO_OK)
    return stream.status;
  if (status != PO_OK || !result.done || result.stopped || result.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  return PO_OK;
}

typedef struct po33_capacity_scan {
  po33_capacity_report_t report;
  uint8_t selectors[256], owners[256], references[256];
  int active, unsafe;
} po33_capacity_scan_t;

static int capacity_round(size_t bytes, size_t *out) {
  if (bytes > SIZE_MAX - (PO33_POOL_ALLOCATION_BLOCK_BYTES - 1u))
    return 0;
  *out = ((bytes + PO33_POOL_ALLOCATION_BLOCK_BYTES - 1u) / PO33_POOL_ALLOCATION_BLOCK_BYTES) *
         PO33_POOL_ALLOCATION_BLOCK_BYTES;
  return 1;
}

static int capacity_selectors(const po_packet_t *p, void *user) {
  po33_capacity_scan_t *s = user;
  if (p->tag_code == PO33_TAG_SOUND_SELECT) {
    if (p->payload_len != 1u) {
      s->unsafe = 1;
      return 0;
    }
    s->active = 1;
    if (s->selectors[p->payload[0]])
      s->unsafe = 1;
    s->selectors[p->payload[0]] = 1;
  } else if (p->tag_code == PO33_TAG_END)
    s->active = 0;
  else if (s->active && p->tag_code != PO33_TAG_SOUND_DATA)
    s->unsafe = 1;
  return 0;
}

static po_status_t capacity_pool(const po33_topology_observed_record_t *r, void *user) {
  po33_capacity_scan_t *s = user;
  size_t bytes = r->data_bytes - PO33_SOUND_HEADER_BYTES, rounded;
  if (r->flags & PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION)
    return PO_ERR_PARSE;
  if (!bytes) {
    ++s->report.alias_count;
    return PO_OK;
  }
  if (!capacity_round(bytes, &rounded) || bytes > SIZE_MAX - s->report.serialized_sample_bytes ||
      rounded > SIZE_MAX - s->report.rounded_sample_bytes)
    return PO_ERR_RANGE;
  s->owners[r->selector] = 1;
  ++s->report.pool_count;
  s->report.unaligned_pool_count += bytes % PO33_POOL_ALLOCATION_BLOCK_BYTES != 0u;
  s->report.serialized_sample_bytes += bytes;
  s->report.rounded_sample_bytes += rounded;
  return PO_OK;
}

static po_status_t capacity_reference(const po33_topology_observed_record_t *r, void *user) {
  po33_capacity_scan_t *s = user;
  uint8_t source = r->header.source_selector;
  if (!s->owners[source])
    return PO_ERR_PARSE;
  if (s->references[source] < 2u && ++s->references[source] == 2u)
    ++s->report.shared_pool_count;
  return PO_OK;
}

po_status_t po33_capacity_measure(const uint8_t *frame, size_t frame_len,
                                  po33_capacity_report_t *out) {
  po33_capacity_scan_t s;
  po33_decode_result_t parsed;
  po_status_t status;
  if (frame == NULL || out == NULL || po33_ranges_overlap(frame, frame_len, out, sizeof(*out)))
    return PO_ERR_INVALID_ARG;
  s.report = (po33_capacity_report_t){.frame_bytes = frame_len};
  s.active = 0;
  s.unsafe = 0;
  for (size_t i = 0; i <= UINT8_MAX; ++i) {
    s.selectors[i] = 0;
    s.owners[i] = 0;
    s.references[i] = 0;
  }
  status = po33_frame_parse(frame, frame_len, capacity_selectors, &s, &parsed);
  if (status != PO_OK || !parsed.done || parsed.stopped || parsed.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  /* Validate records before classifying duplicate/extended groups. */
  status = topology_parse_exact_records(frame, frame_len, capacity_pool, &s);
  if (status != PO_OK)
    return status;
  if (s.unsafe)
    return PO_ERR_PARSE;
  status = topology_parse_exact_records(frame, frame_len, capacity_reference, &s);
  if (status != PO_OK)
    return status;
  *out = s.report;
  return PO_OK;
}

po_status_t po33_capacity_project_pool_change(const po33_capacity_report_t *report,
                                              size_t old_pool_bytes, size_t new_pool_bytes,
                                              size_t limit_bytes, po33_capacity_budget_t *out) {
  po33_capacity_budget_t value = {0};
  size_t retained;
  if (report == NULL || out == NULL ||
      po33_ranges_overlap(report, sizeof(*report), out, sizeof(*out)))
    return PO_ERR_INVALID_ARG;
  if (report->rounded_sample_bytes % PO33_POOL_ALLOCATION_BLOCK_BYTES ||
      !capacity_round(old_pool_bytes, &value.removed_bytes) ||
      !capacity_round(new_pool_bytes, &value.added_bytes) ||
      value.removed_bytes > report->rounded_sample_bytes)
    return PO_ERR_RANGE;
  retained = report->rounded_sample_bytes - value.removed_bytes;
  if (value.added_bytes > SIZE_MAX - retained)
    return PO_ERR_RANGE;
  value.projected_bytes = retained + value.added_bytes;
  value.limit_bytes = limit_bytes;
  value.fits = value.projected_bytes <= limit_bytes;
  if (value.fits)
    value.remaining_bytes = limit_bytes - value.projected_bytes;
  else
    value.excess_bytes = value.projected_bytes - limit_bytes;
  *out = value;
  return PO_OK;
}

static void pool_edit_measure_init(po33_pool_edit_measure_t *measurement,
                                   size_t target_sound_record_index) {
  measurement->target_sound_record_index = target_sound_record_index;
  measurement->sound_record_count = 0u;
  measurement->pool_count = 0u;
  measurement->target_pool_index = SIZE_MAX;
  measurement->target = (po33_topology_observed_record_t){0};
  measurement->target_found = 0;
  for (size_t index = 0u; index <= UINT8_MAX; ++index) {
    measurement->selector_count[index] = 0u;
    measurement->owner_count[index] = 0u;
    measurement->owner_pool_index[index] = SIZE_MAX;
  }
}

static po_status_t pool_edit_measure_record(const po33_topology_observed_record_t *record,
                                            void *user) {
  po33_pool_edit_measure_t *measurement = (po33_pool_edit_measure_t *)user;
  size_t record_index = measurement->sound_record_count;
  int owns_pool = record->data_bytes > PO33_SOUND_HEADER_BYTES;

  if (record_index == SIZE_MAX)
    return PO_ERR_RANGE;
  if (measurement->selector_count[record->selector] < 2u)
    ++measurement->selector_count[record->selector];
  if (owns_pool) {
    if (measurement->pool_count == SIZE_MAX)
      return PO_ERR_RANGE;
    if (measurement->owner_count[record->selector] == 0u)
      measurement->owner_pool_index[record->selector] = measurement->pool_count;
    if (measurement->owner_count[record->selector] < 2u)
      ++measurement->owner_count[record->selector];
  }
  if (record_index == measurement->target_sound_record_index) {
    measurement->target = *record;
    measurement->target_found = 1;
    if (owns_pool)
      measurement->target_pool_index = measurement->pool_count;
  }
  ++measurement->sound_record_count;
  if (owns_pool)
    ++measurement->pool_count;
  return PO_OK;
}

static unsigned int pool_edit_record_flags(const po33_pool_edit_measure_t *measurement,
                                           const po33_topology_observed_record_t *record) {
  unsigned int flags = record->flags;
  if (measurement->selector_count[record->selector] > 1u)
    flags |= PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR;
  if (measurement->owner_count[record->header.source_selector] == 0u)
    flags |= PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED;
  else if (measurement->owner_count[record->header.source_selector] > 1u)
    flags |= PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS;
  return flags;
}

static po_status_t pool_edit_resolve_record(const po33_topology_observed_record_t *record,
                                            void *user) {
  po33_pool_edit_resolution_t *resolution = (po33_pool_edit_resolution_t *)user;
  const po33_pool_edit_measure_t *measurement = resolution->measurement;
  size_t referenced_pool_index = SIZE_MAX;
  unsigned int flags;

  if (resolution->sound_record_index >= measurement->sound_record_count)
    return PO_ERR_FRAME;
  flags = pool_edit_record_flags(measurement, record);
  if (measurement->owner_count[record->header.source_selector] == 1u)
    referenced_pool_index = measurement->owner_pool_index[record->header.source_selector];
  if (referenced_pool_index == measurement->target_pool_index) {
    if (resolution->reference_count == SIZE_MAX ||
        (resolution->sound_record_index != measurement->target_sound_record_index &&
         resolution->affected_sound_record_count == SIZE_MAX))
      return PO_ERR_RANGE;
    ++resolution->reference_count;
    if (resolution->sound_record_index != measurement->target_sound_record_index)
      ++resolution->affected_sound_record_count;
  }
  if (resolution->topology_reason == PO33_POOL_REPLACEMENT_REASON_NONE &&
      record->header.source_selector == measurement->target.selector &&
      referenced_pool_index != measurement->target_pool_index)
    resolution->topology_reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
  if (resolution->topology_reason == PO33_POOL_REPLACEMENT_REASON_NONE &&
      referenced_pool_index == measurement->target_pool_index &&
      (record->header.source_selector != measurement->target.selector ||
       (flags &
        (PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR | PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED |
         PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS)) != 0u))
    resolution->topology_reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
  if (record->data_bytes > PO33_SOUND_HEADER_BYTES) {
    if (resolution->pool_index == SIZE_MAX)
      return PO_ERR_RANGE;
    ++resolution->pool_index;
  }
  ++resolution->sound_record_index;
  return PO_OK;
}

static int pool_edit_target_boundary_valid(const po33_topology_observed_record_t *target) {
  size_t selection_end;
  if (target->selection_span.length == 0u || target->data_span.length == 0u ||
      target->selection_span.offset > SIZE_MAX - target->selection_span.length ||
      target->data_span.offset > SIZE_MAX - target->data_span.length ||
      target->data_bytes <= PO33_SOUND_HEADER_BYTES ||
      target->data_bytes > SIZE_MAX - PO_WIRE_OVERHEAD_BYTES ||
      target->data_span.length < target->data_bytes + PO_WIRE_OVERHEAD_BYTES)
    return 0;
  selection_end = target->selection_span.offset + target->selection_span.length;
  return selection_end <= target->data_span.offset;
}

static void pool_edit_plan_clear(po33_pool_edit_plan_t *plan) {
  *plan = (po33_pool_edit_plan_t){
      .target_sound_record_index = SIZE_MAX,
      .replacement =
          {
              .target_pool_index = SIZE_MAX,
              .reason = PO33_POOL_REPLACEMENT_REASON_NONE,
          },
      .target_selector = UINT8_MAX,
  };
}

static int pool_edit_plan_overlaps_input(const po33_pool_edit_plan_t *plan, const uint8_t *frame,
                                         size_t frame_len, const uint8_t *samples,
                                         size_t sample_bytes) {
  return (frame != NULL && po33_ranges_overlap(plan, sizeof(*plan), frame, frame_len)) ||
         (samples != NULL && po33_ranges_overlap(plan, sizeof(*plan), samples, sample_bytes));
}

static po_status_t pool_edit_plan_internal(const uint8_t *frame, size_t frame_len,
                                           size_t target_sound_record_index, const uint8_t *samples,
                                           size_t sample_bytes, int full_range,
                                           po33_pool_edit_plan_t *plan) {
  po33_pool_edit_measure_t measurement;
  po33_pool_edit_resolution_t resolution;
  po33_pool_replacement_plan_t replacement = {
      .target_pool_index = SIZE_MAX,
      .reason = PO33_POOL_REPLACEMENT_REASON_NONE,
  };
  unsigned int target_flags;
  size_t blocks;
  size_t storage_bytes = sample_bytes;
  po_status_t status;

  if (plan == NULL)
    return PO_ERR_INVALID_ARG;
  if (pool_edit_plan_overlaps_input(plan, frame, frame_len, samples, sample_bytes))
    return PO_ERR_INVALID_ARG;
  pool_edit_plan_clear(plan);
  if (frame == NULL || (sample_bytes != 0u && samples == NULL))
    return PO_ERR_INVALID_ARG;
  replacement.requested_sample_bytes = sample_bytes;
  if (full_range) {
    if (sample_bytes == 0u || sample_bytes > UINT32_MAX || sample_bytes == SIZE_MAX)
      return PO_ERR_RANGE;
    storage_bytes = sample_bytes + 1u;
  }
  pool_edit_measure_init(&measurement, target_sound_record_index);
  status = topology_parse_exact_records(frame, frame_len, pool_edit_measure_record, &measurement);
  if (status != PO_OK)
    return status;
  if (!measurement.target_found) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_TARGET_RECORD_OUT_OF_RANGE;
    status = PO_ERR_RANGE;
    goto failure;
  }
  if ((measurement.target.flags & PO33_SOUND_RECORD_FLAG_HEADER_ONLY) != 0u ||
      measurement.target_pool_index == SIZE_MAX) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_TARGET_NOT_OWNED;
    status = PO_ERR_RANGE;
    goto failure;
  }
  replacement.target_pool_index = measurement.target_pool_index;
  replacement.affected_sound_record_count = 1u;
  resolution = (po33_pool_edit_resolution_t){
      .measurement = &measurement,
      .affected_sound_record_count = 1u,
  };
  status = topology_parse_exact_records(frame, frame_len, pool_edit_resolve_record, &resolution);
  if (status != PO_OK)
    return status;
  if (resolution.sound_record_index != measurement.sound_record_count ||
      resolution.pool_index != measurement.pool_count)
    return PO_ERR_FRAME;
  replacement.reference_count = resolution.reference_count;
  replacement.affected_sound_record_count = resolution.affected_sound_record_count;
  if (!pool_edit_target_boundary_valid(&measurement.target)) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_GROUP_BOUNDARY;
    status = PO_ERR_PARSE;
    goto failure;
  }
  replacement.existing_allocation_bytes = measurement.target.data_bytes - PO33_SOUND_HEADER_BYTES;
  replacement.existing_allocation_fits = storage_bytes <= replacement.existing_allocation_bytes;
  replacement.growth_required = !replacement.existing_allocation_fits;
  if (replacement.existing_allocation_fits) {
    replacement.required_allocation_bytes = replacement.existing_allocation_bytes;
  } else {
    if (storage_bytes > SIZE_MAX - (PO33_POOL_ALLOCATION_BLOCK_BYTES - 1u)) {
      replacement.reason = PO33_POOL_REPLACEMENT_REASON_REQUIRED_ALLOCATION_OVERFLOW;
      status = PO_ERR_RANGE;
      goto failure;
    }
    replacement.required_allocation_bytes =
        ((storage_bytes + PO33_POOL_ALLOCATION_BLOCK_BYTES - 1u) /
         PO33_POOL_ALLOCATION_BLOCK_BYTES) *
        PO33_POOL_ALLOCATION_BLOCK_BYTES;
    replacement.required_growth_bytes =
        replacement.required_allocation_bytes - replacement.existing_allocation_bytes;
  }
  target_flags = pool_edit_record_flags(&measurement, &measurement.target);
  if ((target_flags & PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR) != 0u) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_DUPLICATE_TARGET_SELECTOR;
    status = PO_ERR_PARSE;
    goto failure;
  }
  if ((target_flags & PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS) != 0u) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
    status = PO_ERR_PARSE;
    goto failure;
  }
  if ((target_flags & PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION) != 0u) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_UNKNOWN_TARGET_EXTENSION;
    status = PO_ERR_PARSE;
    goto failure;
  }
  if (resolution.topology_reason != PO33_POOL_REPLACEMENT_REASON_NONE) {
    replacement.reason = resolution.topology_reason;
    status = PO_ERR_PARSE;
    goto failure;
  }
  if (full_range && (resolution.reference_count != 1u ||
                     measurement.target.header.source_selector != measurement.target.selector)) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_NOT_EXCLUSIVE;
    status = PO_ERR_PARSE;
    goto failure;
  }
  if (replacement.existing_allocation_bytes % PO33_POOL_ALLOCATION_BLOCK_BYTES != 0u) {
    if (replacement.growth_required) {
      replacement.reason = PO33_POOL_REPLACEMENT_REASON_EXISTING_ALLOCATION_UNALIGNED;
      status = PO_ERR_RANGE;
      goto failure;
    }
  } else {
    replacement.growth_structurally_eligible = 1;
  }
  blocks = replacement.required_growth_bytes / PO33_POOL_ALLOCATION_BLOCK_BYTES;
  if (blocks >
      (SIZE_MAX - frame_len) / (PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO_WIRE_OVERHEAD_BYTES)) {
    replacement.reason = PO33_POOL_REPLACEMENT_REASON_REQUIRED_ALLOCATION_OVERFLOW;
    status = PO_ERR_RANGE;
    goto failure;
  }
  plan->required_frame_bytes =
      frame_len + blocks * (PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO_WIRE_OVERHEAD_BYTES);
  plan->required_workspace_bytes = plan->required_frame_bytes;
  plan->target_sound_record_index = target_sound_record_index;
  plan->target_data_span = measurement.target.data_span;
  plan->replacement = replacement;
  plan->target_selector = measurement.target.selector;
  return PO_OK;

failure:
  plan->replacement = replacement;
  return status;
}

typedef struct po33_pool_edit_range {
  const void *data;
  size_t bytes;
} po33_pool_edit_range_t;

typedef struct po33_pool_edit_rebuild {
  po33_builder_t *builder;
  const po33_pool_edit_plan_t *plan;
  const uint8_t *samples;
  size_t sample_bytes;
  size_t target_data_end;
  size_t target_logical_bytes;
  int target_finished;
  int full_range;
  po_status_t status;
} po33_pool_edit_rebuild_t;

static int pool_edit_replacement_matches(const po33_pool_replacement_plan_t *left,
                                         const po33_pool_replacement_plan_t *right) {
  return left->target_pool_index == right->target_pool_index &&
         left->requested_sample_bytes == right->requested_sample_bytes &&
         left->existing_allocation_bytes == right->existing_allocation_bytes &&
         left->required_allocation_bytes == right->required_allocation_bytes &&
         left->required_growth_bytes == right->required_growth_bytes &&
         left->reference_count == right->reference_count &&
         left->affected_sound_record_count == right->affected_sound_record_count &&
         left->existing_allocation_fits == right->existing_allocation_fits &&
         left->growth_required == right->growth_required &&
         left->growth_structurally_eligible == right->growth_structurally_eligible &&
         left->reason == right->reason;
}

static int pool_edit_plan_matches(const po33_pool_edit_plan_t *left,
                                  const po33_pool_edit_plan_t *right) {
  return left->required_workspace_bytes == right->required_workspace_bytes &&
         left->required_frame_bytes == right->required_frame_bytes &&
         left->target_sound_record_index == right->target_sound_record_index &&
         left->target_data_span.offset == right->target_data_span.offset &&
         left->target_data_span.length == right->target_data_span.length &&
         left->target_selector == right->target_selector &&
         pool_edit_replacement_matches(&left->replacement, &right->replacement);
}

static int edit_rebuild_has_invalid_aliases(const uint8_t *frame, size_t frame_len,
                                            const uint8_t *samples, size_t sample_bytes,
                                            const void *plan, size_t plan_bytes, uint8_t *workspace,
                                            size_t workspace_bytes, uint8_t *out_frame,
                                            size_t out_capacity, size_t *out_frame_bytes) {
  const po33_pool_edit_range_t storage[] = {
      {plan, plan == NULL ? 0u : plan_bytes},
      {workspace, workspace == NULL ? 0u : workspace_bytes},
      {out_frame, out_frame == NULL ? 0u : out_capacity},
      {out_frame_bytes, sizeof(*out_frame_bytes)},
  };

  for (size_t left = 0u; left < sizeof(storage) / sizeof(storage[0]); ++left) {
    for (size_t right = 0u; right < left; ++right)
      if (po33_ranges_overlap(storage[left].data, storage[left].bytes, storage[right].data,
                              storage[right].bytes))
        return 1;
    if ((frame != NULL &&
         po33_ranges_overlap(storage[left].data, storage[left].bytes, frame, frame_len)) ||
        (samples != NULL &&
         po33_ranges_overlap(storage[left].data, storage[left].bytes, samples, sample_bytes)))
      return 1;
  }
  return 0;
}

static int pool_edit_rebuild_has_invalid_aliases(const uint8_t *frame, size_t frame_len,
                                                 const uint8_t *samples, size_t sample_bytes,
                                                 const po33_pool_edit_plan_t *plan,
                                                 uint8_t *workspace, size_t workspace_bytes,
                                                 uint8_t *out_frame, size_t out_capacity,
                                                 size_t *out_frame_bytes) {
  return edit_rebuild_has_invalid_aliases(frame, frame_len, samples, sample_bytes, plan,
                                          sizeof(*plan), workspace, workspace_bytes, out_frame,
                                          out_capacity, out_frame_bytes);
}

static int pool_edit_rebuild_can_clear_output_length(const uint8_t *frame, size_t frame_len,
                                                     const uint8_t *samples, size_t sample_bytes,
                                                     const po33_pool_edit_plan_t *plan,
                                                     uint8_t *workspace, size_t workspace_bytes,
                                                     uint8_t *out_frame, size_t out_capacity,
                                                     size_t *out_frame_bytes) {
  return out_frame_bytes != NULL && !pool_edit_rebuild_has_invalid_aliases(
                                        frame, frame_len, samples, sample_bytes, plan, workspace,
                                        workspace_bytes, out_frame, out_capacity, out_frame_bytes);
}

static void pool_edit_copy_bytes(uint8_t *out, const uint8_t *input, size_t bytes) {
  for (size_t index = 0u; index < bytes; ++index)
    out[index] = input[index];
}

static po_status_t pool_edit_rebuild_append_growth(po33_pool_edit_rebuild_t *rebuild) {
  const po33_pool_replacement_plan_t *replacement = &rebuild->plan->replacement;
  uint8_t payload[PO_MAX_PAYLOAD];
  size_t block_offset = 0u;

  while (block_offset < replacement->required_growth_bytes) {
    size_t within_block = 0u;
    while (within_block < PO33_POOL_ALLOCATION_BLOCK_BYTES) {
      size_t payload_bytes = PO33_POOL_ALLOCATION_BLOCK_BYTES - within_block;
      size_t sample_offset;
      po_status_t status;
      if (payload_bytes > sizeof(payload))
        payload_bytes = sizeof(payload);
      sample_offset = replacement->existing_allocation_bytes + block_offset + within_block;
      for (size_t index = 0u; index < payload_bytes; ++index) {
        size_t sample_index = sample_offset + index;
        payload[index] = sample_index < rebuild->sample_bytes ? rebuild->samples[sample_index] : 0u;
      }
      status = po33_builder_append_packet(rebuild->builder, PO33_TAG_SOUND_DATA, payload,
                                          payload_bytes, NULL);
      if (status != PO_OK)
        return status;
      within_block += payload_bytes;
    }
    block_offset += PO33_POOL_ALLOCATION_BLOCK_BYTES;
  }
  return PO_OK;
}

static int pool_edit_rebuild_packet(const po_packet_t *packet, void *user) {
  po33_pool_edit_rebuild_t *rebuild = (po33_pool_edit_rebuild_t *)user;
  const po33_pool_edit_plan_t *plan = rebuild->plan;
  const po33_pool_replacement_plan_t *replacement = &plan->replacement;
  po_status_t status;
  size_t packet_end;

  if (packet->tag_code == PO33_TAG_BEGIN || packet->tag_code == PO33_TAG_END)
    return 0;
  if (packet->offset > SIZE_MAX - (PO_WIRE_OVERHEAD_BYTES + packet->payload_len)) {
    rebuild->status = PO_ERR_FRAME;
    return 1;
  }
  packet_end = packet->offset + PO_WIRE_OVERHEAD_BYTES + packet->payload_len;
  if (packet->offset >= plan->target_data_span.offset &&
      packet->offset < rebuild->target_data_end && packet->tag_code != PO33_TAG_SOUND_DATA) {
    rebuild->status = PO_ERR_FRAME;
    return 1;
  }
  if (packet->tag_code == PO33_TAG_SOUND_DATA && packet->offset >= plan->target_data_span.offset &&
      packet->offset < rebuild->target_data_end) {
    uint8_t payload[PO_MAX_PAYLOAD];
    size_t total_data_bytes = PO33_SOUND_HEADER_BYTES + replacement->existing_allocation_bytes;
    if (packet_end > rebuild->target_data_end ||
        packet->payload_len > total_data_bytes - rebuild->target_logical_bytes) {
      rebuild->status = PO_ERR_FRAME;
      return 1;
    }
    for (size_t index = 0u; index < packet->payload_len; ++index) {
      size_t logical_index = rebuild->target_logical_bytes + index;
      if (rebuild->full_range && logical_index < 8u) {
        uint32_t value = logical_index < 4u ? 0u : (uint32_t)rebuild->sample_bytes;
        payload[index] = (uint8_t)(value >> (8u * (logical_index % 4u)));
      } else if (logical_index < PO33_SOUND_HEADER_BYTES)
        payload[index] = packet->payload[index];
      else {
        size_t sample_index = logical_index - PO33_SOUND_HEADER_BYTES;
        payload[index] = sample_index < rebuild->sample_bytes ? rebuild->samples[sample_index] : 0u;
      }
    }
    rebuild->target_logical_bytes += packet->payload_len;
    status = po33_builder_append_packet(rebuild->builder, packet->tag_code, payload,
                                        packet->payload_len, NULL);
    if (status == PO_OK && packet_end == rebuild->target_data_end) {
      status = pool_edit_rebuild_append_growth(rebuild);
      rebuild->target_finished = status == PO_OK;
    }
  } else {
    status = po33_builder_append_packet(rebuild->builder, packet->tag_code, packet->payload,
                                        packet->payload_len, NULL);
  }
  if (status != PO_OK) {
    rebuild->status = status;
    return 1;
  }
  return 0;
}

static po_status_t pool_edit_rebuild_internal(const uint8_t *frame, size_t frame_len,
                                              size_t target_sound_record_index,
                                              const uint8_t *samples, size_t sample_bytes,
                                              const po33_pool_edit_plan_t *plan, uint8_t *workspace,
                                              size_t workspace_bytes, uint8_t *out_frame,
                                              size_t out_capacity, size_t *out_frame_bytes,
                                              int full_range) {
  po33_pool_edit_plan_t expected = {0};
  po33_pool_edit_rebuild_t rebuild;
  po33_decode_result_t result;
  po33_builder_t builder;
  size_t frame_bytes;
  po_status_t status;

  if (pool_edit_rebuild_can_clear_output_length(frame, frame_len, samples, sample_bytes, plan,
                                                workspace, workspace_bytes, out_frame, out_capacity,
                                                out_frame_bytes))
    *out_frame_bytes = 0u;
  if (plan == NULL || workspace == NULL || out_frame == NULL || out_frame_bytes == NULL)
    return PO_ERR_INVALID_ARG;
  if (pool_edit_rebuild_has_invalid_aliases(frame, frame_len, samples, sample_bytes, plan,
                                            workspace, workspace_bytes, out_frame, out_capacity,
                                            out_frame_bytes))
    return PO_ERR_INVALID_ARG;
  if (frame == NULL || (sample_bytes != 0u && samples == NULL))
    return PO_ERR_INVALID_ARG;
  status = pool_edit_plan_internal(frame, frame_len, target_sound_record_index, samples,
                                   sample_bytes, full_range, &expected);
  if (status != PO_OK || !pool_edit_plan_matches(plan, &expected))
    return PO_ERR_INVALID_ARG;
  if (workspace_bytes < expected.required_workspace_bytes ||
      out_capacity < expected.required_frame_bytes)
    return PO_ERR_BUFFER_TOO_SMALL;
  if (expected.target_data_span.offset > SIZE_MAX - expected.target_data_span.length)
    return PO_ERR_FRAME;
  status = po33_builder_init(&builder, workspace, workspace_bytes);
  if (status != PO_OK)
    return status;
  rebuild = (po33_pool_edit_rebuild_t){
      .builder = &builder,
      .plan = &expected,
      .samples = samples,
      .sample_bytes = sample_bytes,
      .full_range = full_range,
      .target_data_end = expected.target_data_span.offset + expected.target_data_span.length,
      .status = PO_OK,
  };
  status = po33_frame_parse(frame, frame_len, pool_edit_rebuild_packet, &rebuild, &result);
  if (rebuild.status != PO_OK)
    return rebuild.status;
  if (status != PO_OK || !result.done || result.stopped || result.frame_bytes != frame_len ||
      !rebuild.target_finished ||
      rebuild.target_logical_bytes !=
          PO33_SOUND_HEADER_BYTES + expected.replacement.existing_allocation_bytes)
    return PO_ERR_FRAME;
  status = po33_builder_finish(&builder, &frame_bytes);
  if (status != PO_OK)
    return status;
  if (frame_bytes != expected.required_frame_bytes)
    return PO_ERR_FRAME;
  pool_edit_copy_bytes(out_frame, workspace, frame_bytes);
  *out_frame_bytes = frame_bytes;
  return PO_OK;
}

po_status_t po33_pool_edit_plan(const uint8_t *frame, size_t frame_len, size_t target,
                                const uint8_t *samples, size_t bytes, po33_pool_edit_plan_t *plan) {
  return pool_edit_plan_internal(frame, frame_len, target, samples, bytes, 0, plan);
}

po_status_t po33_sample_edit_plan(const uint8_t *frame, size_t frame_len, size_t target,
                                  const uint8_t *samples, size_t bytes,
                                  po33_sample_edit_plan_t *plan) {
  po_status_t status;
  if (plan == NULL ||
      (frame != NULL && po33_ranges_overlap(plan, sizeof(*plan), frame, frame_len)) ||
      (samples != NULL && po33_ranges_overlap(plan, sizeof(*plan), samples, bytes)))
    return PO_ERR_INVALID_ARG;
  *plan = (po33_sample_edit_plan_t){0};
  status = pool_edit_plan_internal(frame, frame_len, target, samples, bytes, 1, &plan->pool);
  if (status == PO_OK)
    plan->policy = 1u;
  return status;
}

po_status_t po33_pool_edit_rebuild(const uint8_t *frame, size_t frame_len, size_t target,
                                   const uint8_t *samples, size_t bytes,
                                   const po33_pool_edit_plan_t *plan, uint8_t *workspace,
                                   size_t workspace_bytes, uint8_t *out, size_t capacity,
                                   size_t *written) {
  return pool_edit_rebuild_internal(frame, frame_len, target, samples, bytes, plan, workspace,
                                    workspace_bytes, out, capacity, written, 0);
}

po_status_t po33_sample_edit_rebuild(const uint8_t *frame, size_t frame_len, size_t target,
                                     const uint8_t *samples, size_t bytes,
                                     const po33_sample_edit_plan_t *plan, uint8_t *workspace,
                                     size_t workspace_bytes, uint8_t *out, size_t capacity,
                                     size_t *written) {
  int invalid_aliases =
      edit_rebuild_has_invalid_aliases(frame, frame_len, samples, bytes, plan, sizeof(*plan),
                                       workspace, workspace_bytes, out, capacity, written);
  if (written != NULL && !invalid_aliases)
    *written = 0u;
  if (plan == NULL || invalid_aliases || plan->policy != 1u)
    return PO_ERR_INVALID_ARG;
  return pool_edit_rebuild_internal(frame, frame_len, target, samples, bytes, &plan->pool,
                                    workspace, workspace_bytes, out, capacity, written, 1);
}

typedef struct po33_insert_scan {
  uint8_t slot;
  unsigned seen;
  int active, occupied, extension, referenced;
  size_t offset;
} po33_insert_scan_t;

static po_status_t insert_check_reference(const po33_topology_observed_record_t *record,
                                          void *user) {
  po33_insert_scan_t *scan = user;
  if (record->header.source_selector == scan->slot)
    scan->referenced = 1;
  return PO_OK;
}

static int insert_scan_packet(const po_packet_t *packet, void *user) {
  po33_insert_scan_t *scan = user;
  if (packet->tag_code == PO33_TAG_SOUND_SELECT) {
    scan->active = packet->payload[0] == scan->slot;
    if (scan->active) {
      if (scan->seen < 2u)
        ++scan->seen;
      scan->offset = packet->offset;
    }
  } else if (scan->active && packet->tag_code != PO33_TAG_END) {
    if (packet->tag_code == PO33_TAG_SOUND_DATA)
      scan->occupied = 1;
    else
      scan->extension = 1;
  }
  return 0;
}

po_status_t po33_sample_insert_plan(const uint8_t *frame, size_t frame_len, uint8_t slot,
                                    const uint8_t *samples, size_t sample_bytes,
                                    po33_sample_insert_plan_t *plan) {
  po33_insert_scan_t scan = {.slot = slot};
  po33_decode_result_t result;
  po_status_t status;
  size_t blocks;
  if (plan == NULL ||
      (frame != NULL && po33_ranges_overlap(plan, sizeof(*plan), frame, frame_len)) ||
      (samples != NULL && po33_ranges_overlap(plan, sizeof(*plan), samples, sample_bytes)))
    return PO_ERR_INVALID_ARG;
  *plan = (po33_sample_insert_plan_t){.selector_offset = SIZE_MAX, .slot = UINT8_MAX};
  if (frame == NULL || samples == NULL)
    return PO_ERR_INVALID_ARG;
  if (slot >= 8u) {
    plan->reason = PO33_SAMPLE_INSERT_REASON_SLOT_OUT_OF_RANGE;
    return PO_ERR_RANGE;
  }
  if (sample_bytes == 0u || sample_bytes > UINT32_MAX ||
      sample_bytes > SIZE_MAX - PO33_POOL_ALLOCATION_BLOCK_BYTES)
    return PO_ERR_RANGE;
  status = topology_parse_exact_records(frame, frame_len, insert_check_reference, &scan);
  if (status != PO_OK)
    return status;
  status = po33_frame_parse(frame, frame_len, insert_scan_packet, &scan, &result);
  if (status != PO_OK || !result.done || result.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  if (scan.seen == 0u) {
    plan->reason = PO33_SAMPLE_INSERT_REASON_MISSING_SELECTOR;
    return PO_ERR_RANGE;
  }
  if (scan.seen > 1u)
    plan->reason = PO33_SAMPLE_INSERT_REASON_DUPLICATE_SELECTOR;
  else if (scan.occupied)
    plan->reason = PO33_SAMPLE_INSERT_REASON_OCCUPIED;
  else if (scan.extension)
    plan->reason = PO33_SAMPLE_INSERT_REASON_UNKNOWN_EXTENSION;
  else if (scan.referenced)
    plan->reason = PO33_SAMPLE_INSERT_REASON_REFERENCED;
  if (plan->reason != PO33_SAMPLE_INSERT_REASON_NONE)
    return PO_ERR_PARSE;
  blocks = (sample_bytes + PO33_POOL_ALLOCATION_BLOCK_BYTES) / PO33_POOL_ALLOCATION_BLOCK_BYTES;
  if (frame_len > SIZE_MAX - PO33_SOUND_HEADER_BYTES ||
      blocks > (SIZE_MAX - frame_len - PO33_SOUND_HEADER_BYTES) /
                   (PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO_WIRE_OVERHEAD_BYTES))
    return PO_ERR_RANGE;
  plan->required_frame_bytes =
      frame_len + PO33_SOUND_HEADER_BYTES +
      blocks * (PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO_WIRE_OVERHEAD_BYTES);
  plan->required_workspace_bytes = plan->required_frame_bytes;
  plan->sample_bytes = sample_bytes;
  plan->allocation_bytes = blocks * PO33_POOL_ALLOCATION_BLOCK_BYTES;
  plan->selector_offset = scan.offset;
  plan->slot = slot;
  return PO_OK;
}

typedef struct po33_insert_rebuild {
  po33_builder_t *builder;
  const po33_sample_insert_plan_t *plan;
  const uint8_t *samples;
  po_status_t status;
  int inserted;
} po33_insert_rebuild_t;

static int insert_rebuild_packet(const po_packet_t *packet, void *user) {
  po33_insert_rebuild_t *r = user;
  const po33_sample_insert_plan_t *p = r->plan;
  if (packet->tag_code == PO33_TAG_BEGIN || packet->tag_code == PO33_TAG_END)
    return 0;
  r->status = po33_builder_append_packet(r->builder, packet->tag_code, packet->payload,
                                         packet->payload_len, NULL);
  if (r->status != PO_OK)
    return 1;
  if (packet->offset == p->selector_offset) {
    uint8_t header[PO33_SOUND_HEADER_BYTES], data[PO_MAX_PAYLOAD];
    po33_sound_header_t value = {0u, (uint32_t)p->sample_bytes, p->slot};
    size_t position = 0u;
    r->status = po33_sound_header_encode(&value, header, sizeof(header));
    if (r->status != PO_OK)
      return 1;
    for (size_t block = 0u; block < p->allocation_bytes;
         block += PO33_POOL_ALLOCATION_BLOCK_BYTES) {
      size_t end = PO33_SOUND_HEADER_BYTES + block + PO33_POOL_ALLOCATION_BLOCK_BYTES;
      while (position < end) {
        size_t count = end - position;
        if (count > sizeof(data))
          count = sizeof(data);
        for (size_t i = 0u; i < count; ++i) {
          size_t index = position + i;
          data[i] = index < PO33_SOUND_HEADER_BYTES
                        ? header[index]
                        : (index - PO33_SOUND_HEADER_BYTES < p->sample_bytes
                               ? r->samples[index - PO33_SOUND_HEADER_BYTES]
                               : 0u);
        }
        r->status = po33_builder_append_packet(r->builder, PO33_TAG_SOUND_DATA, data, count, NULL);
        if (r->status != PO_OK)
          return 1;
        position += count;
      }
    }
    r->inserted = 1;
  }
  return 0;
}

po_status_t po33_sample_insert_rebuild(const uint8_t *frame, size_t frame_len, uint8_t slot,
                                       const uint8_t *samples, size_t sample_bytes,
                                       const po33_sample_insert_plan_t *plan, uint8_t *workspace,
                                       size_t workspace_bytes, uint8_t *out, size_t capacity,
                                       size_t *written) {
  po33_sample_insert_plan_t expected = {0};
  po33_builder_t builder;
  po33_decode_result_t parsed;
  po33_insert_rebuild_t rebuild;
  size_t length;
  po_status_t status;
  int aliases =
      edit_rebuild_has_invalid_aliases(frame, frame_len, samples, sample_bytes, plan, sizeof(*plan),
                                       workspace, workspace_bytes, out, capacity, written);
  if (written != NULL && !aliases)
    *written = 0u;
  if (plan == NULL || workspace == NULL || out == NULL || written == NULL || aliases)
    return PO_ERR_INVALID_ARG;
  status = po33_sample_insert_plan(frame, frame_len, slot, samples, sample_bytes, &expected);
  if (status != PO_OK || plan->required_workspace_bytes != expected.required_workspace_bytes ||
      plan->required_frame_bytes != expected.required_frame_bytes ||
      plan->sample_bytes != expected.sample_bytes ||
      plan->allocation_bytes != expected.allocation_bytes ||
      plan->selector_offset != expected.selector_offset || plan->slot != expected.slot ||
      plan->reason != expected.reason)
    return PO_ERR_INVALID_ARG;
  if (workspace_bytes < expected.required_workspace_bytes ||
      capacity < expected.required_frame_bytes)
    return PO_ERR_BUFFER_TOO_SMALL;
  status = po33_builder_init(&builder, workspace, workspace_bytes);
  if (status != PO_OK)
    return status;
  rebuild = (po33_insert_rebuild_t){
      .builder = &builder, .plan = &expected, .samples = samples, .status = PO_OK};
  status = po33_frame_parse(frame, frame_len, insert_rebuild_packet, &rebuild, &parsed);
  if (rebuild.status != PO_OK)
    return rebuild.status;
  if (status != PO_OK || !parsed.done || parsed.stopped || !rebuild.inserted ||
      parsed.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  status = po33_builder_finish(&builder, &length);
  if (status != PO_OK || length != expected.required_frame_bytes)
    return status == PO_OK ? PO_ERR_FRAME : status;
  pool_edit_copy_bytes(out, workspace, length);
  *written = length;
  return PO_OK;
}

typedef struct po33_detach_scan {
  uint8_t slot, selectors[256], owners[256];
  int found, referenced;
  po33_topology_observed_record_t target;
} po33_detach_scan_t;

static po_status_t detach_record(const po33_topology_observed_record_t *record, void *user) {
  po33_detach_scan_t *s = user;
  if (record->selector == s->slot) {
    s->target = *record;
    s->found = 1;
  } else if (record->header.source_selector == s->slot) {
    s->referenced = 1;
  }
  if (record->data_bytes > PO33_SOUND_HEADER_BYTES && s->owners[record->selector] < 2u)
    ++s->owners[record->selector];
  return PO_OK;
}

static int detach_selectors(const po_packet_t *packet, void *user) {
  po33_detach_scan_t *s = user;
  /* Selector payload lengths were validated by the topology pass. */
  if (packet->tag_code == PO33_TAG_SOUND_SELECT && s->selectors[packet->payload[0]] < 2u)
    ++s->selectors[packet->payload[0]];
  return 0;
}

po_status_t po33_sample_detach_plan(const uint8_t *frame, size_t frame_len, uint8_t slot,
                                    const uint8_t *samples, size_t sample_bytes,
                                    po33_sample_detach_plan_t *plan) {
  po33_detach_scan_t s;
  po33_decode_result_t parsed;
  po_status_t status;
  size_t blocks, base, added, size;
  uint8_t source;
  if (plan == NULL ||
      (frame != NULL && po33_ranges_overlap(plan, sizeof(*plan), frame, frame_len)) ||
      (samples != NULL && po33_ranges_overlap(plan, sizeof(*plan), samples, sample_bytes)))
    return PO_ERR_INVALID_ARG;
  *plan = (po33_sample_detach_plan_t){.slot = UINT8_MAX, .source_selector = UINT8_MAX};
  if (frame == NULL || samples == NULL)
    return PO_ERR_INVALID_ARG;
  if (slot >= 8u) {
    plan->reason = PO33_SAMPLE_DETACH_REASON_SLOT_OUT_OF_RANGE;
    return PO_ERR_RANGE;
  }
  if (!sample_bytes || sample_bytes > UINT32_MAX || sample_bytes > SIZE_MAX - 4096u)
    return PO_ERR_RANGE;
  s.slot = slot;
  s.found = 0;
  s.referenced = 0;
  s.target = (po33_topology_observed_record_t){0};
  for (size_t i = 0; i <= UINT8_MAX; ++i) {
    s.selectors[i] = 0;
    s.owners[i] = 0;
  }
  status = topology_parse_exact_records(frame, frame_len, detach_record, &s);
  if (status != PO_OK)
    return status;
  status = po33_frame_parse(frame, frame_len, detach_selectors, &s, &parsed);
  if (status != PO_OK || !parsed.done || parsed.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  if (!s.found) {
    plan->reason = PO33_SAMPLE_DETACH_REASON_MISSING_TARGET;
    return PO_ERR_RANGE;
  }
  source = s.target.header.source_selector;
  if (s.selectors[slot] != 1u)
    plan->reason = PO33_SAMPLE_DETACH_REASON_DUPLICATE_TARGET;
  else if (s.target.data_bytes != PO33_SOUND_HEADER_BYTES)
    plan->reason = PO33_SAMPLE_DETACH_REASON_NOT_ALIAS;
  else if (s.target.flags & PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION)
    plan->reason = PO33_SAMPLE_DETACH_REASON_UNKNOWN_EXTENSION;
  else if (!s.owners[source])
    plan->reason = PO33_SAMPLE_DETACH_REASON_SOURCE_UNRESOLVED;
  else if (s.owners[source] != 1u || s.selectors[source] != 1u)
    plan->reason = PO33_SAMPLE_DETACH_REASON_SOURCE_AMBIGUOUS;
  else if (s.referenced)
    plan->reason = PO33_SAMPLE_DETACH_REASON_REFERENCED_TARGET;
  if (plan->reason != PO33_SAMPLE_DETACH_REASON_NONE)
    return PO_ERR_PARSE;
  if (s.target.data_span.length > frame_len)
    return PO_ERR_FRAME;
  base = frame_len - s.target.data_span.length;
  blocks = (sample_bytes + PO33_POOL_ALLOCATION_BLOCK_BYTES) / PO33_POOL_ALLOCATION_BLOCK_BYTES;
  if (blocks > (SIZE_MAX - PO33_SOUND_HEADER_BYTES) /
                   (PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO_WIRE_OVERHEAD_BYTES))
    return PO_ERR_RANGE;
  added = PO33_SOUND_HEADER_BYTES +
          blocks * (PO33_POOL_ALLOCATION_BLOCK_BYTES + 17u * PO_WIRE_OVERHEAD_BYTES);
  if (base > SIZE_MAX - added)
    return PO_ERR_RANGE;
  size = base + added;
  if (base > SIZE_MAX - size)
    return PO_ERR_RANGE;
  *plan = (po33_sample_detach_plan_t){.required_workspace_bytes = base + size,
                                      .required_frame_bytes = size,
                                      .intermediate_frame_bytes = base,
                                      .sample_bytes = sample_bytes,
                                      .allocation_bytes = blocks * PO33_POOL_ALLOCATION_BLOCK_BYTES,
                                      .target_data_span = s.target.data_span,
                                      .slot = slot,
                                      .source_selector = source};
  return PO_OK;
}

typedef struct po33_detach_strip {
  po33_builder_t *builder;
  po33_frame_span_t span;
  po_status_t status;
} po33_detach_strip_t;

static int detach_strip_packet(const po_packet_t *p, void *user) {
  po33_detach_strip_t *s = user;
  if (p->tag_code == PO33_TAG_BEGIN || p->tag_code == PO33_TAG_END)
    return 0;
  if (p->offset >= s->span.offset && p->offset - s->span.offset < s->span.length) {
    if (p->tag_code != PO33_TAG_SOUND_DATA) {
      s->status = PO_ERR_FRAME;
      return 1;
    }
    return 0;
  }
  s->status = po33_builder_append_packet(s->builder, p->tag_code, p->payload, p->payload_len, NULL);
  return s->status != PO_OK;
}

po_status_t po33_sample_detach_rebuild(const uint8_t *frame, size_t frame_len, uint8_t slot,
                                       const uint8_t *samples, size_t sample_bytes,
                                       const po33_sample_detach_plan_t *plan, uint8_t *workspace,
                                       size_t workspace_bytes, uint8_t *out, size_t capacity,
                                       size_t *written) {
  po33_sample_detach_plan_t expected = {0};
  po33_sample_insert_plan_t insertion = {0};
  po33_builder_t builder;
  po33_decode_result_t parsed;
  po33_detach_strip_t strip;
  po_status_t status;
  size_t base;
  int aliases =
      edit_rebuild_has_invalid_aliases(frame, frame_len, samples, sample_bytes, plan, sizeof(*plan),
                                       workspace, workspace_bytes, out, capacity, written);
  if (written != NULL && !aliases)
    *written = 0;
  if (plan == NULL || workspace == NULL || out == NULL || written == NULL || aliases)
    return PO_ERR_INVALID_ARG;
  status = po33_sample_detach_plan(frame, frame_len, slot, samples, sample_bytes, &expected);
  if (status != PO_OK || plan->required_workspace_bytes != expected.required_workspace_bytes ||
      plan->required_frame_bytes != expected.required_frame_bytes ||
      plan->intermediate_frame_bytes != expected.intermediate_frame_bytes ||
      plan->sample_bytes != expected.sample_bytes ||
      plan->allocation_bytes != expected.allocation_bytes ||
      plan->target_data_span.offset != expected.target_data_span.offset ||
      plan->target_data_span.length != expected.target_data_span.length ||
      plan->slot != expected.slot || plan->source_selector != expected.source_selector ||
      plan->reason != expected.reason)
    return PO_ERR_INVALID_ARG;
  if (workspace_bytes < expected.required_workspace_bytes ||
      capacity < expected.required_frame_bytes)
    return PO_ERR_BUFFER_TOO_SMALL;
  status = po33_builder_init(&builder, workspace, expected.intermediate_frame_bytes);
  if (status != PO_OK)
    return status;
  strip = (po33_detach_strip_t){
      .builder = &builder, .span = expected.target_data_span, .status = PO_OK};
  status = po33_frame_parse(frame, frame_len, detach_strip_packet, &strip, &parsed);
  if (strip.status != PO_OK)
    return strip.status;
  if (status != PO_OK || !parsed.done || parsed.stopped || parsed.frame_bytes != frame_len)
    return PO_ERR_FRAME;
  status = po33_builder_finish(&builder, &base);
  if (status != PO_OK || base != expected.intermediate_frame_bytes)
    return PO_ERR_FRAME;
  status = po33_sample_insert_plan(workspace, base, slot, samples, sample_bytes, &insertion);
  if (status != PO_OK || insertion.required_frame_bytes != expected.required_frame_bytes)
    return PO_ERR_FRAME;
  return po33_sample_insert_rebuild(workspace, base, slot, samples, sample_bytes, &insertion,
                                    workspace + base, workspace_bytes - base, out, capacity,
                                    written);
}

typedef struct po33_range_source {
  uint8_t selector;
  size_t allocation_bytes;
} po33_range_source_t;

static po_status_t range_measure_source(const po33_topology_observed_record_t *record, void *user) {
  po33_range_source_t *source = (po33_range_source_t *)user;
  if (record->selector == source->selector && record->data_bytes > PO33_SOUND_HEADER_BYTES)
    source->allocation_bytes = record->data_bytes - PO33_SOUND_HEADER_BYTES;
  return PO_OK;
}

static void range_plan_clear(po33_sound_range_plan_t *plan) {
  *plan = (po33_sound_range_plan_t){
      .target_sound_record_index = SIZE_MAX,
      .referenced_pool_index = SIZE_MAX,
      .target_selector = UINT8_MAX,
      .source_selector = UINT8_MAX,
  };
}

po_status_t po33_sound_range_plan(const uint8_t *frame, size_t frame_len,
                                  size_t target_sound_record_index, uint32_t start_raw,
                                  uint32_t end_raw, po33_sound_range_plan_t *plan) {
  po33_pool_edit_measure_t measurement;
  po33_range_source_t source;
  po_status_t status;
  if (plan == NULL)
    return PO_ERR_INVALID_ARG;
  if (frame != NULL && po33_ranges_overlap(plan, sizeof(*plan), frame, frame_len))
    return PO_ERR_INVALID_ARG;
  range_plan_clear(plan);
  if (frame == NULL)
    return PO_ERR_INVALID_ARG;
  pool_edit_measure_init(&measurement, target_sound_record_index);
  status = topology_parse_exact_records(frame, frame_len, pool_edit_measure_record, &measurement);
  if (status != PO_OK)
    return status;
  if (!measurement.target_found) {
    plan->reason = PO33_SOUND_RANGE_REASON_TARGET_OUT_OF_RANGE;
    return PO_ERR_RANGE;
  }
  if (measurement.selector_count[measurement.target.selector] > 1u) {
    plan->reason = PO33_SOUND_RANGE_REASON_DUPLICATE_TARGET;
    return PO_ERR_PARSE;
  }
  if (measurement.target.flags & PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION) {
    plan->reason = PO33_SOUND_RANGE_REASON_UNKNOWN_TARGET_EXTENSION;
    return PO_ERR_PARSE;
  }
  source = (po33_range_source_t){.selector = measurement.target.header.source_selector};
  if (measurement.owner_count[source.selector] != 1u) {
    plan->reason = measurement.owner_count[source.selector] == 0u
                       ? PO33_SOUND_RANGE_REASON_SOURCE_UNRESOLVED
                       : PO33_SOUND_RANGE_REASON_SOURCE_AMBIGUOUS;
    return PO_ERR_PARSE;
  }
  status = topology_parse_exact_records(frame, frame_len, range_measure_source, &source);
  if (status != PO_OK)
    return status;
  if (start_raw >= end_raw || (uintmax_t)end_raw >= source.allocation_bytes) {
    plan->reason = PO33_SOUND_RANGE_REASON_INVALID_RANGE;
    return PO_ERR_RANGE;
  }
  *plan = (po33_sound_range_plan_t){
      .required_workspace_bytes = frame_len,
      .required_frame_bytes = frame_len,
      .target_sound_record_index = target_sound_record_index,
      .target_data_span = measurement.target.data_span,
      .referenced_pool_index = measurement.owner_pool_index[source.selector],
      .referenced_allocation_bytes = source.allocation_bytes,
      .range_start_raw = start_raw,
      .range_end_raw = end_raw,
      .target_selector = measurement.target.selector,
      .source_selector = source.selector,
  };
  return PO_OK;
}

static int range_plan_matches(const po33_sound_range_plan_t *left,
                              const po33_sound_range_plan_t *right) {
  return left->required_workspace_bytes == right->required_workspace_bytes &&
         left->required_frame_bytes == right->required_frame_bytes &&
         left->target_sound_record_index == right->target_sound_record_index &&
         left->target_data_span.offset == right->target_data_span.offset &&
         left->target_data_span.length == right->target_data_span.length &&
         left->referenced_pool_index == right->referenced_pool_index &&
         left->referenced_allocation_bytes == right->referenced_allocation_bytes &&
         left->range_start_raw == right->range_start_raw &&
         left->range_end_raw == right->range_end_raw &&
         left->target_selector == right->target_selector &&
         left->source_selector == right->source_selector && left->reason == right->reason;
}

typedef struct po33_range_rebuild {
  po33_builder_t *builder;
  const po33_sound_range_plan_t *plan;
  size_t header_bytes;
  po_status_t status;
} po33_range_rebuild_t;

static int range_rebuild_packet(const po_packet_t *packet, void *user) {
  po33_range_rebuild_t *rebuild = (po33_range_rebuild_t *)user;
  const po33_sound_range_plan_t *plan = rebuild->plan;
  uint8_t payload[PO_MAX_PAYLOAD];
  const uint8_t *data = packet->payload;
  if (packet->tag_code == PO33_TAG_BEGIN || packet->tag_code == PO33_TAG_END)
    return 0;
  if (packet->tag_code == PO33_TAG_SOUND_DATA && packet->offset >= plan->target_data_span.offset &&
      packet->offset - plan->target_data_span.offset < plan->target_data_span.length &&
      rebuild->header_bytes < 8u) {
    pool_edit_copy_bytes(payload, packet->payload, packet->payload_len);
    for (size_t index = 0u; index < packet->payload_len && rebuild->header_bytes < 8u; ++index) {
      size_t byte = rebuild->header_bytes++;
      uint32_t value = byte < 4u ? plan->range_start_raw : plan->range_end_raw;
      payload[index] = (uint8_t)(value >> (8u * (byte % 4u)));
    }
    data = payload;
  }
  rebuild->status = po33_builder_append_packet(rebuild->builder, packet->tag_code, data,
                                               packet->payload_len, NULL);
  return rebuild->status != PO_OK;
}

po_status_t po33_sound_range_rebuild(const uint8_t *frame, size_t frame_len,
                                     size_t target_sound_record_index, uint32_t start_raw,
                                     uint32_t end_raw, const po33_sound_range_plan_t *plan,
                                     uint8_t *workspace, size_t workspace_bytes, uint8_t *out_frame,
                                     size_t out_capacity, size_t *out_frame_bytes) {
  po33_sound_range_plan_t expected = {0};
  po33_builder_t builder;
  po33_decode_result_t result;
  po33_range_rebuild_t rebuild;
  size_t frame_bytes;
  po_status_t status;
  int invalid_aliases =
      edit_rebuild_has_invalid_aliases(frame, frame_len, NULL, 0u, plan, sizeof(*plan), workspace,
                                       workspace_bytes, out_frame, out_capacity, out_frame_bytes);
  if (out_frame_bytes != NULL && !invalid_aliases)
    *out_frame_bytes = 0u;
  if (frame == NULL || plan == NULL || workspace == NULL || out_frame == NULL ||
      out_frame_bytes == NULL || invalid_aliases)
    return PO_ERR_INVALID_ARG;
  status = po33_sound_range_plan(frame, frame_len, target_sound_record_index, start_raw, end_raw,
                                 &expected);
  if (status != PO_OK || !range_plan_matches(plan, &expected))
    return PO_ERR_INVALID_ARG;
  if (workspace_bytes < expected.required_workspace_bytes ||
      out_capacity < expected.required_frame_bytes)
    return PO_ERR_BUFFER_TOO_SMALL;
  status = po33_builder_init(&builder, workspace, workspace_bytes);
  if (status != PO_OK)
    return status;
  rebuild = (po33_range_rebuild_t){.builder = &builder, .plan = &expected, .status = PO_OK};
  status = po33_frame_parse(frame, frame_len, range_rebuild_packet, &rebuild, &result);
  if (rebuild.status != PO_OK)
    return rebuild.status;
  if (status != PO_OK || !result.done || result.stopped || result.frame_bytes != frame_len ||
      rebuild.header_bytes != 8u)
    return PO_ERR_FRAME;
  status = po33_builder_finish(&builder, &frame_bytes);
  if (status != PO_OK)
    return status;
  if (frame_bytes != expected.required_frame_bytes)
    return PO_ERR_FRAME;
  pool_edit_copy_bytes(out_frame, workspace, frame_bytes);
  *out_frame_bytes = frame_bytes;
  return PO_OK;
}
