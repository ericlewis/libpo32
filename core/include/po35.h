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
#define PO35_TABLE_7BE7_BYTES    9u
#define PO35_TABLE_1C7F_BYTES    13u
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

/* Indexed table payloads. The bodies remain opaque bytes; all index and data
 * byte values are preserved, including indices outside the observed 0..7.
 * Table codec failures leave outputs unchanged; successful encoders leave
 * any capacity beyond the fixed payload size untouched. */
typedef struct po35_table_7be7 {
  uint8_t index;
  uint8_t data_raw[8];
} po35_table_7be7_t;

typedef struct po35_table_1c7f {
  uint8_t index;
  uint8_t data_raw[12];
} po35_table_1c7f_t;

typedef struct po35_speech_header {
  uint8_t slot;
  uint16_t record_count;
  uint8_t records_per_second;
  uint8_t metadata[5]; /* Payload 4..8; tuning/gain/noise interpretations are candidates. */
} po35_speech_header_t;

typedef struct po35_speech_record {
  int16_t pitch;    /* Signed Q11 octaves relative to C4 in the recovery model. */
  uint8_t gain;     /* Raw code; gain transfer function is unverified. */
  uint8_t unvoiced; /* Raw code; noise/unvoiced transfer function is unverified. */
  /* Signed raw coefficients; LAR interpretation belongs to the recovery model. */
  int8_t low_lar_raw[10];
  int8_t high_lar_raw[30];
} po35_speech_record_t;

/* One replacement for a complete speech slot. records names already encoded,
 * contiguous 44-byte speech records; it is borrowed and is never decoded as a
 * C struct overlay. record_bytes may be zero, otherwise it must be a multiple
 * of PO35_SPEECH_RECORD_BYTES. records may be NULL only when record_bytes is
 * zero. records_per_second is written verbatim to the selected header's rate
 * byte; this API does not normalize or infer a rate. */
typedef struct po35_speech_edit {
  uint8_t slot;
  uint8_t records_per_second;
  const uint8_t *records;
  size_t record_bytes;
} po35_speech_edit_t;

/* Why a requested edit cannot produce a safe transaction plan. These reasons
 * describe an edit or its selected source group; framing failure remains
 * PO_ERR_FRAME and invalid pointers or aliases remain PO_ERR_INVALID_ARG. */
typedef enum po35_speech_edit_reason {
  PO35_SPEECH_EDIT_REASON_NONE = 0,
  PO35_SPEECH_EDIT_REASON_DUPLICATE_SLOT = 1,
  PO35_SPEECH_EDIT_REASON_SLOT_OUT_OF_RANGE = 2,
  PO35_SPEECH_EDIT_REASON_INVALID_RECORD_SIZE = 3,
  PO35_SPEECH_EDIT_REASON_INVALID_RECORD_COUNT = 4,
  PO35_SPEECH_EDIT_REASON_ABSENT_SLOT = 5,
  PO35_SPEECH_EDIT_REASON_INCOMPLETE_GROUP = 6,
  PO35_SPEECH_EDIT_REASON_UNKNOWN_SELECTED_GROUP_EXTENSION = 7
} po35_speech_edit_reason_t;

/* A read-only transaction plan. On success, selected_slot_mask has one bit for
 * each selected slot (bit 0 is slot 0), affected_slot_count is its population,
 * capacities are exact, reason is NONE, failed_edit_index is SIZE_MAX and
 * failed_slot is UINT8_MAX. On an edit-specific RANGE or PARSE failure, all
 * capacities and selected fields are zero while reason, failed_edit_index and
 * failed_slot identify the rejected edit. A plan failure with no rejected edit
 * has zero capacities and selected fields, NONE, SIZE_MAX and UINT8_MAX. */
typedef struct po35_speech_edit_plan {
  size_t required_workspace_bytes;
  size_t required_frame_bytes;
  size_t affected_slot_count;
  size_t failed_edit_index;
  uint16_t selected_slot_mask;
  uint8_t failed_slot;
  po35_speech_edit_reason_t reason;
} po35_speech_edit_plan_t;

typedef struct po35_speech_controls {
  uint8_t parameters[4]; /* Same four-byte order as per-step speech parameters. */
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
  /* Payload 153..154; the second byte is a candidate remembered sound ID. */
  uint8_t state_raw[2];
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
po_status_t po35_table_7be7_decode(const uint8_t *data, size_t len, po35_table_7be7_t *out);
po_status_t po35_table_7be7_encode(const po35_table_7be7_t *value, uint8_t *out, size_t capacity);
po_status_t po35_table_1c7f_decode(const uint8_t *data, size_t len, po35_table_1c7f_t *out);
po_status_t po35_table_1c7f_encode(const po35_table_1c7f_t *value, uint8_t *out, size_t capacity);
po_status_t po35_speech_header_decode(const uint8_t *data, size_t len, po35_speech_header_t *out);
po_status_t po35_speech_header_encode(const po35_speech_header_t *header, uint8_t *out,
                                      size_t capacity);
po_status_t po35_speech_record_decode(const uint8_t *data, size_t len, po35_speech_record_t *out);
po_status_t po35_speech_record_encode(const po35_speech_record_t *record, uint8_t *out,
                                      size_t capacity);

/* Transactional speech replacement. po35_speech_edit_plan is implemented;
 * po35_speech_edit_rebuild remains reserved for the following atomic-rebuild
 * task and must not be invoked until that implementation is linked.
 *
 * Both functions borrow frame, edits and every edit's record storage for the
 * call. Those read-only inputs may overlap each other. A plan output, plan
 * input, workspace, output frame and output-length storage must be disjoint
 * from each other and from every borrowed input. Prohibited aliases are
 * PO_ERR_INVALID_ARG; output guarantees do not apply to invalid aliases.
 * workspace contents are unspecified after a call.
 *
 * plan validates a complete source frame and all requested edits without
 * writing a frame. edits may be NULL only when edit_count is zero. plan is
 * required. A nonempty edit body must be divisible by PO35_SPEECH_RECORD_BYTES
 * and record_bytes / PO35_SPEECH_RECORD_BYTES must not exceed UINT16_MAX;
 * records may be NULL only for a zero-byte body. PO_ERR_RANGE reports a
 * duplicate/out-of-range slot or invalid body through plan.reason. PO_ERR_PARSE
 * reports an absent slot, incomplete selected group or selected-group unknown
 * extension through plan.reason. PO_ERR_FRAME reports invalid source framing.
 * For a non-NULL, nonoverlapping plan output, every other failure returns the
 * no-plan state described above.
 *
 * rebuild consumes a successful matching plan and revalidates its inputs. A
 * stale, mismatched or failed plan is PO_ERR_INVALID_ARG. workspace may be
 * NULL only when plan.required_workspace_bytes is zero. out_frame and
 * out_frame_bytes are always required. PO_ERR_BUFFER_TOO_SMALL reports either
 * insufficient workspace or output capacity. For valid, nonoverlapping output
 * arguments, every failure leaves out_frame untouched and sets
 * *out_frame_bytes to zero. */
po_status_t po35_speech_edit_plan(const uint8_t *frame, size_t frame_len,
                                  const po35_speech_edit_t *edits, size_t edit_count,
                                  po35_speech_edit_plan_t *plan);
po_status_t po35_speech_edit_rebuild(const uint8_t *frame, size_t frame_len,
                                     const po35_speech_edit_t *edits, size_t edit_count,
                                     const po35_speech_edit_plan_t *plan, uint8_t *workspace,
                                     size_t workspace_bytes, uint8_t *out_frame,
                                     size_t out_capacity, size_t *out_frame_bytes);

po_status_t po35_settings_decode(const uint8_t *data, size_t len, po35_settings_t *out);
/* chain_count may be 0..PO35_CHAIN_MAX_COUNT. Larger values are PO_ERR_RANGE.
 * This 99-entry record capacity does not establish device support above the
 * PO-35 manual's 64-pattern chain limit.
 * out_len is optional and is written only on success. */
po_status_t po35_settings_encode(const po35_settings_t *settings, uint8_t *out, size_t capacity,
                                 size_t *out_len);
po_status_t po35_pattern_decode(const uint8_t *data, size_t len, po35_pattern_t *out);
po_status_t po35_pattern_encode(const po35_pattern_t *pattern, uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
