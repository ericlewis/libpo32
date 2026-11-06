#include "po_frame_report.h"

#include "po33.h"
#include "po35.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct po_frame_unknown_accumulator {
  size_t packet_count;
  size_t payload_bytes;
} po_frame_unknown_accumulator_t;

typedef struct po_frame_collector {
  po_frame_family_t family;
  po_frame_report_t *report;
  po_frame_unknown_accumulator_t *unknown_by_tag;
  int failed;
} po_frame_collector_t;

static int po_frame_report_is_known_po33_tag(uint16_t tag_code) {
  switch (tag_code) {
  case PO33_TAG_BEGIN:
  case PO33_TAG_SETTINGS:
  case PO33_TAG_PATTERN_SELECT:
  case PO33_TAG_PATTERN_DATA:
  case PO33_TAG_SOUND_SELECT:
  case PO33_TAG_SOUND_DATA:
  case PO33_TAG_END:
    return 1;
  default:
    return 0;
  }
}

static int po_frame_report_is_known_po35_tag(uint16_t tag_code) {
  switch (tag_code) {
  case PO35_TAG_PATCH:
  case PO35_TAG_TABLE_7BE7:
  case PO35_TAG_TABLE_1C7F:
  case PO35_TAG_PATTERN_EMPTY:
  case PO35_TAG_PATTERN:
  case PO35_TAG_SPEECH_HEADER:
  case PO35_TAG_SPEECH_DATA:
  case PO35_TAG_SPEECH_END:
  case PO35_TAG_SETTINGS:
    return 1;
  default:
    return 0;
  }
}

static int po_frame_report_add_selector(po_frame_report_t *report, po_frame_selector_kind_t kind,
                                        const po_packet_t *packet, size_t expected_length) {
  po_frame_selector_topology_t *topology = report->selectors + kind;
  if (packet->payload_len != expected_length) {
    if (topology->invalid_packets == SIZE_MAX)
      return 0;
    ++topology->invalid_packets;
    return 1;
  }
  if (topology->occurrences[packet->payload[0]] == SIZE_MAX)
    return 0;
  ++topology->occurrences[packet->payload[0]];
  return 1;
}

static int po_frame_report_add_unknown(po_frame_collector_t *collector, const po_packet_t *packet) {
  po_frame_report_t *report = collector->report;
  po_frame_unknown_accumulator_t *tag;
  if (report->unknown_packet_count == SIZE_MAX ||
      report->unknown_payload_bytes > SIZE_MAX - packet->payload_len)
    return 0;
  if (collector->unknown_by_tag == NULL) {
    collector->unknown_by_tag = (po_frame_unknown_accumulator_t *)calloc(
        (size_t)UINT16_MAX + 1u, sizeof(*collector->unknown_by_tag));
    if (collector->unknown_by_tag == NULL)
      return 0;
  }
  tag = collector->unknown_by_tag + packet->tag_code;
  if (tag->packet_count == SIZE_MAX || tag->payload_bytes > SIZE_MAX - packet->payload_len)
    return 0;
  if (tag->packet_count == 0u)
    ++report->unknown_tag_count;
  ++tag->packet_count;
  tag->payload_bytes += packet->payload_len;
  ++report->unknown_packet_count;
  report->unknown_payload_bytes += packet->payload_len;
  return 1;
}

static int po_frame_report_collect_packet(const po_packet_t *packet, void *user) {
  po_frame_collector_t *collector = (po_frame_collector_t *)user;
  po_frame_report_t *report = collector->report;
  int ok = 1;
  if (collector->family == PO_FRAME_FAMILY_PO33) {
    switch (packet->tag_code) {
    case PO33_TAG_PATTERN_SELECT:
      ok = po_frame_report_add_selector(report, PO_FRAME_SELECTOR_PATTERN, packet, 1u);
      break;
    case PO33_TAG_SOUND_SELECT:
      ok = po_frame_report_add_selector(report, PO_FRAME_SELECTOR_SOUND, packet, 1u);
      break;
    default:
      if (!po_frame_report_is_known_po33_tag(packet->tag_code))
        ok = po_frame_report_add_unknown(collector, packet);
      break;
    }
  } else {
    switch (packet->tag_code) {
    case PO35_TAG_PATTERN_EMPTY:
      ok = po_frame_report_add_selector(report, PO_FRAME_SELECTOR_PATTERN, packet, 1u);
      break;
    case PO35_TAG_PATTERN:
      ok = po_frame_report_add_selector(report, PO_FRAME_SELECTOR_PATTERN, packet,
                                        PO35_PATTERN_BYTES);
      break;
    case PO35_TAG_SPEECH_HEADER:
      ok = po_frame_report_add_selector(report, PO_FRAME_SELECTOR_SPEECH, packet,
                                        PO35_SPEECH_HEADER_BYTES);
      break;
    default:
      if (!po_frame_report_is_known_po35_tag(packet->tag_code))
        ok = po_frame_report_add_unknown(collector, packet);
      break;
    }
  }
  if (!ok) {
    collector->failed = 1;
    return 1;
  }
  return 0;
}

static int po_frame_report_finalize_unknown_tags(po_frame_collector_t *collector) {
  po_frame_report_t *report = collector->report;
  size_t index = 0u;
  if (report->unknown_tag_count == 0u)
    return 1;
  if (report->unknown_tag_count > SIZE_MAX / sizeof(*report->unknown_tags))
    return 0;
  report->unknown_tags =
      (po_frame_unknown_tag_t *)malloc(report->unknown_tag_count * sizeof(*report->unknown_tags));
  if (report->unknown_tags == NULL)
    return 0;
  for (uint32_t tag_code = 0u; tag_code <= UINT16_MAX; ++tag_code) {
    const po_frame_unknown_accumulator_t *tag = collector->unknown_by_tag + tag_code;
    if (tag->packet_count == 0u)
      continue;
    report->unknown_tags[index++] = (po_frame_unknown_tag_t){
        .tag_code = (uint16_t)tag_code,
        .packet_count = tag->packet_count,
        .payload_bytes = tag->payload_bytes,
    };
  }
  return index == report->unknown_tag_count;
}

void po_frame_report_destroy(po_frame_report_t *report) {
  if (report == NULL)
    return;
  free(report->unknown_tags);
  *report = (po_frame_report_t){0};
}

int po_frame_report_collect(po_frame_family_t family, const uint8_t *frame, size_t frame_len,
                            po_frame_report_t *out) {
  po_frame_report_t report = {0};
  po_frame_collector_t collector;
  if (out == NULL || out->unknown_tags != NULL ||
      (family != PO_FRAME_FAMILY_PO33 && family != PO_FRAME_FAMILY_PO35))
    return 0;
  report.family = family;
  report.input_bytes = frame_len;
  collector = (po_frame_collector_t){
      .family = family,
      .report = &report,
  };
  if (family == PO_FRAME_FAMILY_PO33) {
    po33_decode_result_t result;
    report.parser_status =
        po33_frame_parse(frame, frame_len, po_frame_report_collect_packet, &collector, &result);
    report.packet_count = result.packet_count;
    report.consumed_bytes = result.frame_bytes;
    report.final_state = result.final_state;
    report.complete = result.done;
    report.stopped = result.stopped;
  } else {
    po35_decode_result_t result;
    report.parser_status =
        po35_frame_parse(frame, frame_len, po_frame_report_collect_packet, &collector, &result);
    report.packet_count = result.packet_count;
    report.consumed_bytes = result.frame_bytes;
    report.final_state = result.final_state;
    report.complete = result.done;
    report.stopped = result.stopped;
  }
  if (collector.failed) {
    free(collector.unknown_by_tag);
    po_frame_report_destroy(&report);
    return 0;
  }
  if (report.consumed_bytes <= frame_len)
    report.unparsed_bytes = frame_len - report.consumed_bytes;
  if (report.complete)
    report.trailing_bytes = report.unparsed_bytes;
  if (!po_frame_report_finalize_unknown_tags(&collector)) {
    free(collector.unknown_by_tag);
    po_frame_report_destroy(&report);
    return 0;
  }
  free(collector.unknown_by_tag);
  *out = report;
  return 1;
}

static const char *po_frame_report_family_name(po_frame_family_t family) {
  switch (family) {
  case PO_FRAME_FAMILY_PO33:
    return "po33";
  case PO_FRAME_FAMILY_PO35:
    return "po35";
  default:
    return NULL;
  }
}

static const char *po_frame_report_status_name(po_status_t status) {
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

static int po_frame_report_writef(FILE *stream, const char *format, ...) {
  va_list arguments;
  int written;
  va_start(arguments, format);
  written = vfprintf(stream, format, arguments);
  va_end(arguments);
  return written >= 0;
}

static int po_frame_report_write_selector_json(FILE *stream,
                                               const po_frame_selector_topology_t *topology) {
  int first = 1;
  if (!po_frame_report_writef(stream, "["))
    return 0;
  for (size_t value = 0; value < sizeof(topology->occurrences) / sizeof(topology->occurrences[0]);
       ++value) {
    if (topology->occurrences[value] == 0)
      continue;
    if (!po_frame_report_writef(stream, "%s{\"value\":%zu,\"occurrences\":%zu}", first ? "" : ",",
                                value, topology->occurrences[value]))
      return 0;
    first = 0;
  }
  return po_frame_report_writef(stream, "]");
}

static int po_frame_report_write_unknown_json(FILE *stream, const po_frame_report_t *report) {
  if (!po_frame_report_writef(stream, "["))
    return 0;
  for (size_t index = 0; index < report->unknown_tag_count; ++index) {
    const po_frame_unknown_tag_t *tag = report->unknown_tags + index;
    if (!po_frame_report_writef(
            stream, "%s{\"tag\":\"0x%04x\",\"packets\":%zu,\"payload_bytes\":%zu}",
            index ? "," : "", (unsigned)tag->tag_code, tag->packet_count, tag->payload_bytes))
      return 0;
  }
  return po_frame_report_writef(stream, "]");
}

static int po_frame_report_write_selector_text(FILE *stream,
                                               const po_frame_selector_topology_t *topology) {
  int first = 1;
  for (size_t value = 0; value < sizeof(topology->occurrences) / sizeof(topology->occurrences[0]);
       ++value) {
    if (topology->occurrences[value] == 0)
      continue;
    if (!po_frame_report_writef(stream, "%s%zux%zu", first ? "" : ",", value,
                                topology->occurrences[value]))
      return 0;
    first = 0;
  }
  return !first || po_frame_report_writef(stream, "-");
}

static int po_frame_report_write_unknown_text(FILE *stream, const po_frame_report_t *report) {
  if (!report->unknown_tag_count)
    return po_frame_report_writef(stream, "-");
  for (size_t index = 0; index < report->unknown_tag_count; ++index) {
    const po_frame_unknown_tag_t *tag = report->unknown_tags + index;
    if (!po_frame_report_writef(stream, "%s0x%04xx%zu/%zu", index ? "," : "",
                                (unsigned)tag->tag_code, tag->packet_count, tag->payload_bytes))
      return 0;
  }
  return 1;
}

int po_frame_report_write_json(FILE *stream, const po_frame_report_t *report) {
  const char *family;
  if (stream == NULL || report == NULL ||
      (report->unknown_tag_count != 0 && report->unknown_tags == NULL) ||
      (family = po_frame_report_family_name(report->family)) == NULL)
    return 0;
  if (!po_frame_report_writef(
          stream,
          "{\"family\":\"%s\",\"parser_status\":\"%s\",\"complete\":%s,\"stopped\":%s,"
          "\"packet_count\":%zu,\"input_bytes\":%zu,\"consumed_bytes\":%zu,\"trailing_bytes\":%zu,"
          "\"unparsed_bytes\":%zu,\"final_state\":\"0x%04x\",\"selectors\":{\"patterns\":",
          family, po_frame_report_status_name(report->parser_status),
          report->complete ? "true" : "false", report->stopped ? "true" : "false",
          report->packet_count, report->input_bytes, report->consumed_bytes, report->trailing_bytes,
          report->unparsed_bytes, (unsigned)report->final_state) ||
      !po_frame_report_write_selector_json(stream, report->selectors + PO_FRAME_SELECTOR_PATTERN) ||
      !po_frame_report_writef(stream, ",\"sounds\":") ||
      !po_frame_report_write_selector_json(stream, report->selectors + PO_FRAME_SELECTOR_SOUND) ||
      !po_frame_report_writef(stream, ",\"speech\":") ||
      !po_frame_report_write_selector_json(stream, report->selectors + PO_FRAME_SELECTOR_SPEECH) ||
      !po_frame_report_writef(
          stream,
          "},\"invalid_selectors\":{\"patterns\":%zu,\"sounds\":%zu,\"speech\":%zu},"
          "\"unknown_packets\":{\"count\":%zu,\"payload_bytes\":%zu,\"tags\":",
          report->selectors[PO_FRAME_SELECTOR_PATTERN].invalid_packets,
          report->selectors[PO_FRAME_SELECTOR_SOUND].invalid_packets,
          report->selectors[PO_FRAME_SELECTOR_SPEECH].invalid_packets, report->unknown_packet_count,
          report->unknown_payload_bytes) ||
      !po_frame_report_write_unknown_json(stream, report) ||
      !po_frame_report_writef(stream, "}}\n"))
    return 0;
  return 1;
}

int po_frame_report_write_text(FILE *stream, const po_frame_report_t *report) {
  const char *family;
  if (stream == NULL || report == NULL ||
      (report->unknown_tag_count != 0 && report->unknown_tags == NULL) ||
      (family = po_frame_report_family_name(report->family)) == NULL)
    return 0;
  if (!po_frame_report_writef(
          stream,
          "%s parser=%s complete=%s stopped=%s packets=%zu input=%zu consumed=%zu trailing=%zu "
          "unparsed=%zu state=0x%04x selectors=patterns[",
          family, po_frame_report_status_name(report->parser_status),
          report->complete ? "yes" : "no", report->stopped ? "yes" : "no", report->packet_count,
          report->input_bytes, report->consumed_bytes, report->trailing_bytes,
          report->unparsed_bytes, (unsigned)report->final_state) ||
      !po_frame_report_write_selector_text(stream, report->selectors + PO_FRAME_SELECTOR_PATTERN) ||
      !po_frame_report_writef(stream, "];sounds[") ||
      !po_frame_report_write_selector_text(stream, report->selectors + PO_FRAME_SELECTOR_SOUND) ||
      !po_frame_report_writef(stream, "];speech[") ||
      !po_frame_report_write_selector_text(stream, report->selectors + PO_FRAME_SELECTOR_SPEECH) ||
      !po_frame_report_writef(stream, "];invalid=%zu/%zu/%zu unknown=packets:%zu,bytes:%zu,tags[",
                              report->selectors[PO_FRAME_SELECTOR_PATTERN].invalid_packets,
                              report->selectors[PO_FRAME_SELECTOR_SOUND].invalid_packets,
                              report->selectors[PO_FRAME_SELECTOR_SPEECH].invalid_packets,
                              report->unknown_packet_count, report->unknown_payload_bytes) ||
      !po_frame_report_write_unknown_text(stream, report) || !po_frame_report_writef(stream, "]\n"))
    return 0;
  return 1;
}
