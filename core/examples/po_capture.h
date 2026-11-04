#ifndef PO_CAPTURE_H
#define PO_CAPTURE_H
#include "po_stereo.h"
/* Hosted WAV/file allocation adapter; not part of the freestanding API. */
typedef struct po_audio {
  float *samples; /* Interleaved; channel values are never combined. */
  size_t frames;
  uint32_t rate;
  uint16_t channels;
} po_audio_t;
/* Read nonempty mono/stereo PCM16/24/32 or float32 WAV, including extensible
 * WAV, at 8000..192000 Hz. Samples must be finite; no clipping or normalization
 * is applied beyond integer PCM scaling. Output must be empty. Failure leaves
 * it empty; caller frees samples after success. */
int po_audio_read(const char *path, po_audio_t *out);

typedef struct po_capture {
  float *samples;
  size_t frames;
  uint32_t rate;
} po_capture_t;
/* Stereo modem capture: 32000..192000 Hz, at least half a second.
 * Output must be empty. Caller frees samples after a successful read. */
int po_capture_read(const char *path, po_capture_t *out);
int po_capture_decode(const po_capture_t *capture, po_stereo_channel_order_t order,
                      uint8_t **out_wire, size_t *out_length);
#endif
