/* Hosted, read-only diagnosis of one stereo backup capture. */
#include "po_capture.h"
#include "po_frame_report.h"
#include "po_wav_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PO_CAPTURE_DOCTOR_FAMILY_COUNT = 2, PO_CAPTURE_DOCTOR_ORDER_COUNT = 2 };

typedef struct po_capture_doctor_candidate {
  int recovered;
  uint8_t *wire;
  size_t wire_bytes;
  po_frame_report_t reports[PO_CAPTURE_DOCTOR_FAMILY_COUNT];
} po_capture_doctor_candidate_t;

typedef struct po_capture_doctor_wav_info {
  const char *format;
} po_capture_doctor_wav_info_t;

static const po_frame_family_t po_capture_doctor_families[PO_CAPTURE_DOCTOR_FAMILY_COUNT] = {
    PO_FRAME_FAMILY_PO33,
    PO_FRAME_FAMILY_PO35,
};

static const po_stereo_channel_order_t po_capture_doctor_orders[PO_CAPTURE_DOCTOR_ORDER_COUNT] = {
    PO_STEREO_CHANNEL_NORMAL,
    PO_STEREO_CHANNEL_SWAPPED,
};

static uint16_t po_capture_doctor_le16(const uint8_t *bytes) {
  return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t po_capture_doctor_le32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
}

/* po_capture_read intentionally normalizes samples. The doctor keeps the
 * source sample encoding separate so its WAV format field remains factual. */
static int po_capture_doctor_read_wav_info(const char *path, po_capture_doctor_wav_info_t *out) {
  FILE *file = NULL;
  uint8_t *bytes = NULL;
  const char *format = NULL;
  long end;
  size_t file_size;
  size_t riff_size;
  size_t position;
  uint16_t encoding = 0;
  uint16_t bits = 0;
  int ok = 0;

  if (path == NULL || out == NULL)
    return 0;
  *out = (po_capture_doctor_wav_info_t){0};
  file = fopen(path, "rb");
  if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 12 ||
      fseek(file, 0, SEEK_SET) != 0)
    goto cleanup;
  file_size = (size_t)end;
  bytes = (uint8_t *)malloc(file_size);
  if (bytes == NULL || fread(bytes, 1, file_size, file) != file_size ||
      memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0 ||
      po_capture_doctor_le32(bytes + 4) < 4 ||
      (size_t)po_capture_doctor_le32(bytes + 4) > file_size - 8u)
    goto cleanup;
  riff_size = (size_t)po_capture_doctor_le32(bytes + 4) + 8u;
  for (position = 12u; position <= riff_size - 8u;) {
    const uint8_t *chunk = bytes + position;
    size_t chunk_size = po_capture_doctor_le32(chunk + 4);
    if (chunk_size > riff_size - position - 8u)
      goto cleanup;
    if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16u) {
      encoding = po_wav_encoding(chunk + 8u, chunk_size);
      bits = po_capture_doctor_le16(chunk + 22u);
      if (encoding == 0)
        goto cleanup;
    }
    position += 8u + chunk_size;
    if (chunk_size & 1u) {
      if (position == riff_size)
        break;
      ++position;
    }
  }
  if (encoding == 1u && bits == 16u)
    format = "pcm16";
  else if (encoding == 1u && bits == 24u)
    format = "pcm24";
  else if (encoding == 1u && bits == 32u)
    format = "pcm32";
  else if (encoding == 3u && bits == 32u)
    format = "float32";
  else
    goto cleanup;
  out->format = format;
  ok = 1;

cleanup:
  free(bytes);
  if (file != NULL && fclose(file) != 0)
    ok = 0;
  if (!ok)
    *out = (po_capture_doctor_wav_info_t){0};
  return ok;
}

static const char *po_capture_doctor_family_name(po_frame_family_t family) {
  switch (family) {
  case PO_FRAME_FAMILY_PO33:
    return "po33";
  case PO_FRAME_FAMILY_PO35:
    return "po35";
  default:
    return NULL;
  }
}

static const char *po_capture_doctor_order_name(po_stereo_channel_order_t order) {
  switch (order) {
  case PO_STEREO_CHANNEL_NORMAL:
    return "normal";
  case PO_STEREO_CHANNEL_SWAPPED:
    return "swapped";
  default:
    return NULL;
  }
}

static void po_capture_doctor_candidate_destroy(po_capture_doctor_candidate_t *candidate) {
  size_t index;
  if (candidate == NULL)
    return;
  free(candidate->wire);
  for (index = 0; index < PO_CAPTURE_DOCTOR_FAMILY_COUNT; ++index)
    po_frame_report_destroy(candidate->reports + index);
  *candidate = (po_capture_doctor_candidate_t){0};
}

/* A transport failure intentionally remains only "not-recovered": the hosted
 * adapter does not expose a more specific reason, and this command must not
 * infer one from the waveform. */
static int po_capture_doctor_collect_candidate(const po_capture_t *capture,
                                               po_stereo_channel_order_t order,
                                               po_capture_doctor_candidate_t *out) {
  size_t index;
  if (capture == NULL || out == NULL)
    return 0;
  *out = (po_capture_doctor_candidate_t){0};
  if (!po_capture_decode(capture, order, &out->wire, &out->wire_bytes))
    return 1;
  out->recovered = 1;
  for (index = 0; index < PO_CAPTURE_DOCTOR_FAMILY_COUNT; ++index) {
    if (!po_frame_report_collect(po_capture_doctor_families[index], out->wire, out->wire_bytes,
                                 out->reports + index)) {
      po_capture_doctor_candidate_destroy(out);
      return 0;
    }
  }
  return 1;
}

static int po_capture_doctor_write_not_run_text(FILE *stream, po_frame_family_t family) {
  const char *name = po_capture_doctor_family_name(family);
  return name != NULL && fprintf(stream, "  %s parser=not-run complete=no\n", name) >= 0;
}

static int po_capture_doctor_write_candidate_text(FILE *stream, po_stereo_channel_order_t order,
                                                  const po_capture_doctor_candidate_t *candidate) {
  const char *order_name = po_capture_doctor_order_name(order);
  size_t index;
  if (stream == NULL || candidate == NULL || order_name == NULL ||
      fprintf(stream, "candidate channel_order=%s transport=%s recovered_bytes=%zu\n", order_name,
              candidate->recovered ? "recovered" : "not-recovered", candidate->wire_bytes) < 0)
    return 0;
  for (index = 0; index < PO_CAPTURE_DOCTOR_FAMILY_COUNT; ++index) {
    if (!candidate->recovered) {
      if (!po_capture_doctor_write_not_run_text(stream, po_capture_doctor_families[index]))
        return 0;
    } else if (fprintf(stream, "  ") < 0 ||
               !po_frame_report_write_text(stream, candidate->reports + index)) {
      return 0;
    }
  }
  return 1;
}

static int po_capture_doctor_write_text(FILE *stream, const po_capture_t *capture,
                                        const po_capture_doctor_wav_info_t *wav,
                                        const po_capture_doctor_candidate_t *candidates) {
  size_t index;
  if (stream == NULL)
    return 0;
  if (capture == NULL)
    return fprintf(stream, "capture status=unreadable\nselection=none\n") >= 0;
  if (wav == NULL || wav->format == NULL || candidates == NULL ||
      fprintf(stream,
              "capture status=ok format=%s channels=2 sample_rate=%u frames=%zu\nselection=none\n",
              wav->format, (unsigned)capture->rate, capture->frames) < 0)
    return 0;
  for (index = 0; index < PO_CAPTURE_DOCTOR_ORDER_COUNT; ++index) {
    if (!po_capture_doctor_write_candidate_text(stream, po_capture_doctor_orders[index],
                                                candidates + index))
      return 0;
  }
  return 1;
}

static int po_capture_doctor_write_not_run_json(FILE *stream, po_frame_family_t family) {
  const char *name = po_capture_doctor_family_name(family);
  return name != NULL &&
         fprintf(stream, "{\"family\":\"%s\",\"parser_status\":\"not-run\",\"complete\":false}",
                 name) >= 0;
}

static int po_capture_doctor_write_candidate_json(FILE *stream, po_stereo_channel_order_t order,
                                                  const po_capture_doctor_candidate_t *candidate) {
  const char *order_name = po_capture_doctor_order_name(order);
  size_t index;
  if (stream == NULL || candidate == NULL || order_name == NULL ||
      fprintf(
          stream,
          "{\"channel_order\":\"%s\",\"transport\":\"%s\",\"recovered_bytes\":%zu,\"families\":[",
          order_name, candidate->recovered ? "recovered" : "not-recovered",
          candidate->wire_bytes) < 0)
    return 0;
  for (index = 0; index < PO_CAPTURE_DOCTOR_FAMILY_COUNT; ++index) {
    if ((index != 0 && fputc(',', stream) == EOF) ||
        (!candidate->recovered &&
         !po_capture_doctor_write_not_run_json(stream, po_capture_doctor_families[index])) ||
        (candidate->recovered && !po_frame_report_write_json(stream, candidate->reports + index)))
      return 0;
  }
  return fputs("]}", stream) != EOF;
}

static int po_capture_doctor_write_json(FILE *stream, const po_capture_t *capture,
                                        const po_capture_doctor_wav_info_t *wav,
                                        const po_capture_doctor_candidate_t *candidates) {
  size_t index;
  if (stream == NULL)
    return 0;
  if (capture == NULL)
    return fputs("{\"capture\":{\"status\":\"unreadable\"},\"selection\":\"none\",\"candidates\":[]"
                 "}\n",
                 stream) != EOF;
  if (wav == NULL || wav->format == NULL || candidates == NULL ||
      fprintf(stream,
              "{\"capture\":{\"status\":\"ok\",\"format\":\"%s\",\"channels\":2,\"sample_"
              "rate\":%u,\"frames\":%zu},\"selection\":\"none\",\"candidates\":[",
              wav->format, (unsigned)capture->rate, capture->frames) < 0)
    return 0;
  for (index = 0; index < PO_CAPTURE_DOCTOR_ORDER_COUNT; ++index) {
    if ((index != 0 && fputc(',', stream) == EOF) ||
        !po_capture_doctor_write_candidate_json(stream, po_capture_doctor_orders[index],
                                                candidates + index))
      return 0;
  }
  return fputs("]}\n", stream) != EOF;
}

int main(int argc, char **argv) {
  po_capture_t capture = {0};
  po_capture_doctor_wav_info_t wav = {0};
  po_capture_doctor_candidate_t candidates[PO_CAPTURE_DOCTOR_ORDER_COUNT] = {{0}};
  const char *path = NULL;
  int json = 0;
  int rc = 1;
  size_t index;

  if (argc == 2) {
    path = argv[1];
  } else if (argc == 3 && (strcmp(argv[1], "--text") == 0 || strcmp(argv[1], "--json") == 0)) {
    path = argv[2];
    json = strcmp(argv[1], "--json") == 0;
  } else {
    fprintf(stderr, "Usage: %s [--text|--json] capture.wav\n", argv[0]);
    return 2;
  }

  if (!po_capture_read(path, &capture)) {
    if (!(json ? po_capture_doctor_write_json(stdout, NULL, NULL, NULL)
               : po_capture_doctor_write_text(stdout, NULL, NULL, NULL)))
      fprintf(stderr, "Cannot write capture diagnostic.\n");
    return 1;
  }
  if (!po_capture_doctor_read_wav_info(path, &wav)) {
    fprintf(stderr, "Cannot inspect capture WAV format.\n");
    goto cleanup;
  }
  for (index = 0; index < PO_CAPTURE_DOCTOR_ORDER_COUNT; ++index) {
    if (!po_capture_doctor_collect_candidate(&capture, po_capture_doctor_orders[index],
                                             candidates + index)) {
      fprintf(stderr, "Cannot collect capture diagnostic.\n");
      goto cleanup;
    }
  }
  if (!(json ? po_capture_doctor_write_json(stdout, &capture, &wav, candidates)
             : po_capture_doctor_write_text(stdout, &capture, &wav, candidates))) {
    fprintf(stderr, "Cannot write capture diagnostic.\n");
    goto cleanup;
  }
  rc = 0;

cleanup:
  for (index = 0; index < PO_CAPTURE_DOCTOR_ORDER_COUNT; ++index)
    po_capture_doctor_candidate_destroy(candidates + index);
  free(capture.samples);
  return rc;
}
