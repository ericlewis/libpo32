#include "po35.h"
#include "_cgo_export.h"

static int po35_go_packet_trampoline(const po_packet_t *packet, void *user) {
  return (int)po35GoPacketCallback((po_packet_t *)packet, (uintptr_t)user);
}

po_status_t po35_go_frame_parse(const uint8_t *frame, size_t length, uintptr_t handle,
                                po35_decode_result_t *result) {
  return po35_frame_parse(frame, length, handle ? po35_go_packet_trampoline : NULL, (void *)handle,
                          result);
}
