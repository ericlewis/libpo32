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
  if (!stream->record.have_data || stream->record.header_length != PO33_SOUND_HEADER_BYTES)
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

po_status_t po33_pool_topology_requirements(const uint8_t *frame, size_t frame_len,
                                            size_t *sound_record_count, size_t *pool_count,
                                            size_t *reference_count) {
  po33_topology_measurement_t measurement;
  po_status_t status;
  if (frame == NULL || sound_record_count == NULL || pool_count == NULL || reference_count == NULL)
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

static int pool_replacement_ranges_overlap(const void *left, size_t left_bytes, const void *right,
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
  po_status_t status = PO_OK;

  if (plan == NULL)
    return PO_ERR_INVALID_ARG;
  if ((sound_records == NULL && sound_record_count != 0u) || (pools == NULL && pool_count != 0u)) {
    status = PO_ERR_INVALID_ARG;
    goto finish;
  }
  if (sound_record_count > SIZE_MAX / sizeof(*sound_records) ||
      pool_count > SIZE_MAX / sizeof(*pools)) {
    status = PO_ERR_INVALID_ARG;
    goto finish;
  }
  if (pool_replacement_ranges_overlap(plan, sizeof(*plan), sound_records,
                                      sound_record_count * sizeof(*sound_records)) ||
      pool_replacement_ranges_overlap(plan, sizeof(*plan), pools, pool_count * sizeof(*pools)))
    return PO_ERR_INVALID_ARG;
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
  result.affected_sound_record_count = 1u;
  for (size_t i = 0; i < sound_record_count; ++i) {
    const po33_sound_record_t *record = sound_records + i;
    if (record->source_selector == target->selector &&
        record->referenced_pool_index != target_pool_index) {
      result.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
      status = PO_ERR_PARSE;
      goto finish;
    }
    if (record->referenced_pool_index != target_pool_index)
      continue;
    if (result.reference_count == SIZE_MAX ||
        (i != target_sound_record_index && result.affected_sound_record_count == SIZE_MAX)) {
      result.reason = PO33_POOL_REPLACEMENT_REASON_COUNT_OVERFLOW;
      status = PO_ERR_RANGE;
      goto finish;
    }
    ++result.reference_count;
    if (i != target_sound_record_index)
      ++result.affected_sound_record_count;
    if (record->source_selector != target->selector ||
        (record->flags &
         (PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR | PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED |
          PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS)) != 0u) {
      result.reason = PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR;
      status = PO_ERR_PARSE;
      goto finish;
    }
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
