#ifndef PO33_H
#define PO33_H

#include "po_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PO-33/133 backup codec. Field roles and evidence limits: docs/PO_FORMATS.md. */
#define PO33_PREAMBLE_BYTES     4096u
#define PO33_TAG_BEGIN          0x985Bu
#define PO33_TAG_SETTINGS       0x7D5Cu
#define PO33_TAG_PATTERN_SELECT 0xA041u
#define PO33_TAG_PATTERN_DATA   0xC498u
#define PO33_TAG_SOUND_SELECT   0x68B5u
#define PO33_TAG_SOUND_DATA     0xEF78u
#define PO33_TAG_END            0x826Fu
#define PO33_SAMPLE_RATE_HZ     23437.5f
#define PO33_SETTINGS_BYTES     510u
#define PO33_PATTERN_BYTES      1032u
#define PO33_PATTERN_FX_BYTES   8u
#define PO33_SOUND_HEADER_BYTES 9u
#define PO33_STEP_COUNT         16u
#define PO33_SOUND_COUNT        16u
#define PO33_CHAIN_CAPACITY     128u
#define PO33_FX_OCTAVE_UP       6u
#define PO33_FX_BASELINE        15u /* Observed normal baseline; clearing behavior is unresolved. */

#define PO33_POOL_ALLOCATION_BLOCK_BYTES 4096u

/* Logical records may span multiple wire packets. Preserve unused chain
 * entries and uninterpreted bytes; no trim convention is inferred. */
typedef struct po33_settings {
  uint8_t prefix_raw[255];
  uint8_t chain[PO33_CHAIN_CAPACITY];
  uint8_t chain_length, tempo, swing_raw;
  uint8_t state_raw[16]; /* Retained state; FX associations observed, update rules unresolved. */
  uint8_t suffix_raw[108];
} po33_settings_t;

/* Raw nibble views; numeric note, duration and retrigger mappings are unresolved. */
typedef struct po33_pattern_control {
  uint8_t note_raw;
  uint8_t timing_raw;
} po33_pattern_control_t;

/* Per-step FX codes in step order. Preserve all codes, including unnamed ones. */
typedef struct po33_pattern_fx {
  uint8_t code_raw[PO33_STEP_COUNT];
} po33_pattern_fx_t;

typedef struct po33_pattern_event {
  /* LE24 parameter word: observed pitch, level and filter roles; resonance is
   * a candidate for the remaining chunk. Widths/scales remain raw. Preserve
   * parameter-only rows even when control_raw is zero. */
  uint8_t event_raw[3];
  /* Note/key-related low nibble; timing-related high nibble (melodic length
   * or drum retrigger context). Exact numeric mappings remain unresolved. */
  uint8_t control_raw;
} po33_pattern_event_t;

typedef struct po33_pattern {
  po33_pattern_event_t events[PO33_STEP_COUNT][PO33_SOUND_COUNT];
  /* Two FX codes per byte, high nibble first. Code 6 is octave-up;
   * other effect IDs remain raw. */
  uint8_t footer_raw[8];
} po33_pattern_t;

typedef struct po33_sound_header {
  /* Raw coordinates into the whole referenced pool, not its owner's trim.
   * Endpoint inclusion is unassigned; do not clamp to received allocation. */
  uint32_t range_start_raw, range_end_raw;
  uint8_t source_selector;
} po33_sound_header_t;

/* A byte span in a complete PO-33 source frame. Offsets include the preamble.
 * A zero-length span has offset zero. A span reports wire bytes only; it does
 * not imply that the enclosed SOUND_DATA payloads are contiguous. */
typedef struct po33_frame_span {
  size_t offset;
  size_t length;
} po33_frame_span_t;

/* Topology flags describe retained ambiguity; they do not assign playback or
 * allocation semantics to the raw range values. */
#define PO33_SOUND_RECORD_FLAG_HEADER_ONLY        0x01u
#define PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR 0x02u
#define PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION  0x04u
#define PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED  0x08u
#define PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS   0x10u

/* One SOUND_SELECT record and its following SOUND_DATA region. selection_span
 * covers its selector packet. data_span covers the raw packet region from the
 * first SOUND_DATA packet through the last, including packet framing and any
 * intervening unknown extension packets. range_* and source_selector are the
 * decoded nine-byte sound header, not inferred playback coordinates. Either
 * pool index is SIZE_MAX when its relationship is unavailable or ambiguous. */
typedef struct po33_sound_record {
  uint8_t selector;
  uint32_t range_start_raw, range_end_raw;
  uint8_t source_selector;
  unsigned int flags;
  po33_frame_span_t selection_span;
  po33_frame_span_t data_span;
  size_t owned_pool_index;
  size_t referenced_pool_index;
} po33_sound_record_t;

/* A pool is an owned SOUND_DATA record with bytes beyond its nine-byte header.
 * data_bytes counts only SOUND_DATA payload bytes, including that header;
 * data_span is a raw wire span and may include packet framing. */
typedef struct po33_pool {
  size_t owner_record_index;
  po33_frame_span_t data_span;
  size_t data_bytes;
} po33_pool_t;

/* A resolved source-selector relationship. The referenced sound record owns
 * the pool; the referenced pool is not necessarily the record's own pool. */
typedef struct po33_pool_reference {
  size_t pool_index;
  size_t sound_record_index;
} po33_pool_reference_t;

/* Why a requested owned-pool replacement cannot be planned safely. These
 * reasons describe topology facts or allocation shape; they do not assign
 * playback semantics to a raw range or establish device capacity. */
typedef enum po33_pool_replacement_reason {
  PO33_POOL_REPLACEMENT_REASON_NONE = 0,
  PO33_POOL_REPLACEMENT_REASON_TARGET_RECORD_OUT_OF_RANGE = 1,
  PO33_POOL_REPLACEMENT_REASON_TARGET_NOT_OWNED = 2,
  PO33_POOL_REPLACEMENT_REASON_DUPLICATE_TARGET_SELECTOR = 3,
  PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_SELECTOR = 4,
  PO33_POOL_REPLACEMENT_REASON_UNKNOWN_TARGET_EXTENSION = 5,
  PO33_POOL_REPLACEMENT_REASON_AMBIGUOUS_GROUP_BOUNDARY = 6,
  PO33_POOL_REPLACEMENT_REASON_EXISTING_ALLOCATION_UNALIGNED = 7,
  PO33_POOL_REPLACEMENT_REASON_REQUIRED_ALLOCATION_OVERFLOW = 8,
  PO33_POOL_REPLACEMENT_REASON_COUNT_OVERFLOW = 9
} po33_pool_replacement_reason_t;

/* A read-only replacement readiness plan for one owned pool. requested,
 * existing and required allocation sizes exclude the nine-byte sound header.
 * required_allocation_bytes never shrinks an existing pool; growth rounds it
 * up to PO33_POOL_ALLOCATION_BLOCK_BYTES. reference_count counts every record
 * whose resolved source points at target_pool_index, including the owner when
 * it resolves to itself. affected_sound_record_count counts that owner once
 * plus every other resolved reference. growth_structurally_eligible reports
 * whether this topology could append whole blocks, even when growth is not
 * needed for the current request.
 * On a rejected plan, available facts remain populated and reason explains the
 * first unsafe condition; target_pool_index is SIZE_MAX when no owned target is
 * available. */
typedef struct po33_pool_replacement_plan {
  size_t target_pool_index;
  size_t requested_sample_bytes;
  size_t existing_allocation_bytes;
  size_t required_allocation_bytes;
  size_t required_growth_bytes;
  size_t reference_count;
  size_t affected_sound_record_count;
  int existing_allocation_fits;
  int growth_required;
  int growth_structurally_eligible;
  po33_pool_replacement_reason_t reason;
} po33_pool_replacement_plan_t;

/* Split/combine control_raw: low note/key nibble, high timing nibble.
 * Encode requires both fields in 0..15; invalid input leaves output untouched.
 * Input/output must not overlap. */
po_status_t po33_pattern_control_decode(uint8_t raw, po33_pattern_control_t *out);
po_status_t po33_pattern_control_encode(const po33_pattern_control_t *value, uint8_t *out_raw);

/* Fixed-record codecs: null input/output returns PO_ERR_INVALID_ARG; a wrong
 * decode length returns PO_ERR_FRAME; insufficient encode capacity returns
 * PO_ERR_BUFFER_TOO_SMALL. Settings chain lengths above PO33_CHAIN_CAPACITY
 * return PO_ERR_RANGE. Failures leave outputs unchanged; successful encoders
 * leave capacity beyond the record size untouched. Input/output must not overlap. */
po_status_t po33_settings_decode(const uint8_t *data, size_t len, po33_settings_t *out);
po_status_t po33_settings_encode(const po33_settings_t *value, uint8_t *out, size_t capacity);
po_status_t po33_pattern_decode(const uint8_t *data, size_t len, po33_pattern_t *out);
po_status_t po33_pattern_encode(const po33_pattern_t *value, uint8_t *out, size_t capacity);
/* FX records contain two codes per byte, high nibble first. Decode preserves
 * every code; encode requires each code in 0..15. No FX update rules are inferred. */
po_status_t po33_pattern_fx_decode(const uint8_t *data, size_t len, po33_pattern_fx_t *out);
po_status_t po33_pattern_fx_encode(const po33_pattern_fx_t *value, uint8_t *out, size_t capacity);
po_status_t po33_sound_header_decode(const uint8_t *data, size_t len, po33_sound_header_t *out);
po_status_t po33_sound_header_encode(const po33_sound_header_t *value, uint8_t *out,
                                     size_t capacity);

typedef struct po33_decode_result {
  size_t packet_count;
  size_t frame_bytes;
  int done;
  uint16_t final_state;
  int stopped;
} po33_decode_result_t;

/*
 * Pack interleaved left/right absolute phase quadrants in 0..3, increasing
 * with analytic-signal phase, into wire bytes. Differences are modulo four.
 * Right is bits 0..1, left is bits 2..3; first stereo symbol is the low nibble.
 * The first absolute symbol is a reference, producing no output. parity (0/1)
 * skips that many differential symbols. An odd final nibble is omitted.
 * Input/output must not overlap. On insufficient capacity, out_len receives
 * the required length and output is untouched. Empty input is permitted.
 */
po_status_t po33_pack_symbols(const uint8_t *phases, size_t symbol_count, unsigned int parity,
                              uint8_t *out_wire, size_t capacity, size_t *out_len);

/*
 * Parse a frame starting at its 4092-byte counter ramp and four-byte sync.
 * Verify every trailer using PO-32 CRC/whitening, continuously seeded 0x1D0F.
 * Unknown tags are delivered raw, without assigning semantics. Packet offsets
 * count from this PO-33 frame's start. BEGIN and END must have zero length.
 * Trailing bytes after END are ignored; frame_bytes reports consumed bytes.
 *
 * Validated packets may be delivered before a later error. Only result->done
 * establishes a complete transfer. A nonzero callback return stops with
 * PO_OK and result->stopped set, without asserting completion. result may
 * be NULL. Existing PO-32 APIs and their strict tag checks are unaffected.
 */
po_status_t po33_frame_parse(const uint8_t *frame, size_t frame_len, po_packet_callback_t callback,
                             void *user, po33_decode_result_t *result);

/* Inspect a complete, CRC-valid frame and return exact element counts for
 * caller-owned sound-record, pool and reverse-reference arrays. Every result
 * pointer is required. Structurally malformed source records return
 * PO_ERR_FRAME; size overflow returns PO_ERR_RANGE. Duplicate selectors and
 * unknown extensions remain successful topology facts, represented by record
 * flags. On failure, all count outputs are unchanged. */
po_status_t po33_pool_topology_requirements(const uint8_t *frame, size_t frame_len,
                                            size_t *sound_record_count, size_t *pool_count,
                                            size_t *reference_count);

/* Build the views measured by po33_pool_topology_requirements(). Each array
 * capacity must cover its queried count; an array may be NULL only with zero
 * queried count and zero capacity. Insufficient storage returns
 * PO_ERR_BUFFER_TOO_SMALL before changing any array. Null required storage
 * returns PO_ERR_INVALID_ARG.
 * Malformed source frames return PO_ERR_FRAME; unrepresentable counts return
 * PO_ERR_RANGE. On every failure all supplied arrays remain unchanged.
 * Successful calls write exactly the queried elements and leave capacity beyond
 * them untouched. */
po_status_t po33_pool_topology_build(const uint8_t *frame, size_t frame_len,
                                     po33_sound_record_t *sound_records,
                                     size_t sound_record_capacity, po33_pool_t *pools,
                                     size_t pool_capacity, po33_pool_reference_t *references,
                                     size_t reference_capacity);

/* Plan a replacement against caller-owned topology arrays returned together by
 * po33_pool_topology_build(). target_sound_record_index identifies the pool's
 * owning record, not a raw source selector. requested_sample_bytes is encoded
 * sample storage excluding the nine-byte header. No source frame, header,
 * range, reference or allocation is changed.
 *
 * sound_records and pools may be NULL only with their matching zero count.
 * plan is required and must not overlap either input array. Target range and
 * ownership failures return PO_ERR_RANGE; unsafe or internally inconsistent
 * topology returns PO_ERR_PARSE. A valid plan output is cleared for invalid
 * arguments, while plan.reason describes every target-specific rejection.
 * PO_OK means the existing allocation fits, or required growth is structurally
 * eligible; it does not establish an output-frame capacity or device support. */
po_status_t po33_pool_replacement_plan(const po33_sound_record_t *sound_records,
                                       size_t sound_record_count, const po33_pool_t *pools,
                                       size_t pool_count, size_t target_sound_record_index,
                                       size_t requested_sample_bytes,
                                       po33_pool_replacement_plan_t *plan);

/* Expand one signed-order mu-law sample byte to linear PCM16. Parse framing
 * and the sound header first; pool padding can be decoded, while applying
 * trim/slice ranges is separate. The nominal clock is PO33_SAMPLE_RATE_HZ. */
int16_t po33_sample_decode(uint8_t encoded);

/* Nearest representable signed-order mu-law level, saturating at +/-32124.
 * Ties select the smaller magnitude. Zero has the canonical encoding 0;
 * decoding the alternate zero byte 255 and re-encoding it produces 0. */
uint8_t po33_sample_encode(int16_t sample);

/* Size a mono PCM conversion to the nominal 23437.5 Hz sample clock. Input
 * rates are 8000..192000 Hz; 23438 is treated as an exact native-rate alias
 * for exported WAVs. Other rates round duration up to one whole output sample.
 * Empty input requires zero output bytes. Counts that cannot fit the input
 * float array, output buffer or signed 64-bit sample index return RANGE.
 * On error *out_count is zero. No device allocation limit is imposed. */
po_status_t po33_sample_count_for_pcm(size_t count, uint32_t rate, size_t *out_count);

/* Convert finite normalized mono PCM in [-1,1] to signed-order mu-law bytes.
 * A bandlimited filter resamples nonnative rates; native 23438 Hz input is
 * encoded directly. Values scale by 32768 and round to nearest PCM16 (ties
 * away from zero), saturating at the codec's +/-32124 limits before encoding.
 * All validation precedes output writes; on error output is untouched and
 * *written is zero. Empty input permits NULL pcm/out. Input, output and written
 * must be disjoint. No allocation or persistent state; IEEE-754 float/double
 * are required. Headers, pool allocation and metadata are the caller's job. */
po_status_t po33_samples_encode_f32(const float *pcm, size_t count, uint32_t rate, uint8_t *out,
                                    size_t capacity, size_t *written);

typedef struct po33_builder {
  uint8_t *buffer;
  size_t capacity;
  size_t length;
  uint16_t state;
  int finished;
} po33_builder_t;

/* Initialize caller-owned storage with the preamble and zero-length BEGIN.
 * At least PO33_PREAMBLE_BYTES + 5 bytes are required. Invalid arguments or
 * insufficient capacity leave the output buffer untouched and builder empty.
 * Input payloads must not overlap the output buffer or builder structure. */
po_status_t po33_builder_init(po33_builder_t *builder, uint8_t *buffer, size_t capacity);

/* Append a raw packet (0..255 payload bytes). BEGIN and END are reserved for
 * init/finish. Unknown tags are preserved. Failure does not mutate the builder,
 * output or packet_offset. Offsets include the counter preamble. */
po_status_t po33_builder_append_packet(po33_builder_t *builder, uint16_t tag_code,
                                       const uint8_t *payload, size_t payload_len,
                                       size_t *packet_offset);

/* Append zero-length END; idempotent after success. No partial writes on error.
 * frame_len is optional and is written only on success. */
po_status_t po33_builder_finish(po33_builder_t *builder, size_t *frame_len);

#ifdef __cplusplus
}
#endif
#endif
