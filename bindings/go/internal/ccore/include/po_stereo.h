#ifndef PO_STEREO_H
#define PO_STEREO_H
#include "po_common.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Shared DQPSK transport for PO-33/133 and PO-35/137 backups. This layer
 * transports wire bytes; a model-specific parser must validate the frame. */
typedef struct po_stereo_modulator {
  const uint8_t *wire;
  size_t wire_bytes;
  size_t position, total_frames, active_frames, lead_frames;
  uint64_t symbol;
  uint32_t sample_rate;
  unsigned phase[2], next_phase[2];
  int initialized;
} po_stereo_modulator_t;

/* Borrow wire while the state may render or reset, including later renders
 * after reset. Keep it alive and immutable. Nonempty input is required, but its
 * packet grammar is not checked. Supported rates: 44100, 48000, 96000 Hz.
 * Output has 0.25 s silence on each side and a 128-symbol final phase hold.
 * Linear quadrant transitions approximate the observed capture; generated
 * audio is not a claim of exact vendor waveform or physical-device acceptance.
 * Failed initialization leaves an uninitialized, non-done state. No heap or
 * caller output is used. Do not modify state fields directly. */
po_status_t po_stereo_modulator_init(po_stereo_modulator_t *state, const uint8_t *wire,
                                     size_t wire_bytes, uint32_t sample_rate);
po_status_t po_stereo_modulator_reset(po_stereo_modulator_t *state);
size_t po_stereo_modulator_remaining(const po_stereo_modulator_t *state);
int po_stereo_modulator_done(const po_stereo_modulator_t *state);

/* Output interleaved L/R floats, nominal peak amplitude 0.6. Capacity and
 * *frames_written are STEREO FRAMES, not individual floats. frames_written is
 * required and becomes zero on error. Zero capacity is a successful no-op and
 * permits NULL output. Chunk boundaries do not change the waveform. State,
 * wire, output and frames_written must not overlap. */
po_status_t po_stereo_modulator_render_f32(po_stereo_modulator_t *state, float *interleaved,
                                           size_t frame_capacity, size_t *frames_written);

typedef enum po_stereo_channel_order {
  PO_STEREO_CHANNEL_NORMAL = 0,
  PO_STEREO_CHANNEL_SWAPPED = 1
} po_stereo_channel_order_t;

typedef struct po_stereo_workspace {
  double *scalars;
  size_t scalar_count;
  uint8_t *bytes;
  size_t byte_count;
} po_stereo_workspace_t;

/* RX uses forward/backward 200 Hz audio and 3 Hz timing-pilot filters; it is
 * explicitly one-shot, not streaming. Accepted rates: 32000..192000 Hz.
 * Requirements are element counts (double, byte, byte), not storage sizes.
 * All result pointers are required and zeroed on failure. Zero frames needs
 * zero storage. Overflow and unsupported rates return RANGE. */
po_status_t po_stereo_decode_requirements(size_t frame_count, uint32_t sample_rate,
                                          size_t *scalar_count, size_t *byte_count,
                                          size_t *wire_capacity);

/* Immutable interleaved L/R float input, all samples finite. Workspace arrays,
 * input, output, workspace descriptor and out_len must not overlap. Scratch
 * contents are unspecified after calling. Finds 4092-byte counter plus sync
 * and copies remaining raw wire bytes, possibly past the model's final packet.
 * Only a model parser can establish completion. No preamble returns FRAME.
 * out_len is required; zero on error except insufficient wire capacity, when
 * it receives required length. Output is untouched on every error. NULL
 * output with zero capacity can query actual recovered length. Empty or short
 * input returns FRAME; it does not establish completion. */
po_status_t po_stereo_decode_f32(const float *interleaved, size_t frame_count, uint32_t sample_rate,
                                 po_stereo_channel_order_t order, po_stereo_workspace_t *workspace,
                                 uint8_t *out_wire, size_t wire_capacity, size_t *out_len);
#ifdef __cplusplus
}
#endif
#endif
