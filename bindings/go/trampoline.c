#include "po32.h"
#include "_cgo_export.h"

static int cCallbackTrampoline(const po32_packet_t *packet, void *user) {
  return (int)goCallbackBridge((po32_packet_t *)packet, user);
}

po32_status_t frame_parse_with_trampoline(const uint8_t *frame, size_t frame_len, void *user,
                                          po32_final_tail_t *out_tail) {
  return po32_frame_parse(frame, frame_len, cCallbackTrampoline, user, out_tail);
}
