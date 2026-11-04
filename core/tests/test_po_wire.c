#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po_common.h"
#include "po32.h"
#include "po33.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_legacy_frame(void) {
  const uint8_t expected[] = {0xc3, 0x0d, 0x88, 0x16, 0x98, 0x54, 0xd3, 0x5e, 0xd3, 0x4e, 0xf0};
  uint8_t frame[256], one = 1;
  size_t length;
  po32_builder_t builder;
  po_final_tail_t tail;
  po32_builder_init(&builder, frame, sizeof(frame));
  assert(po32_builder_append_packet(&builder, PO32_TAG_RESET, &one, 1, NULL) == PO_OK);
  assert(po32_builder_finish(&builder, &length) == PO_OK);
  assert(length == PO32_PREAMBLE_BYTES + sizeof(expected));
  assert(memcmp(frame + PO32_PREAMBLE_BYTES, expected, sizeof(expected)) == 0);
  assert(po32_frame_parse(frame, length, NULL, NULL, &tail) == PO_OK);
  assert(tail.final_state == 0x1fb3);
  po32_builder_reset(&builder);
  assert(po32_builder_append_packet(&builder, 0x1234, &one, 1, NULL) == PO_OK);
  assert(po32_builder_finish(&builder, &length) == PO_OK);
  assert(po32_frame_parse(frame, length, NULL, NULL, NULL) == PO_ERR_FRAME);
}

static void test_legacy_types(void) {
  enum po32_status old_status = PO_OK;
  struct po32_packet old_packet = {0};
  struct po32_packet_trailer old_trailer = {0};
  struct po32_final_tail old_tail = {0};
  po_packet_t packet = old_packet;
  po_packet_trailer_t trailer = old_trailer;
  po_final_tail_t tail = old_tail;
  po_status_t status = old_status;
  assert(status == PO32_OK && packet.payload_len == 0 && trailer.state == 0 &&
         tail.final_state == 0);
}

int main(void) {
  test_legacy_types();
  test_legacy_frame();
  puts("Shared wire compatibility tests passed");
  return 0;
}
