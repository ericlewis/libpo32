/* Hosted description of observed PO-35 partial-transfer shapes. */
#include "po35.h"
#include "po_frame_report.h"

#include <stdio.h>
#include <stdlib.h>

typedef enum po35_manifest_relation {
  PO35_MANIFEST_NOT_EVALUABLE,
  PO35_MANIFEST_MATCH,
  PO35_MANIFEST_MISMATCH
} po35_manifest_relation_t;

typedef struct po35_manifest {
  size_t settings_records;
  size_t pattern_records;
  size_t full_pattern_records;
  size_t empty_pattern_records;
  size_t speech_header_records;
  uint16_t chain_mask;
  uint16_t pattern_mask;
  uint16_t speech_header_mask;
  uint16_t speech_event_mask;
  int settings_invalid;
  int chain_invalid;
  int pattern_invalid;
  int speech_invalid;
  int speech_active_slot;
  size_t speech_expected_bytes;
  size_t speech_received_bytes;
} po35_manifest_t;

static void po35_manifest_observe_pattern(po35_manifest_t *manifest, uint8_t index) {
  uint16_t bit;
  if (index >= 16u) {
    manifest->pattern_invalid = 1;
    return;
  }
  bit = (uint16_t)(1u << index);
  if (manifest->pattern_mask & bit)
    manifest->pattern_invalid = 1;
  manifest->pattern_mask |= bit;
}

static void po35_manifest_observe_events(po35_manifest_t *manifest, const po35_pattern_t *pattern) {
  for (size_t index = 0u; index < PO35_PATTERN_STEP_COUNT; ++index) {
    uint8_t event = pattern->speech_events[index];
    uint8_t slot;
    if ((event & 15u) == 0u)
      continue;
    slot = event >> 4u;
    if (slot >= PO35_SPEECH_SLOT_COUNT) {
      manifest->speech_invalid = 1;
      continue;
    }
    manifest->speech_event_mask |= (uint16_t)(1u << slot);
  }
}

static void po35_manifest_finish_speech_group(po35_manifest_t *manifest) {
  if (manifest->speech_active_slot < 0 ||
      manifest->speech_received_bytes != manifest->speech_expected_bytes)
    manifest->speech_invalid = 1;
  manifest->speech_active_slot = -1;
  manifest->speech_expected_bytes = 0u;
  manifest->speech_received_bytes = 0u;
}

static void po35_manifest_observe_speech_packet(po35_manifest_t *manifest,
                                                const po_packet_t *packet) {
  switch (packet->tag_code) {
  case PO35_TAG_SPEECH_HEADER: {
    po35_speech_header_t header;
    uint16_t bit;
    ++manifest->speech_header_records;
    if (manifest->speech_active_slot >= 0)
      po35_manifest_finish_speech_group(manifest);
    if (po35_speech_header_decode(packet->payload, packet->payload_len, &header) != PO_OK ||
        header.slot >= PO35_SPEECH_SLOT_COUNT) {
      manifest->speech_invalid = 1;
      break;
    }
    bit = (uint16_t)(1u << header.slot);
    if (manifest->speech_header_mask & bit)
      manifest->speech_invalid = 1;
    manifest->speech_header_mask |= bit;
    manifest->speech_active_slot = header.slot;
    manifest->speech_expected_bytes = (size_t)header.record_count * PO35_SPEECH_RECORD_BYTES;
    manifest->speech_received_bytes = 0u;
    break;
  }
  case PO35_TAG_SPEECH_DATA:
    if (manifest->speech_active_slot < 0 || packet->payload_len == 0u ||
        packet->payload_len > manifest->speech_expected_bytes - manifest->speech_received_bytes) {
      manifest->speech_invalid = 1;
      break;
    }
    manifest->speech_received_bytes += packet->payload_len;
    break;
  case PO35_TAG_SPEECH_END:
    if (packet->payload_len != 0u)
      manifest->speech_invalid = 1;
    po35_manifest_finish_speech_group(manifest);
    break;
  default:
    if (manifest->speech_active_slot >= 0)
      manifest->speech_invalid = 1;
    break;
  }
}

static int po35_manifest_collect_packet(const po_packet_t *packet, void *user) {
  po35_manifest_t *manifest = (po35_manifest_t *)user;
  po35_manifest_observe_speech_packet(manifest, packet);
  switch (packet->tag_code) {
  case PO35_TAG_SETTINGS: {
    po35_settings_t settings;
    ++manifest->settings_records;
    if (po35_settings_decode(packet->payload, packet->payload_len, &settings) != PO_OK) {
      manifest->settings_invalid = 1;
      break;
    }
    for (size_t index = 0u; index < settings.chain_count; ++index) {
      uint8_t pattern_index = settings.chain[index];
      if (pattern_index >= 16u)
        manifest->chain_invalid = 1;
      else
        manifest->chain_mask |= (uint16_t)(1u << pattern_index);
    }
    break;
  }
  case PO35_TAG_PATTERN: {
    po35_pattern_t pattern;
    ++manifest->pattern_records;
    ++manifest->full_pattern_records;
    if (po35_pattern_decode(packet->payload, packet->payload_len, &pattern) != PO_OK) {
      manifest->pattern_invalid = 1;
      break;
    }
    po35_manifest_observe_pattern(manifest, pattern.pattern_index);
    po35_manifest_observe_events(manifest, &pattern);
    break;
  }
  case PO35_TAG_PATTERN_EMPTY:
    ++manifest->pattern_records;
    ++manifest->empty_pattern_records;
    if (packet->payload_len != 1u)
      manifest->pattern_invalid = 1;
    else
      po35_manifest_observe_pattern(manifest, packet->payload[0]);
    break;
  default:
    break;
  }
  return 0;
}

static po35_manifest_relation_t po35_manifest_chain_relation(const po35_manifest_t *manifest) {
  if (manifest->settings_records != 1u || manifest->settings_invalid || manifest->chain_invalid ||
      manifest->pattern_invalid || manifest->pattern_records == 0u ||
      manifest->pattern_records >= 16u)
    return PO35_MANIFEST_NOT_EVALUABLE;
  return manifest->chain_mask == manifest->pattern_mask ? PO35_MANIFEST_MATCH
                                                        : PO35_MANIFEST_MISMATCH;
}

static po35_manifest_relation_t po35_manifest_speech_relation(const po35_manifest_t *manifest) {
  if (manifest->pattern_invalid || manifest->speech_invalid || manifest->pattern_records == 0u ||
      manifest->pattern_records >= 16u)
    return PO35_MANIFEST_NOT_EVALUABLE;
  return manifest->speech_header_mask == manifest->speech_event_mask ? PO35_MANIFEST_MATCH
                                                                     : PO35_MANIFEST_MISMATCH;
}

static const char *po35_manifest_relation_name(po35_manifest_relation_t relation) {
  switch (relation) {
  case PO35_MANIFEST_MATCH:
    return "match";
  case PO35_MANIFEST_MISMATCH:
    return "mismatch";
  default:
    return "not-evaluable";
  }
}

static const char *po35_manifest_shape(const po35_manifest_t *manifest,
                                       po35_manifest_relation_t chain_relation,
                                       po35_manifest_relation_t speech_relation) {
  if (!manifest->pattern_invalid && manifest->pattern_records == 16u &&
      manifest->pattern_mask == UINT16_MAX)
    return "full-bank-like";
  if (manifest->pattern_records != 0u && manifest->pattern_records < 16u &&
      chain_relation == PO35_MANIFEST_MATCH && speech_relation == PO35_MANIFEST_MATCH)
    return "observed-partial-candidate";
  return "unclassified";
}

static int po35_manifest_write(FILE *stream, const po35_manifest_t *manifest) {
  po35_manifest_relation_t chain_relation;
  po35_manifest_relation_t speech_relation;
  if (stream == NULL || manifest == NULL)
    return 0;
  chain_relation = po35_manifest_chain_relation(manifest);
  speech_relation = po35_manifest_speech_relation(manifest);
  return fprintf(stream,
                 "pattern_records=%zu full_patterns=%zu empty_patterns=%zu settings=%zu "
                 "speech_headers=%zu\n"
                 "pattern_indices=0x%04x active_chain=0x%04x speech_headers=0x%04x "
                 "speech_events=0x%04x\n"
                 "chain_pattern_set=%s\n"
                 "speech_header_event_set=%s\n"
                 "shape=%s\n"
                 "projection=none\n",
                 manifest->pattern_records, manifest->full_pattern_records,
                 manifest->empty_pattern_records, manifest->settings_records,
                 manifest->speech_header_records, (unsigned)manifest->pattern_mask,
                 (unsigned)manifest->chain_mask, (unsigned)manifest->speech_header_mask,
                 (unsigned)manifest->speech_event_mask, po35_manifest_relation_name(chain_relation),
                 po35_manifest_relation_name(speech_relation),
                 po35_manifest_shape(manifest, chain_relation, speech_relation)) >= 0;
}

int main(int argc, char **argv) {
  po35_decode_result_t result;
  po35_manifest_t manifest = {.speech_active_slot = -1};
  uint8_t *frame = NULL;
  size_t frame_bytes = 0u;
  po_status_t status;
  int rc = 1;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s frame.bin\n", argv[0]);
    return 2;
  }
  if (!po_frame_read_file(argv[1], &frame, &frame_bytes)) {
    fprintf(stderr, "Cannot read frame: %s\n", argv[1]);
    goto cleanup;
  }
  status = po35_frame_parse(frame, frame_bytes, po35_manifest_collect_packet, &manifest, &result);
  if (status != PO_OK || !result.done || result.stopped || result.frame_bytes != frame_bytes) {
    fprintf(stderr, "Input is not one complete PO-35 frame: %s\n", argv[1]);
    goto cleanup;
  }
  if (manifest.speech_active_slot >= 0)
    manifest.speech_invalid = 1;
  if (!po35_manifest_write(stdout, &manifest) || fflush(stdout) != 0) {
    fprintf(stderr, "Cannot write transfer manifest.\n");
    goto cleanup;
  }
  rc = 0;

cleanup:
  free(frame);
  return rc;
}
