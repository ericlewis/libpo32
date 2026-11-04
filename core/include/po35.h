#ifndef PO35_H
#define PO35_H

#include "po_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PO-35 and PO-137 share this backup grammar. These codecs establish offline
 * byte preservation, not device installation or exact speech synthesis. */
#define PO35_PREAMBLE_BYTES      4096u
#define PO35_TAG_PATCH           0x37b2u
#define PO35_TAG_TABLE_7BE7      0x7be7u
#define PO35_TAG_TABLE_1C7F      0x1c7fu
#define PO35_TAG_PATTERN_EMPTY   0x29ffu
#define PO35_TAG_PATTERN         0xccb2u
#define PO35_TAG_SPEECH_HEADER   0xdc32u
#define PO35_TAG_SPEECH_DATA     0x73b0u
#define PO35_TAG_SPEECH_END      0x29a1u
#define PO35_TAG_SETTINGS        0x84d8u
#define PO35_SPEECH_HEADER_BYTES 9u
#define PO35_SPEECH_RECORD_BYTES 44u
#define PO35_SPEECH_SLOT_COUNT   15u
#define PO35_DRUM_COUNT          16u
#define PO35_PATTERN_STEP_COUNT  16u
#define PO35_PATTERN_BYTES       183u
#define PO35_SETTINGS_BASE_BYTES 156u
#define PO35_CHAIN_MAX_COUNT     (PO_MAX_PAYLOAD - PO35_SETTINGS_BASE_BYTES)

typedef struct po35_decode_result {
  size_t packet_count;
  size_t frame_bytes;
  int done;
  uint16_t final_state;
  int stopped;
} po35_decode_result_t;

typedef struct po35_builder {
  uint8_t *buffer;
  size_t capacity;
  size_t length;
  uint16_t state;
  int finished;
} po35_builder_t;

/* Validate counter preamble, packets and special final tail. Unknown packets
 * are delivered raw through borrowed packet pointers; copy the packet struct
 * to retain its contents. No BEGIN/END pair is required. A nonzero callback
 * return stops with PO_OK, stopped=1 and done=0. frame_bytes and final_state
 * describe the validated prefix even on later failure.
 * Trailing data after the tail is ignored and excluded from frame_bytes.
 * result may be NULL. Packet offsets include the full counter preamble.
 * This validates framing, not speech-group completeness or packet ordering. */
po_status_t po35_frame_parse(const uint8_t *frame, size_t frame_len, po_packet_callback_t callback,
                             void *user, po35_decode_result_t *result);

/* Initialize counter preamble only. A minimum PO35_PREAMBLE_BYTES capacity is
 * required. Failure leaves builder empty and the output buffer untouched.
 * Builder, output and input payload storage must not overlap. */
po_status_t po35_builder_init(po35_builder_t *builder, uint8_t *buffer, size_t capacity);
/* Append 0..255 raw bytes. The final-tail tag 0x71c3 is reserved. Failure does
 * not mutate the builder, output or optional packet_offset. */
po_status_t po35_builder_append_packet(po35_builder_t *builder, uint16_t tag_code,
                                       const uint8_t *payload, size_t payload_len,
                                       size_t *packet_offset);
/* Append the special five-byte final tail, idempotently. No partial writes.
 * frame_len is optional and is written only on success. */
po_status_t po35_builder_finish(po35_builder_t *builder, size_t *frame_len);

typedef struct po35_speech_header {
  uint8_t slot;
  uint16_t record_count;
  uint8_t records_per_second;
  uint8_t metadata[5]; /* Original payload bytes 4..8; semantics unresolved. */
} po35_speech_header_t;

typedef struct po35_speech_record {
  int16_t pitch; /* Signed Q11 octaves relative to C4 in the recovery model. */
  uint8_t gain;
  uint8_t unvoiced;
  int8_t low_lar_raw[10];
  int8_t high_lar_raw[30];
} po35_speech_record_t;

typedef struct po35_speech_controls {
  uint8_t parameters[4]; /* Byte order is known; individual names are not. */
  uint8_t state_raw[4];
} po35_speech_controls_t;

typedef struct po35_drum_controls {
  uint8_t pitch;
  uint8_t morph;
} po35_drum_controls_t;

typedef struct po35_settings {
  po35_speech_controls_t speech[PO35_SPEECH_SLOT_COUNT];
  po35_drum_controls_t drums[PO35_DRUM_COUNT];
  uint8_t tempo;
  uint8_t state_raw[2]; /* Payload bytes 153..154. */
  uint8_t chain_count;
  uint8_t chain[PO35_CHAIN_MAX_COUNT]; /* Zero-based indices; repeats preserved. */
} po35_settings_t;

typedef struct po35_pattern {
  uint8_t pattern_index;
  uint8_t speech_events[PO35_PATTERN_STEP_COUNT];
  uint8_t speech_secondary_raw[PO35_PATTERN_STEP_COUNT];
  uint8_t speech_parameters[PO35_PATTERN_STEP_COUNT][4];
  uint8_t drum_events[PO35_PATTERN_STEP_COUNT];
  po35_drum_controls_t drums[PO35_PATTERN_STEP_COUNT];
  uint8_t index_pairs_raw[PO35_PATTERN_STEP_COUNT];
  uint8_t step_state_raw[PO35_PATTERN_STEP_COUNT];
  uint8_t tail_raw[4];   /* Payload bytes 177..180. */
  uint16_t derived_mask; /* Stored verbatim; encoding never recomputes it. */
} po35_pattern_t;

/* Lossless structural codecs. Decode requires the exact payload size and
 * returns PO_ERR_PARSE on malformed lengths. Encode requires capacity for
 * the complete payload and returns PO_ERR_BUFFER_TOO_SMALL before writing.
 * Null input/output is PO_ERR_INVALID_ARG. No value normalization is done;
 * unknown fields and future byte values survive roundtrips. Input/output
 * must not overlap. Drum patch 0x37b2 uses the existing po32_packet_encode /
 * po32_packet_decode with po32_patch_packet_t; it is not duplicated here. */
po_status_t po35_speech_header_decode(const uint8_t *data, size_t len, po35_speech_header_t *out);
po_status_t po35_speech_header_encode(const po35_speech_header_t *header, uint8_t *out,
                                      size_t capacity);
po_status_t po35_speech_record_decode(const uint8_t *data, size_t len, po35_speech_record_t *out);
po_status_t po35_speech_record_encode(const po35_speech_record_t *record, uint8_t *out,
                                      size_t capacity);
po_status_t po35_settings_decode(const uint8_t *data, size_t len, po35_settings_t *out);
/* chain_count may be 0..PO35_CHAIN_MAX_COUNT. Larger values are PO_ERR_RANGE.
 * out_len is optional and is written only on success. */
po_status_t po35_settings_encode(const po35_settings_t *settings, uint8_t *out, size_t capacity,
                                 size_t *out_len);
po_status_t po35_pattern_decode(const uint8_t *data, size_t len, po35_pattern_t *out);
po_status_t po35_pattern_encode(const po35_pattern_t *pattern, uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
