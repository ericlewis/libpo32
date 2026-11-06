#ifndef PO_FRAME_REPORT_H
#define PO_FRAME_REPORT_H

#include "po_common.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hosted, read-only summaries over existing complete-frame parsers. They do
 * not recover, combine, or alter captured bytes. */
typedef enum po_frame_family { PO_FRAME_FAMILY_PO33, PO_FRAME_FAMILY_PO35 } po_frame_family_t;

typedef enum po_frame_selector_kind {
  PO_FRAME_SELECTOR_PATTERN,
  PO_FRAME_SELECTOR_SOUND,
  PO_FRAME_SELECTOR_SPEECH,
  PO_FRAME_SELECTOR_COUNT
} po_frame_selector_kind_t;

/* Each raw selector value is counted independently. invalid_packets records
 * selector-tag packets whose payload shape does not expose a selector value. */
typedef struct po_frame_selector_topology {
  size_t occurrences[256];
  size_t invalid_packets;
} po_frame_selector_topology_t;

/* One summary per distinct unrecognized packet tag, sorted by raw tag code. */
typedef struct po_frame_unknown_tag {
  uint16_t tag_code;
  size_t packet_count;
  size_t payload_bytes;
} po_frame_unknown_tag_t;

typedef struct po_frame_report {
  po_frame_family_t family;
  po_status_t parser_status;
  size_t input_bytes;
  size_t consumed_bytes;
  /* Bytes after a complete frame only. */
  size_t trailing_bytes;
  /* All bytes not consumed by the parser, including trailing bytes after END. */
  size_t unparsed_bytes;
  size_t packet_count;
  uint16_t final_state;
  int complete;
  int stopped;
  po_frame_selector_topology_t selectors[PO_FRAME_SELECTOR_COUNT];
  po_frame_unknown_tag_t *unknown_tags;
  size_t unknown_tag_count;
  size_t unknown_packet_count;
  size_t unknown_payload_bytes;
} po_frame_report_t;

/* Load one raw byte file into hosted caller-owned storage. Output pointers are
 * required and are cleared on failure; caller frees bytes after success. */
int po_frame_read_file(const char *path, uint8_t **out_bytes, size_t *out_length);

/* Collect a report for one explicitly selected family. out must be zeroed or
 * destroyed before use. A parser failure is represented in parser_status and
 * still returns 1 with its validated-prefix facts. Returns 0 only for an
 * invalid family/output lifecycle or hosted allocation failure. */
int po_frame_report_collect(po_frame_family_t family, const uint8_t *frame, size_t frame_len,
                            po_frame_report_t *out);

/* Frees hosted summary memory and resets report to its empty state. */
void po_frame_report_destroy(po_frame_report_t *report);

/* Emit deterministic one-line reports. Return 1 on a write setup success and
 * 0 for invalid arguments or a stream write failure. */
int po_frame_report_write_json(FILE *stream, const po_frame_report_t *report);
int po_frame_report_write_text(FILE *stream, const po_frame_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
