#ifndef PO33_H
#define PO33_H

#include "po_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental PO-33 backup codec. Generated audio requires device validation. */
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
#define PO33_SOUND_HEADER_BYTES 9u
#define PO33_STEP_COUNT         16u
#define PO33_SOUND_COUNT        16u
#define PO33_CHAIN_CAPACITY     128u

/* Logical records may span multiple wire packets. Preserve unused chain
 * entries and uninterpreted bytes; no trim convention is inferred. */
typedef struct po33_settings {
  uint8_t prefix_raw[255];
  uint8_t chain[PO33_CHAIN_CAPACITY];
  uint8_t chain_length, tempo, swing_raw;
  uint8_t state_raw[16];
  uint8_t suffix_raw[108];
} po33_settings_t;

typedef struct po33_pattern_event {
  uint8_t event_raw[3];
  /* Note-related low nibble; context-dependent parameter/retrigger high nibble. */
  uint8_t control_raw;
} po33_pattern_event_t;

typedef struct po33_pattern {
  po33_pattern_event_t events[PO33_STEP_COUNT][PO33_SOUND_COUNT];
  uint8_t footer_raw[8];
} po33_pattern_t;

typedef struct po33_sound_header {
  uint32_t range_start_raw, range_end_raw;
  uint8_t source_selector;
} po33_sound_header_t;

/* Decode requires an exact fixed-record length. Encode permits a larger
 * destination. All validation precedes writes. Input/output must not overlap. */
po_status_t po33_settings_decode(const uint8_t *data, size_t len, po33_settings_t *out);
po_status_t po33_settings_encode(const po33_settings_t *value, uint8_t *out, size_t capacity);
po_status_t po33_pattern_decode(const uint8_t *data, size_t len, po33_pattern_t *out);
po_status_t po33_pattern_encode(const po33_pattern_t *value, uint8_t *out, size_t capacity);
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

/* Expand one signed-order mu-law sample byte to linear PCM16. Sound packet
 * headers and padding are not samples; identifying those is the caller's job.
 * The nominal sample clock is PO33_SAMPLE_RATE_HZ. */
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
