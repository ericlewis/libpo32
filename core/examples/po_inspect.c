/* Hosted inspection of one complete, explicitly selected backup frame. */
#include "po_frame_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *po_inspect_family_name(po_frame_family_t family) {
  switch (family) {
  case PO_FRAME_FAMILY_PO33:
    return "po33";
  case PO_FRAME_FAMILY_PO35:
    return "po35";
  default:
    return NULL;
  }
}

static int po_inspect_parse_family(const char *name, po_frame_family_t *family) {
  if (name == NULL || family == NULL)
    return 0;
  if (strcmp(name, "po33") == 0) {
    *family = PO_FRAME_FAMILY_PO33;
    return 1;
  }
  if (strcmp(name, "po35") == 0) {
    *family = PO_FRAME_FAMILY_PO35;
    return 1;
  }
  return 0;
}

static int po_inspect_report_is_complete(const po_frame_report_t *report) {
  return report->parser_status == PO_OK && report->complete && !report->stopped &&
         report->consumed_bytes == report->input_bytes;
}

int main(int argc, char **argv) {
  po_frame_family_t family;
  po_frame_report_t report = {0};
  uint8_t *bytes = NULL;
  size_t length = 0;
  int rc = 1;

  if (argc != 4 || strcmp(argv[1], "--family") != 0 || !po_inspect_parse_family(argv[2], &family)) {
    fprintf(stderr, "Usage: %s --family po33|po35 frame.bin\n", argv[0]);
    return 2;
  }
  if (!po_frame_read_file(argv[3], &bytes, &length)) {
    fprintf(stderr, "Cannot read frame file: %s\n", argv[3]);
    goto cleanup;
  }
  if (!po_frame_report_collect(family, bytes, length, &report)) {
    fprintf(stderr, "Cannot inspect %s frame: %s\n", po_inspect_family_name(family), argv[3]);
    goto cleanup;
  }
  if (!po_inspect_report_is_complete(&report)) {
    fprintf(stderr, "Input is not one complete %s frame: %s\n", po_inspect_family_name(family),
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
