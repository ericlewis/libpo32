#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main po32_example_cli_main
#include "../examples/po32_example.c"
#undef main

static void test_fail_helpers(void) {
  assert(fail_message("x\n") == 1);
  assert(fail_status("context", -7) == 1);
}

static void test_packet_printer(void) {
  packet_printer_t printer = {0};
  po32_packet_t packet;
  int result;

  memset(&packet, 0, sizeof(packet));
  packet.tag_code = PO32_TAG_PATCH;
  packet.payload_len = 4u;
  packet.offset = 12u;

  result = print_packet(&packet, &printer);
  assert(result == 0);
  assert(printer.packet_count == 1);
}

static void test_example_main_success(void) {
  int result = po32_example_cli_main();
  assert(result == 0);
}

int main(void) {
  test_fail_helpers();
  test_packet_printer();
  test_example_main_success();
  return 0;
}
