/* Hosted structural comparison of two complete, explicitly selected backup frames. */
#include "po_frame_report.h"

#include "po33.h"
#include "po35.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct po_diff_packet_list {
  po_packet_t *items;
  size_t count;
  size_t capacity;
  int failed;
} po_diff_packet_list_t;

typedef enum po_diff_record_kind {
  PO_DIFF_RECORD_RAW,
  PO_DIFF_RECORD_PO33_SETTINGS,
  PO_DIFF_RECORD_PO33_PATTERN,
  PO_DIFF_RECORD_PO33_SOUND,
  PO_DIFF_RECORD_PO35_PATTERN,
  PO_DIFF_RECORD_PO35_SPEECH
} po_diff_record_kind_t;

typedef struct po_diff_record {
  po_diff_record_kind_t kind;
  uint16_t tag_code;
  uint8_t selector_raw;
  size_t packet_start;
  size_t packet_count;
  size_t payload_bytes;
} po_diff_record_t;

typedef struct po_diff_frame {
  uint8_t *bytes;
  size_t byte_count;
  po_diff_packet_list_t packets;
  po_diff_record_t *records;
  size_t record_count;
  size_t record_capacity;
} po_diff_frame_t;

typedef struct po_diff_bytes {
  size_t changed;
  size_t first_offset;
  uint8_t before_byte;
  uint8_t after_byte;
  int before_present;
  int after_present;
  int identical;
} po_diff_bytes_t;

typedef struct po_diff_payload {
  size_t changed;
  size_t first_index;
  size_t first_packet_index;
  size_t first_byte_index;
  uint8_t before_byte;
  uint8_t after_byte;
  int identical;
} po_diff_payload_t;

static const char *po_diff_family_name(po_frame_family_t family) {
  switch (family) {
  case PO_FRAME_FAMILY_PO33:
    return "po33";
  case PO_FRAME_FAMILY_PO35:
    return "po35";
  default:
    return NULL;
  }
}

static int po_diff_parse_family(const char *name, po_frame_family_t *family) {
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

static int po_diff_read_file(const char *path, uint8_t **out_bytes, size_t *out_length) {
  FILE *file = NULL;
  uint8_t *bytes = NULL;
  long end;
  size_t length;
  int ok = 0;

  if (path == NULL || out_bytes == NULL || out_length == NULL)
    return 0;
  *out_bytes = NULL;
  *out_length = 0;
  file = fopen(path, "rb");
  if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 0 ||
      (uintmax_t)end > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0)
    goto cleanup;
  length = (size_t)end;
  bytes = (uint8_t *)malloc(length ? length : 1u);
  if (bytes == NULL || (length != 0u && fread(bytes, 1, length, file) != length) || ferror(file))
    goto cleanup;
  *out_bytes = bytes;
  *out_length = length;
  bytes = NULL;
  ok = 1;

cleanup:
  free(bytes);
  if (file != NULL && fclose(file) != 0)
    ok = 0;
  if (!ok) {
    free(*out_bytes);
    *out_bytes = NULL;
    *out_length = 0;
  }
  return ok;
}

static void po_diff_frame_destroy(po_diff_frame_t *frame) {
  if (frame == NULL)
    return;
  free(frame->bytes);
  free(frame->packets.items);
  free(frame->records);
  *frame = (po_diff_frame_t){0};
}

static int po_diff_collect_packet(const po_packet_t *packet, void *user) {
  po_diff_packet_list_t *list = (po_diff_packet_list_t *)user;
  po_packet_t *items;
  size_t capacity;

  if (list->count == list->capacity) {
    capacity = list->capacity ? list->capacity * 2u : 16u;
    if (capacity < list->capacity || capacity > SIZE_MAX / sizeof(*items)) {
      list->failed = 1;
      return 1;
    }
    items = (po_packet_t *)realloc(list->items, capacity * sizeof(*items));
    if (items == NULL) {
      list->failed = 1;
      return 1;
    }
    list->items = items;
    list->capacity = capacity;
  }
  list->items[list->count++] = *packet;
  return 0;
}

static int po_diff_collect_packets(po_frame_family_t family, const uint8_t *bytes,
                                   size_t byte_count, po_diff_packet_list_t *out) {
  po_status_t status;

  if (bytes == NULL || out == NULL)
    return 0;
  *out = (po_diff_packet_list_t){0};
  if (family == PO_FRAME_FAMILY_PO33) {
    po33_decode_result_t result;
    status = po33_frame_parse(bytes, byte_count, po_diff_collect_packet, out, &result);
    if (status != PO_OK || out->failed || !result.done || result.stopped ||
        result.frame_bytes != byte_count)
      return 0;
  } else if (family == PO_FRAME_FAMILY_PO35) {
    po35_decode_result_t result;
    status = po35_frame_parse(bytes, byte_count, po_diff_collect_packet, out, &result);
    if (status != PO_OK || out->failed || !result.done || result.stopped ||
        result.frame_bytes != byte_count)
      return 0;
  } else {
    return 0;
  }
  return 1;
}

static int po_diff_record_append(po_diff_frame_t *frame, po_diff_record_kind_t kind,
                                 uint16_t tag_code, uint8_t selector_raw, size_t packet_start,
                                 size_t packet_count) {
  po_diff_record_t *records;
  po_diff_record_t *record;
  size_t payload_bytes = 0;
  size_t capacity;

  if (packet_start > frame->packets.count || packet_count > frame->packets.count - packet_start)
    return 0;
  for (size_t index = 0; index < packet_count; ++index) {
    size_t payload = frame->packets.items[packet_start + index].payload_len;
    if (payload > SIZE_MAX - payload_bytes)
      return 0;
    payload_bytes += payload;
  }
  if (frame->record_count == frame->record_capacity) {
    capacity = frame->record_capacity ? frame->record_capacity * 2u : 16u;
    if (capacity < frame->record_capacity || capacity > SIZE_MAX / sizeof(*records))
      return 0;
    records = (po_diff_record_t *)realloc(frame->records, capacity * sizeof(*records));
    if (records == NULL)
      return 0;
    frame->records = records;
    frame->record_capacity = capacity;
  }
  record = frame->records + frame->record_count++;
  *record = (po_diff_record_t){.kind = kind,
                               .tag_code = tag_code,
                               .selector_raw = selector_raw,
                               .packet_start = packet_start,
                               .packet_count = packet_count,
                               .payload_bytes = payload_bytes};
  return 1;
}

static int po_diff_build_po33_records(po_diff_frame_t *frame) {
  size_t index = 0;

  while (index < frame->packets.count) {
    const po_packet_t *packet = frame->packets.items + index;
    size_t end = index + 1u;

    if (packet->tag_code == PO33_TAG_BEGIN || packet->tag_code == PO33_TAG_END) {
      ++index;
      continue;
    }
    if (packet->tag_code == PO33_TAG_SETTINGS) {
      size_t payload_bytes = packet->payload_len;
      while (end < frame->packets.count &&
             frame->packets.items[end].tag_code == PO33_TAG_SETTINGS) {
        if (frame->packets.items[end].payload_len > SIZE_MAX - payload_bytes)
          return 0;
        payload_bytes += frame->packets.items[end].payload_len;
        ++end;
      }
      if (payload_bytes == PO33_SETTINGS_BYTES) {
        if (!po_diff_record_append(frame, PO_DIFF_RECORD_PO33_SETTINGS, packet->tag_code, 0, index,
                                   end - index))
          return 0;
      } else {
        if (!po_diff_record_append(frame, PO_DIFF_RECORD_RAW, packet->tag_code, 0, index, 1u))
          return 0;
        end = index + 1u;
      }
    } else if ((packet->tag_code == PO33_TAG_PATTERN_SELECT ||
                packet->tag_code == PO33_TAG_SOUND_SELECT) &&
               packet->payload_len == 1u) {
      uint16_t data_tag =
          packet->tag_code == PO33_TAG_PATTERN_SELECT ? PO33_TAG_PATTERN_DATA : PO33_TAG_SOUND_DATA;
      size_t payload_bytes = 0;
      while (end < frame->packets.count && frame->packets.items[end].tag_code == data_tag) {
        if (frame->packets.items[end].payload_len > SIZE_MAX - payload_bytes)
          return 0;
        payload_bytes += frame->packets.items[end].payload_len;
        ++end;
      }
      if ((data_tag == PO33_TAG_PATTERN_DATA && payload_bytes == PO33_PATTERN_BYTES) ||
          (data_tag == PO33_TAG_SOUND_DATA && end > index + 1u)) {
        po_diff_record_kind_t kind = data_tag == PO33_TAG_PATTERN_DATA ? PO_DIFF_RECORD_PO33_PATTERN
                                                                       : PO_DIFF_RECORD_PO33_SOUND;
        if (!po_diff_record_append(frame, kind, packet->tag_code, packet->payload[0], index,
                                   end - index))
          return 0;
      } else {
        if (!po_diff_record_append(frame, PO_DIFF_RECORD_RAW, packet->tag_code, 0, index, 1u))
          return 0;
        end = index + 1u;
      }
    } else if (!po_diff_record_append(frame, PO_DIFF_RECORD_RAW, packet->tag_code, 0, index, 1u)) {
      return 0;
    }
    index = end;
  }
  return 1;
}

static int po_diff_build_po35_records(po_diff_frame_t *frame) {
  size_t index = 0;

  while (index < frame->packets.count) {
    const po_packet_t *packet = frame->packets.items + index;
    size_t end = index + 1u;

    if ((packet->tag_code == PO35_TAG_PATTERN_EMPTY && packet->payload_len == 1u) ||
        (packet->tag_code == PO35_TAG_PATTERN && packet->payload_len == PO35_PATTERN_BYTES)) {
      if (!po_diff_record_append(frame, PO_DIFF_RECORD_PO35_PATTERN, packet->tag_code,
                                 packet->payload[0], index, 1u))
        return 0;
    } else if (packet->tag_code == PO35_TAG_SPEECH_HEADER &&
               packet->payload_len == PO35_SPEECH_HEADER_BYTES) {
      /* DC32 stores its record count as the same BE16 used by the public header codec. */
      size_t expected = (((size_t)packet->payload[1] << 8u) | (size_t)packet->payload[2]) *
                        PO35_SPEECH_RECORD_BYTES;
      size_t payload_bytes = 0;
      while (end < frame->packets.count &&
             frame->packets.items[end].tag_code == PO35_TAG_SPEECH_DATA) {
        if (frame->packets.items[end].payload_len > SIZE_MAX - payload_bytes)
          return 0;
        payload_bytes += frame->packets.items[end].payload_len;
        ++end;
      }
      if (end < frame->packets.count && frame->packets.items[end].tag_code == PO35_TAG_SPEECH_END &&
          frame->packets.items[end].payload_len == 0u && payload_bytes == expected) {
        ++end;
        if (!po_diff_record_append(frame, PO_DIFF_RECORD_PO35_SPEECH, packet->tag_code,
                                   packet->payload[0], index, end - index))
          return 0;
      } else {
        if (!po_diff_record_append(frame, PO_DIFF_RECORD_RAW, packet->tag_code, 0, index, 1u))
          return 0;
        end = index + 1u;
      }
    } else if (!po_diff_record_append(frame, PO_DIFF_RECORD_RAW, packet->tag_code, 0, index, 1u)) {
      return 0;
    }
    index = end;
  }
  return 1;
}

static int po_diff_build_records(po_frame_family_t family, po_diff_frame_t *frame) {
  if (family == PO_FRAME_FAMILY_PO33)
    return po_diff_build_po33_records(frame);
  if (family == PO_FRAME_FAMILY_PO35)
    return po_diff_build_po35_records(frame);
  return 0;
}

static int po_diff_packet_known(po_frame_family_t family, uint16_t tag_code) {
  if (family == PO_FRAME_FAMILY_PO33) {
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
  if (family == PO_FRAME_FAMILY_PO35) {
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
  return 0;
}

static void po_diff_compare_bytes(const po_diff_frame_t *before, const po_diff_frame_t *after,
                                  po_diff_bytes_t *out) {
  size_t common = before->byte_count < after->byte_count ? before->byte_count : after->byte_count;

  *out = (po_diff_bytes_t){.identical = 1};
  for (size_t index = 0; index < common; ++index) {
    if (before->bytes[index] == after->bytes[index])
      continue;
    if (out->identical) {
      out->first_offset = index;
      out->before_byte = before->bytes[index];
      out->after_byte = after->bytes[index];
      out->before_present = 1;
      out->after_present = 1;
      out->identical = 0;
    }
    ++out->changed;
  }
  if (before->byte_count != after->byte_count) {
    size_t extra = before->byte_count > after->byte_count ? before->byte_count - after->byte_count
                                                          : after->byte_count - before->byte_count;
    if (out->identical) {
      out->first_offset = common;
      out->before_present = common < before->byte_count;
      out->after_present = common < after->byte_count;
      if (out->before_present)
        out->before_byte = before->bytes[common];
      if (out->after_present)
        out->after_byte = after->bytes[common];
      out->identical = 0;
    }
    out->changed += extra;
  }
}

static int po_diff_packet_topology_equal(const po_diff_frame_t *before,
                                         const po_diff_frame_t *after, size_t *first_index) {
  size_t common =
      before->packets.count < after->packets.count ? before->packets.count : after->packets.count;

  for (size_t index = 0; index < common; ++index) {
    const po_packet_t *a = before->packets.items + index;
    const po_packet_t *b = after->packets.items + index;
    if (a->tag_code != b->tag_code || a->payload_len != b->payload_len) {
      *first_index = index;
      return 0;
    }
  }
  if (before->packets.count != after->packets.count) {
    *first_index = common;
    return 0;
  }
  return 1;
}

static void po_diff_compare_packet_payloads(const po_diff_frame_t *before,
                                            const po_diff_frame_t *after, po_diff_payload_t *out) {
  *out = (po_diff_payload_t){.identical = 1};
  for (size_t index = 0; index < before->packets.count; ++index) {
    const po_packet_t *a = before->packets.items + index;
    const po_packet_t *b = after->packets.items + index;
    if (memcmp(a->payload, b->payload, a->payload_len) == 0)
      continue;
    if (out->identical) {
      size_t byte_index = 0;
      while (a->payload[byte_index] == b->payload[byte_index])
        ++byte_index;
      out->first_index = index;
      out->first_packet_index = index;
      out->first_byte_index = byte_index;
      out->before_byte = a->payload[byte_index];
      out->after_byte = b->payload[byte_index];
      out->identical = 0;
    }
    ++out->changed;
  }
}

static int po_diff_record_signature_equal(const po_diff_frame_t *before, const po_diff_record_t *a,
                                          const po_diff_frame_t *after, const po_diff_record_t *b) {
  if (a->kind != b->kind || a->tag_code != b->tag_code || a->selector_raw != b->selector_raw ||
      a->packet_count != b->packet_count || a->payload_bytes != b->payload_bytes)
    return 0;
  for (size_t index = 0; index < a->packet_count; ++index) {
    const po_packet_t *left = before->packets.items + a->packet_start + index;
    const po_packet_t *right = after->packets.items + b->packet_start + index;
    if (left->tag_code != right->tag_code || left->payload_len != right->payload_len)
      return 0;
  }
  return 1;
}

static int po_diff_record_topology_equal(const po_diff_frame_t *before,
                                         const po_diff_frame_t *after, size_t *first_index) {
  size_t common =
      before->record_count < after->record_count ? before->record_count : after->record_count;

  for (size_t index = 0; index < common; ++index) {
    if (!po_diff_record_signature_equal(before, before->records + index, after,
                                        after->records + index)) {
      *first_index = index;
      return 0;
    }
  }
  if (before->record_count != after->record_count) {
    *first_index = common;
    return 0;
  }
  return 1;
}

static void po_diff_compare_record_payloads(const po_diff_frame_t *before,
                                            const po_diff_frame_t *after, po_diff_payload_t *out) {
  *out = (po_diff_payload_t){.identical = 1};
  for (size_t record_index = 0; record_index < before->record_count; ++record_index) {
    const po_diff_record_t *a = before->records + record_index;
    const po_diff_record_t *b = after->records + record_index;
    int changed = 0;

    for (size_t packet_index = 0; packet_index < a->packet_count; ++packet_index) {
      const po_packet_t *left = before->packets.items + a->packet_start + packet_index;
      const po_packet_t *right = after->packets.items + b->packet_start + packet_index;
      if (memcmp(left->payload, right->payload, left->payload_len) == 0)
        continue;
      if (out->identical) {
        size_t byte_index = 0;
        while (left->payload[byte_index] == right->payload[byte_index])
          ++byte_index;
        out->first_index = record_index;
        out->first_packet_index = a->packet_start + packet_index;
        out->first_byte_index = byte_index;
        out->before_byte = left->payload[byte_index];
        out->after_byte = right->payload[byte_index];
        out->identical = 0;
      }
      changed = 1;
      break;
    }
    if (changed)
      ++out->changed;
  }
}

static int po_diff_writef(FILE *stream, const char *format, ...) {
  va_list arguments;
  int written;

  va_start(arguments, format);
  written = vfprintf(stream, format, arguments);
  va_end(arguments);
  return written >= 0;
}

static int po_diff_write_packet(FILE *stream, po_frame_family_t family, const po_packet_t *packet) {
  if (packet == NULL)
    return po_diff_writef(stream, "-");
  return po_diff_writef(stream, "%s(tag=0x%04x,raw_bytes=%zu)",
                        po_diff_packet_known(family, packet->tag_code) ? "packet" : "raw",
                        (unsigned)packet->tag_code, packet->payload_len);
}

static int po_diff_write_record(FILE *stream, const po_diff_record_t *record) {
  if (record == NULL)
    return po_diff_writef(stream, "-");
  switch (record->kind) {
  case PO_DIFF_RECORD_RAW:
    return po_diff_writef(stream, "raw(tag=0x%04x,packets=%zu,raw_bytes=%zu)",
                          (unsigned)record->tag_code, record->packet_count, record->payload_bytes);
  case PO_DIFF_RECORD_PO33_SETTINGS:
    return po_diff_writef(stream, "po33-settings(packets=%zu,raw_bytes=%zu)", record->packet_count,
                          record->payload_bytes);
  case PO_DIFF_RECORD_PO33_PATTERN:
    return po_diff_writef(stream, "po33-pattern(selector_raw=%u,packets=%zu,raw_bytes=%zu)",
                          (unsigned)record->selector_raw, record->packet_count,
                          record->payload_bytes);
  case PO_DIFF_RECORD_PO33_SOUND:
    return po_diff_writef(stream, "po33-sound(selector_raw=%u,packets=%zu,raw_bytes=%zu)",
                          (unsigned)record->selector_raw, record->packet_count,
                          record->payload_bytes);
  case PO_DIFF_RECORD_PO35_PATTERN:
    return po_diff_writef(stream, "po35-pattern(selector_raw=%u,packets=%zu,raw_bytes=%zu)",
                          (unsigned)record->selector_raw, record->packet_count,
                          record->payload_bytes);
  case PO_DIFF_RECORD_PO35_SPEECH:
    return po_diff_writef(stream, "po35-speech(slot_raw=%u,packets=%zu,raw_bytes=%zu)",
                          (unsigned)record->selector_raw, record->packet_count,
                          record->payload_bytes);
  default:
    return 0;
  }
}

static int po_diff_write_byte(FILE *stream, int present, uint8_t value) {
  return present ? po_diff_writef(stream, "0x%02x", (unsigned)value) : po_diff_writef(stream, "-");
}

static int po_diff_write_report(FILE *stream, po_frame_family_t family,
                                const po_diff_frame_t *before, const po_diff_frame_t *after) {
  po_diff_bytes_t bytes;
  po_diff_payload_t packet_payloads;
  po_diff_payload_t record_payloads;
  size_t packet_first = 0;
  size_t record_first = 0;
  int packets_same;
  int records_same;

  if (stream == NULL || before == NULL || after == NULL || po_diff_family_name(family) == NULL)
    return 0;
  po_diff_compare_bytes(before, after, &bytes);
  packets_same = po_diff_packet_topology_equal(before, after, &packet_first);
  records_same = po_diff_record_topology_equal(before, after, &record_first);
  if (!po_diff_writef(stream, "family=%s\n", po_diff_family_name(family)))
    return 0;
  if (bytes.identical) {
    if (!po_diff_writef(stream, "bytes identical count=%zu\n", before->byte_count))
      return 0;
  } else if (!po_diff_writef(
                 stream, "bytes changed before=%zu after=%zu changed=%zu first_offset=%zu before=",
                 before->byte_count, after->byte_count, bytes.changed, bytes.first_offset) ||
             !po_diff_write_byte(stream, bytes.before_present, bytes.before_byte) ||
             !po_diff_writef(stream, " after=") ||
             !po_diff_write_byte(stream, bytes.after_present, bytes.after_byte) ||
             !po_diff_writef(stream, "\n")) {
    return 0;
  }
  if (packets_same) {
    if (!po_diff_writef(stream, "packets topology identical count=%zu\n", before->packets.count))
      return 0;
    po_diff_compare_packet_payloads(before, after, &packet_payloads);
    if (packet_payloads.identical) {
      if (!po_diff_writef(stream, "packets payload identical count=%zu\n", before->packets.count))
        return 0;
    } else if (!po_diff_writef(
                   stream,
                   "packets payload changed count=%zu first_index=%zu first_raw_offset=%zu "
                   "before=0x%02x after=0x%02x\n",
                   packet_payloads.changed, packet_payloads.first_index,
                   packet_payloads.first_byte_index, (unsigned)packet_payloads.before_byte,
                   (unsigned)packet_payloads.after_byte)) {
      return 0;
    }
  } else if (!po_diff_writef(
                 stream, "packets topology changed before=%zu after=%zu first_index=%zu before=",
                 before->packets.count, after->packets.count, packet_first) ||
             !po_diff_write_packet(stream, family,
                                   packet_first < before->packets.count
                                       ? before->packets.items + packet_first
                                       : NULL) ||
             !po_diff_writef(stream, " after=") ||
             !po_diff_write_packet(stream, family,
                                   packet_first < after->packets.count
                                       ? after->packets.items + packet_first
                                       : NULL) ||
             !po_diff_writef(stream, "\npackets payload not-compared topology=changed\n")) {
    return 0;
  }
  if (records_same) {
    if (!po_diff_writef(stream, "records topology identical count=%zu\n", before->record_count))
      return 0;
    po_diff_compare_record_payloads(before, after, &record_payloads);
    if (record_payloads.identical) {
      if (!po_diff_writef(stream, "records payload identical count=%zu\n", before->record_count))
        return 0;
    } else if (!po_diff_writef(stream,
                               "records payload changed count=%zu first_index=%zu first_packet=%zu "
                               "first_raw_offset=%zu before=0x%02x after=0x%02x record=",
                               record_payloads.changed, record_payloads.first_index,
                               record_payloads.first_packet_index, record_payloads.first_byte_index,
                               (unsigned)record_payloads.before_byte,
                               (unsigned)record_payloads.after_byte) ||
               !po_diff_write_record(stream, before->records + record_payloads.first_index) ||
               !po_diff_writef(stream, "\n")) {
      return 0;
    }
  } else if (!po_diff_writef(
                 stream, "records topology changed before=%zu after=%zu first_index=%zu before=",
                 before->record_count, after->record_count, record_first) ||
             !po_diff_write_record(stream, record_first < before->record_count
                                               ? before->records + record_first
                                               : NULL) ||
             !po_diff_writef(stream, " after=") ||
             !po_diff_write_record(stream, record_first < after->record_count
                                               ? after->records + record_first
                                               : NULL) ||
             !po_diff_writef(stream, "\nrecords payload not-compared topology=changed\n")) {
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  po_frame_family_t family;
  po_diff_frame_t before = {0};
  po_diff_frame_t after = {0};
  int rc = 1;

  if (argc != 5 || strcmp(argv[1], "--family") != 0 || !po_diff_parse_family(argv[2], &family)) {
    fprintf(stderr, "Usage: %s --family po33|po35 before.bin after.bin\n", argv[0]);
    return 2;
  }
  if (!po_diff_read_file(argv[3], &before.bytes, &before.byte_count)) {
    fprintf(stderr, "Cannot read before frame file: %s\n", argv[3]);
    goto cleanup;
  }
  if (!po_diff_collect_packets(family, before.bytes, before.byte_count, &before.packets)) {
    fprintf(stderr, "Before input is not one complete %s frame: %s\n", po_diff_family_name(family),
            argv[3]);
    goto cleanup;
  }
  if (!po_diff_build_records(family, &before)) {
    fprintf(stderr, "Cannot analyze before frame records.\n");
    goto cleanup;
  }
  if (!po_diff_read_file(argv[4], &after.bytes, &after.byte_count)) {
    fprintf(stderr, "Cannot read after frame file: %s\n", argv[4]);
    goto cleanup;
  }
  if (!po_diff_collect_packets(family, after.bytes, after.byte_count, &after.packets)) {
    fprintf(stderr, "After input is not one complete %s frame: %s\n", po_diff_family_name(family),
            argv[4]);
    goto cleanup;
  }
  if (!po_diff_build_records(family, &after)) {
    fprintf(stderr, "Cannot analyze after frame records.\n");
    goto cleanup;
  }
  if (!po_diff_write_report(stdout, family, &before, &after)) {
    fprintf(stderr, "Cannot write frame diff.\n");
    goto cleanup;
  }
  rc = 0;

cleanup:
  po_diff_frame_destroy(&before);
  po_diff_frame_destroy(&after);
  return rc;
}
