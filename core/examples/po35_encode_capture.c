/* Hosted speech replacement and modem output; PCM analysis lives in the core. */
#include "po35.h"
#include "po35_analysis.h"
#include "po_capture.h"
#include "po_stereo_wav.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *path;
  uint8_t *records;
  size_t bytes;
} e35_edit_t;

typedef enum {
  E35_CHANNEL_UNSET,
  E35_CHANNEL_LEFT,
  E35_CHANNEL_RIGHT,
  E35_CHANNEL_MIX
} e35_channel_t;

typedef struct {
  e35_edit_t edits[PO35_SPEECH_SLOT_COUNT];
  uint32_t rate, fps;
  e35_channel_t channel;
  size_t edit_count;
} e35_options_t;

typedef struct {
  const e35_options_t *options;
  uint16_t seen;
  int active, failed;
  size_t expected, received, removed_bytes;
} e35_scan_t;

typedef struct {
  po35_builder_t builder;
  const e35_options_t *options;
  int active, failed;
} e35_rebuild_t;

static int e35_read_file(const char *path, uint8_t **out, size_t *out_length) {
  FILE *fp = fopen(path, "rb");
  uint8_t *bytes = NULL;
  long length = 0;
  int ok = 0;
  if (fp == NULL)
    return 0;
  if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0)
    goto cleanup;
  bytes = (uint8_t *)malloc(length ? (size_t)length : 1u);
  if (bytes == NULL || fread(bytes, 1, (size_t)length, fp) != (size_t)length || ferror(fp))
    goto cleanup;
  ok = 1;
cleanup:
  if (fclose(fp) != 0)
    ok = 0;
  if (ok) {
    *out = bytes;
    *out_length = (size_t)length;
  } else
    free(bytes);
  return ok;
}

static int e35_rebuild_packet(const po_packet_t *packet, void *user) {
  e35_rebuild_t *rebuild = (e35_rebuild_t *)user;
  if (rebuild->options != NULL) {
    if (packet->tag_code == PO35_TAG_SPEECH_HEADER) {
      const e35_edit_t *edit = &rebuild->options->edits[packet->payload[0]];
      rebuild->active = edit->path ? packet->payload[0] : -1;
      if (edit->path) {
        uint8_t header[PO35_SPEECH_HEADER_BYTES];
        size_t count = edit->bytes / PO35_SPEECH_RECORD_BYTES;
        memcpy(header, packet->payload, sizeof(header));
        header[1] = (uint8_t)(count >> 8);
        header[2] = (uint8_t)count;
        header[3] = (uint8_t)rebuild->options->fps;
        if (po35_builder_append_packet(&rebuild->builder, packet->tag_code, header, sizeof(header),
                                       NULL) != PO_OK)
          goto failure;
        for (size_t pos = 0; pos < edit->bytes;) {
          size_t bytes = edit->bytes - pos;
          if (bytes > 5u * PO35_SPEECH_RECORD_BYTES)
            bytes = 5u * PO35_SPEECH_RECORD_BYTES;
          if (po35_builder_append_packet(&rebuild->builder, PO35_TAG_SPEECH_DATA,
                                         edit->records + pos, bytes, NULL) != PO_OK)
            goto failure;
          pos += bytes;
        }
        return 0;
      }
    } else if (packet->tag_code == PO35_TAG_SPEECH_DATA && rebuild->active >= 0) {
      return 0;
    } else if (packet->tag_code == PO35_TAG_SPEECH_END) {
      rebuild->active = -1;
    }
  }
  if (po35_builder_append_packet(&rebuild->builder, packet->tag_code, packet->payload,
                                 packet->payload_len, NULL) != PO_OK)
    goto failure;
  return 0;
failure:
  rebuild->failed = 1;
  return 1;
}

static int e35_scan_packet(const po_packet_t *packet, void *user) {
  e35_scan_t *scan = (e35_scan_t *)user;
  switch (packet->tag_code) {
  case PO35_TAG_SPEECH_HEADER: {
    po35_speech_header_t header;
    if (scan->active >= 0 ||
        po35_speech_header_decode(packet->payload, packet->payload_len, &header) != PO_OK ||
        header.slot >= PO35_SPEECH_SLOT_COUNT || (scan->seen & (1u << header.slot)))
      goto failure;
    scan->seen |= (uint16_t)(1u << header.slot);
    scan->active = header.slot;
    scan->expected = (size_t)header.record_count * PO35_SPEECH_RECORD_BYTES;
    scan->received = 0;
    break;
  }
  case PO35_TAG_SPEECH_DATA:
    if (scan->active < 0 || packet->payload_len == 0 ||
        packet->payload_len > scan->expected - scan->received)
      goto failure;
    scan->received += packet->payload_len;
    if (scan->options->edits[scan->active].path)
      scan->removed_bytes += packet->payload_len + 5u;
    break;
  case PO35_TAG_SPEECH_END:
    if (scan->active < 0 || packet->payload_len != 0 || scan->received != scan->expected)
      goto failure;
    scan->active = -1;
    break;
  default:
    if (scan->active >= 0) {
      if (scan->options->edits[scan->active].path) {
        fprintf(stderr, "Speech slot %d contains an extension; replacement is ambiguous.\n",
                scan->active);
        goto failure;
      }
      switch (packet->tag_code) {
      case PO35_TAG_SETTINGS:
      case PO35_TAG_PATTERN:
      case PO35_TAG_PATTERN_EMPTY:
      case PO35_TAG_PATCH:
      case PO35_TAG_TABLE_7BE7:
      case PO35_TAG_TABLE_1C7F:
        goto failure;
      default:
        break;
      }
    }
    break;
  }
  return 0;
failure:
  scan->failed = 1;
  return 1;
}

static int e35_number(const char *text, unsigned long maximum, unsigned long *out) {
  char *end;
  unsigned long value;
  int base;
  if (!text || text[0] < '0' || text[0] > '9')
    return 0;
  base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
  errno = 0;
  value = strtoul(text, &end, base);
  if (errno || end == text || *end || value > maximum)
    return 0;
  *out = value;
  return 1;
}

static int e35_parse_options(int argc, char **argv, e35_options_t *options) {
  int have_rate = 0, have_fps = 0, have_channel = 0;
  *options = (e35_options_t){0};
  options->rate = 48000;
  options->fps = 80;
  if (argc < 3)
    return 0;
  for (int i = 3; i < argc;) {
    unsigned long value;
    if (!strcmp(argv[i], "--speech")) {
      if (i + 2 >= argc || !e35_number(argv[i + 1], PO35_SPEECH_SLOT_COUNT - 1u, &value) ||
          options->edits[value].path)
        return 0;
      options->edits[value].path = argv[i + 2];
      ++options->edit_count;
      i += 3;
    } else if (!strcmp(argv[i], "--rate")) {
      if (have_rate || i + 1 >= argc || !e35_number(argv[i + 1], 96000, &value) ||
          (value != 44100 && value != 48000 && value != 96000))
        return 0;
      options->rate = (uint32_t)value;
      have_rate = 1;
      i += 2;
    } else if (!strcmp(argv[i], "--fps")) {
      if (have_fps || i + 1 >= argc || !e35_number(argv[i + 1], 80, &value) ||
          (value != 20 && value != 40 && value != 60 && value != 80))
        return 0;
      options->fps = (uint32_t)value;
      have_fps = 1;
      i += 2;
    } else if (!strcmp(argv[i], "--channel")) {
      if (have_channel || i + 1 >= argc)
        return 0;
      if (!strcmp(argv[i + 1], "left"))
        options->channel = E35_CHANNEL_LEFT;
      else if (!strcmp(argv[i + 1], "right"))
        options->channel = E35_CHANNEL_RIGHT;
      else if (!strcmp(argv[i + 1], "mix"))
        options->channel = E35_CHANNEL_MIX;
      else
        return 0;
      have_channel = 1;
      i += 2;
    } else {
      return 0;
    }
  }
  return options->edit_count || (!have_fps && !have_channel);
}

static int e35_analyze(e35_edit_t *edit, uint32_t fps, e35_channel_t channel) {
  po_audio_t audio = {0};
  po35_analysis_workspace_t workspace;
  size_t bytes = 0, written = 0;
  int ok = 0;
  if (!po_audio_read(edit->path, &audio)) {
    fprintf(stderr, "Cannot read nonempty mono/stereo PCM or float WAV: %s\n", edit->path);
    goto cleanup;
  }
  if ((audio.channels == 2 && channel == E35_CHANNEL_UNSET) ||
      (audio.channels == 1 && channel == E35_CHANNEL_RIGHT)) {
    fprintf(stderr, "Stereo input requires --channel left|right|mix; mono has no right channel.\n");
    goto cleanup;
  }
  if (po35_analysis_size_for_pcm(audio.frames, audio.rate, fps, &bytes) != PO_OK || bytes == 0 ||
      bytes > 640u * PO35_SPEECH_RECORD_BYTES) {
    fprintf(stderr, "Speech input exceeds the 640-record creation limit at %u fps.\n", fps);
    goto cleanup;
  }
  for (size_t i = 0; i < audio.frames * audio.channels; ++i)
    if (audio.samples[i] < -1.0f || audio.samples[i] > 1.0f) {
      fprintf(stderr, "Speech PCM must be normalized to [-1,1]; no clipping is applied.\n");
      goto cleanup;
    }
  if (audio.channels == 2)
    for (size_t i = 0; i < audio.frames; ++i) {
      float left = audio.samples[2 * i], right = audio.samples[2 * i + 1];
      audio.samples[i] = channel == E35_CHANNEL_LEFT    ? left
                         : channel == E35_CHANNEL_RIGHT ? right
                                                        : left * 0.5f + right * 0.5f;
    }
  edit->records = (uint8_t *)malloc(bytes);
  if (edit->records == NULL ||
      po35_analysis_encode_f32(&workspace, audio.samples, audio.frames, audio.rate, fps,
                               edit->records, bytes, &written) != PO_OK ||
      written != bytes) {
    fprintf(stderr, "Cannot analyze speech input: %s\n", edit->path);
    goto cleanup;
  }
  edit->bytes = bytes;
  ok = 1;
cleanup:
  free(audio.samples);
  return ok;
}

static void e35_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s frame.bin new-output.wav [--rate 44100|48000|96000]\n"
          "       [--speech SLOT audio.wav]... [--fps 20|40|60|80]\n"
          "       [--channel left|right|mix]\n"
          "Speech slots are zero-based (0..14) and must exist in the input frame.\n"
          "Input: mono/stereo PCM16/24/32 or float32 WAV at 8000..192000 Hz.\n"
          "Stereo input requires --channel; mix is the arithmetic mean of both channels.\n"
          "Defaults: 48000 Hz transfer, 80 fps speech. Speech input is limited to 640\n"
          "records and padded to a whole record, never truncated or normalized.\n"
          "Sample metadata, controls and patterns are retained. Analysis is approximate.\n"
          "Without --speech the frame is rebuilt unchanged. Output must not exist.\n",
          program);
}

int main(int argc, char **argv) {
  uint8_t *frame = NULL, *rebuilt = NULL;
  size_t frame_len = 0, rebuilt_len = 0, capacity = 0;
  e35_options_t options;
  po35_decode_result_t result;
  e35_rebuild_t rebuild = {0};
  int ok = 0;
  if (!e35_parse_options(argc, argv, &options)) {
    e35_usage(argv[0]);
    return 2;
  }
  if (!e35_read_file(argv[1], &frame, &frame_len)) {
    fprintf(stderr, "Cannot read input frame.\n");
    goto cleanup;
  }
  capacity = frame_len;
  if (options.edit_count) {
    e35_scan_t scan = {0};
    scan.options = &options;
    scan.active = -1;
    if (po35_frame_parse(frame, frame_len, e35_scan_packet, &scan, &result) != PO_OK ||
        !result.done || result.stopped || result.frame_bytes != frame_len || scan.failed ||
        scan.active >= 0 || scan.removed_bytes > frame_len) {
      fprintf(stderr, "Input must contain complete, consistent PO-35 speech groups.\n");
      goto cleanup;
    }
    for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i)
      if (options.edits[i].path && !(scan.seen & (1u << i))) {
        fprintf(stderr, "Speech slot %zu is not present in the input frame.\n", i);
        goto cleanup;
      }
    capacity -= scan.removed_bytes;
    for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
      e35_edit_t *edit = &options.edits[i];
      size_t addition;
      if (!edit->path)
        continue;
      if (!e35_analyze(edit, options.fps, options.channel))
        goto cleanup;
      addition = edit->bytes + 5u * ((edit->bytes / PO35_SPEECH_RECORD_BYTES + 4u) / 5u);
      if (addition > SIZE_MAX - capacity)
        goto cleanup;
      capacity += addition;
    }
    rebuild.options = &options;
  }
  rebuild.active = -1;
  rebuilt = (uint8_t *)malloc(capacity ? capacity : 1u);
  if (rebuilt == NULL || po35_builder_init(&rebuild.builder, rebuilt, capacity) != PO_OK ||
      po35_frame_parse(frame, frame_len, e35_rebuild_packet, &rebuild, &result) != PO_OK ||
      !result.done || result.stopped || result.frame_bytes != frame_len || rebuild.failed ||
      po35_builder_finish(&rebuild.builder, &rebuilt_len) != PO_OK || rebuilt_len != capacity ||
      (!options.edit_count && memcmp(frame, rebuilt, frame_len))) {
    fprintf(stderr, "Input must contain exactly one complete, byte-preserving PO-35 frame.\n");
    goto cleanup;
  }
  if (!po35_write_transfer_wav(argv[2], rebuilt, rebuilt_len, options.rate)) {
    fprintf(stderr, "Cannot create new transfer WAV. Existing outputs are not overwritten.\n");
    goto cleanup;
  }
  if (options.edit_count)
    printf("Encoded %zu-byte PO-35 frame at %u Hz; replaced %zu speech slot(s) at %u fps.\n"
           "Analysis is approximate; original metadata and controls are retained.\n",
           rebuilt_len, options.rate, options.edit_count, options.fps);
  else
    printf("Encoded %zu-byte PO-35 frame at %u Hz; byte-exact rebuild verified.\n", rebuilt_len,
           options.rate);
  ok = 1;
cleanup:
  free(rebuilt);
  free(frame);
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i)
    free(options.edits[i].records);
  return ok ? 0 : 1;
}
