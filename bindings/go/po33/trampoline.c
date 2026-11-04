#include "po33.h"
#include "_cgo_export.h"

static int po33_go_packet_callback(const po_packet_t *packet, void *user) {
  return po33GoPacketCallback((po_packet_t *)packet, *(uintptr_t *)user);
}

po_status_t po33_go_frame_parse(const uint8_t *frame, size_t frame_len, uintptr_t handle,
                                po33_decode_result_t *result) {
  return po33_frame_parse(frame, frame_len, handle ? po33_go_packet_callback : NULL,
                          handle ? &handle : NULL, result);
}
