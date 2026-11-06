/* Hosted readback evidence for one explicit raw frame and stereo capture. */
#include "po_capture.h"
#include "po_frame_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PO_TRANSFER_VERIFY_ORDER_COUNT = 2 };

typedef struct po_transfer_verify_candidate {
  po_stereo_channel_order_t order;
  uint8_t *wire;
  size_t wire_bytes;
  po_frame_report_t report;
  int recovered;
  int exact;
} po_transfer_verify_candidate_t;

static const po_stereo_channel_order_t po_transfer_verify_orders[PO_TRANSFER_VERIFY_ORDER_COUNT] = {
    PO_STEREO_CHANNEL_NORMAL,
    PO_STEREO_CHANNEL_SWAPPED,
};

static const char *po_transfer_verify_family_name(po_frame_family_t family) {
  switch (family) {
  case PO_FRAME_FAMILY_PO33:
    return "po33";
  case PO_FRAME_FAMILY_PO35:
    return "po35";
  default:
    return NULL;
  }
}

static int po_transfer_verify_parse_family(const char *name, po_frame_family_t *family) {
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

static const char *po_transfer_verify_order_name(po_stereo_channel_order_t order) {
  switch (order) {
  case PO_STEREO_CHANNEL_NORMAL:
    return "normal";
  case PO_STEREO_CHANNEL_SWAPPED:
    return "swapped";
  default:
    return NULL;
  }
}

static const char *po_transfer_verify_status_name(po_status_t status) {
  switch (status) {
  case PO_OK:
    return "ok";
  case PO_ERR_INVALID_ARG:
    return "invalid-argument";
  case PO_ERR_RANGE:
    return "range";
  case PO_ERR_BUFFER_TOO_SMALL:
    return "buffer-too-small";
  case PO_ERR_FRAME:
    return "frame";
  case PO_ERR_PARSE:
    return "parse";
  default:
    return "unknown";
  }
}

static int po_transfer_verify_reference_complete(const po_frame_report_t *report) {
  return report != NULL && report->parser_status == PO_OK && report->complete && !report->stopped &&
         report->consumed_bytes == report->input_bytes;
}

static int po_transfer_verify_candidate_complete(const po_frame_report_t *report) {
  return report != NULL && report->parser_status == PO_OK && report->complete && !report->stopped;
}

static const char *po_transfer_verify_parser_name(const po_transfer_verify_candidate_t *candidate) {
  if (!candidate->recovered)
    return "not-run";
  if (po_transfer_verify_candidate_complete(&candidate->report))
    return "complete";
  if (candidate->report.parser_status != PO_OK)
    return po_transfer_verify_status_name(candidate->report.parser_status);
  if (candidate->report.stopped)
    return "stopped";
  if (candidate->report.complete)
    return "trailing";
  return "incomplete";
}

static void po_transfer_verify_candidate_destroy(po_transfer_verify_candidate_t *candidate) {
  if (candidate == NULL)
    return;
  free(candidate->wire);
  po_frame_report_destroy(&candidate->report);
  *candidate = (po_transfer_verify_candidate_t){0};
}

static int po_transfer_verify_collect_candidate(const po_capture_t *capture,
                                                po_frame_family_t family, const uint8_t *reference,
                                                size_t reference_bytes,
                                                po_stereo_channel_order_t order,
                                                po_transfer_verify_candidate_t *out) {
  if (capture == NULL || reference == NULL || out == NULL)
    return 0;
  *out = (po_transfer_verify_candidate_t){.order = order};
  if (!po_capture_decode(capture, order, &out->wire, &out->wire_bytes))
    return 1;
  out->recovered = 1;
  if (!po_frame_report_collect(family, out->wire, out->wire_bytes, &out->report)) {
    po_transfer_verify_candidate_destroy(out);
    return 0;
  }
  out->exact = po_transfer_verify_candidate_complete(&out->report) &&
               out->report.consumed_bytes == reference_bytes &&
               memcmp(out->wire, reference, reference_bytes) == 0;
  return 1;
}

static int po_transfer_verify_write_report(FILE *stream, po_frame_family_t family,
                                           size_t reference_bytes,
                                           const po_transfer_verify_candidate_t *candidates,
                                           size_t candidate_count, size_t exact_count) {
  const char *family_name = po_transfer_verify_family_name(family);
  if (stream == NULL || family_name == NULL || candidates == NULL ||
      fprintf(stream, "family=%s reference_bytes=%zu\n", family_name, reference_bytes) < 0)
    return 0;
  for (size_t index = 0; index < candidate_count; ++index) {
    const po_transfer_verify_candidate_t *candidate = candidates + index;
    const char *order_name = po_transfer_verify_order_name(candidate->order);
    if (order_name == NULL ||
        fprintf(stream,
                "candidate channel_order=%s transport=%s parser=%s exact=%s recovered_bytes=%zu "
                "frame_bytes=%zu trailing_bytes=%zu\n",
                order_name, candidate->recovered ? "recovered" : "not-recovered",
                po_transfer_verify_parser_name(candidate), candidate->exact ? "yes" : "no",
                candidate->wire_bytes, candidate->report.consumed_bytes,
                candidate->report.trailing_bytes) < 0)
      return 0;
  }
  return fprintf(stream, "verification=%s matches=%zu selection=none\n",
                 exact_count ? "exact" : "not-exact", exact_count) >= 0;
}

int main(int argc, char **argv) {
  po_frame_family_t family;
  po_frame_report_t reference_report = {0};
  po_capture_t capture = {0};
  po_transfer_verify_candidate_t candidates[PO_TRANSFER_VERIFY_ORDER_COUNT] = {{0}};
  uint8_t *reference = NULL;
  size_t reference_bytes = 0u;
  size_t exact_count = 0u;
  int rc = 1;

  if (argc != 5 || strcmp(argv[1], "--family") != 0 ||
      !po_transfer_verify_parse_family(argv[2], &family)) {
    fprintf(stderr, "Usage: %s --family po33|po35 reference.bin capture.wav\n", argv[0]);
    return 2;
  }
  if (!po_frame_read_file(argv[3], &reference, &reference_bytes)) {
    fprintf(stderr, "Cannot read reference frame: %s\n", argv[3]);
    goto cleanup;
  }
  if (!po_frame_report_collect(family, reference, reference_bytes, &reference_report)) {
    fprintf(stderr, "Cannot inspect reference frame: %s\n", argv[3]);
    goto cleanup;
  }
  if (!po_transfer_verify_reference_complete(&reference_report)) {
    fprintf(stderr, "Reference is not one complete %s frame: %s\n",
            po_transfer_verify_family_name(family), argv[3]);
    goto cleanup;
  }
  if (!po_capture_read(argv[4], &capture)) {
    fprintf(stderr, "Cannot read capture WAV: %s\n", argv[4]);
    goto cleanup;
  }
  for (size_t index = 0; index < PO_TRANSFER_VERIFY_ORDER_COUNT; ++index) {
    if (!po_transfer_verify_collect_candidate(&capture, family, reference, reference_bytes,
                                              po_transfer_verify_orders[index],
                                              candidates + index)) {
      fprintf(stderr, "Cannot inspect recovered capture candidate.\n");
      goto cleanup;
    }
    if (candidates[index].exact)
      ++exact_count;
  }
  if (!po_transfer_verify_write_report(stdout, family, reference_bytes, candidates,
                                       PO_TRANSFER_VERIFY_ORDER_COUNT, exact_count) ||
      fflush(stdout) != 0) {
    fprintf(stderr, "Cannot write transfer verification.\n");
    goto cleanup;
  }
  rc = exact_count ? 0 : 1;

cleanup:
  for (size_t index = 0; index < PO_TRANSFER_VERIFY_ORDER_COUNT; ++index)
    po_transfer_verify_candidate_destroy(candidates + index);
  free(capture.samples);
  po_frame_report_destroy(&reference_report);
  free(reference);
  return rc;
}
