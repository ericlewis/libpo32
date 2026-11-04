#ifndef PO_CAPTURE_H
#define PO_CAPTURE_H
#include "po_stereo.h"
/* Hosted WAV/file allocation adapter; not part of the freestanding API. */
typedef struct po_capture {
  float *samples;
  size_t frames;
  uint32_t rate;
} po_capture_t;
/* Output must be empty. Caller frees samples after a successful read. */
int po_capture_read(const char *path, po_capture_t *out);
int po_capture_decode(const po_capture_t *capture, po_stereo_channel_order_t order,
                      uint8_t **out_wire, size_t *out_length);
#endif
