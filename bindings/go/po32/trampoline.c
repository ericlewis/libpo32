#include "po32.h"
#include "_cgo_export.h"

static int go_po32_callback_trampoline(const po32_packet_t *packet, void *user) {
  return (int)goPO32CallbackBridge((po32_packet_t *)packet, user);
}

po32_status_t go_po32_frame_parse(const uint8_t *frame, size_t frame_len, void *user,
                                  po32_final_tail_t *out_tail) {
  return po32_frame_parse(frame, frame_len, go_po32_callback_trampoline, user, out_tail);
}

po32_status_t go_po32_demodulator_push(po32_demodulator_t *d, const float *samples, size_t count,
                                       void *user) {
  return po32_demodulator_push(d, samples, count, user != NULL ? go_po32_callback_trampoline : NULL,
                               user);
}

void go_po32_demodulator_flush(po32_demodulator_t *d, void *user) {
  po32_demodulator_flush(d, user != NULL ? go_po32_callback_trampoline : NULL, user);
}
