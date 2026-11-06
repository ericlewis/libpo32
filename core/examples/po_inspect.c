/* Hosted inspection of one complete, explicitly selected backup frame. */
#include "po_frame_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  po_frame_family_t family;
  po_frame_report_t report = {0};
  uint8_t *bytes = NULL;
  size_t length = 0;
  int rc = 1;

  if (argc != 4 || strcmp(argv[1], "--family") != 0 || !po_frame_parse_family(argv[2], &family)) {
    fprintf(stderr, "Usage: %s --family po33|po35 frame.bin\n", argv[0]);
    return 2;
  }
  if (!po_frame_read_file(argv[3], &bytes, &length)) {
    fprintf(stderr, "Cannot read frame file: %s\n", argv[3]);
    goto cleanup;
  }
  if (!po_frame_report_collect(family, bytes, length, &report)) {
    fprintf(stderr, "Cannot inspect %s frame: %s\n", po_frame_family_name(family), argv[3]);
    goto cleanup;
  }
  if (!po_frame_report_is_exact_frame(&report)) {
    fprintf(stderr, "Input is not one complete %s frame: %s\n", po_frame_family_name(family),
            argv[3]);
    goto cleanup;
  }
  if (!po_frame_report_write_text(stdout, &report) || fflush(stdout) != 0) {
    fprintf(stderr, "Cannot write frame report.\n");
    goto cleanup;
  }
  rc = 0;

cleanup:
  po_frame_report_destroy(&report);
  free(bytes);
  return rc;
}
