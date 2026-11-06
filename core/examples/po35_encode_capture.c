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
  po35_builder_t builder;
  int failed;
} e35_copy_t;

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

static int e35_copy_packet(const po_packet_t *packet, void *user) {
  e35_copy_t *copy = (e35_copy_t *)user;
  if (po35_builder_append_packet(&copy->builder, packet->tag_code, packet->payload,
                                 packet->payload_len, NULL) != PO_OK) {
    copy->failed = 1;
    return 1;
  }
  return 0;
}

static size_t e35_edit_descriptors(const e35_options_t *options, po35_speech_edit_t *edits) {
  size_t count = 0u;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    const e35_edit_t *edit = options->edits + i;
    if (!edit->path)
      continue;
    edits[count++] = (po35_speech_edit_t){
        .slot = (uint8_t)i,
        .records_per_second = (uint8_t)options->fps,
        .records = edit->records,
        .record_bytes = edit->bytes,
    };
  }
  return count;
}

static void e35_report_plan_failure(const po35_speech_edit_plan_t *plan) {
  if (plan->reason == PO35_SPEECH_EDIT_REASON_ABSENT_SLOT)
    fprintf(stderr, "Speech slot %u is not present in the input frame.\n",
            (unsigned)plan->failed_slot);
  else if (plan->reason == PO35_SPEECH_EDIT_REASON_UNKNOWN_SELECTED_GROUP_EXTENSION)
    fprintf(stderr, "Speech slot %u contains an extension; replacement is ambiguous.\n",
            (unsigned)plan->failed_slot);
  else
    fprintf(stderr, "Input must contain complete, consistent PO-35 speech groups.\n");
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
  uint8_t *frame = NULL, *rebuilt = NULL, *workspace = NULL;
  size_t frame_len = 0, rebuilt_len = 0;
  e35_options_t options;
  int ok = 0;
  if (!e35_parse_options(argc, argv, &options)) {
    e35_usage(argv[0]);
    return 2;
  }
  if (!e35_read_file(argv[1], &frame, &frame_len)) {
    fprintf(stderr, "Cannot read input frame.\n");
    goto cleanup;
  }
  if (options.edit_count) {
    po35_speech_edit_t edits[PO35_SPEECH_SLOT_COUNT];
    po35_speech_edit_plan_t plan;
    size_t edit_count = e35_edit_descriptors(&options, edits);
    po_status_t status;

    if (edit_count != options.edit_count)
      goto cleanup;
    status = po35_speech_edit_plan(frame, frame_len, edits, edit_count, &plan);
    if (status != PO_OK) {
      e35_report_plan_failure(&plan);
      goto cleanup;
    }
    for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i)
      if (options.edits[i].path && !e35_analyze(options.edits + i, options.fps, options.channel))
        goto cleanup;
    edit_count = e35_edit_descriptors(&options, edits);
    if (edit_count != options.edit_count ||
        po35_speech_edit_plan(frame, frame_len, edits, edit_count, &plan) != PO_OK) {
      e35_report_plan_failure(&plan);
      goto cleanup;
    }
    workspace = (uint8_t *)malloc(plan.required_workspace_bytes);
    rebuilt = (uint8_t *)malloc(plan.required_frame_bytes);
    if (workspace == NULL || rebuilt == NULL ||
        po35_speech_edit_rebuild(frame, frame_len, edits, edit_count, &plan, workspace,
                                 plan.required_workspace_bytes, rebuilt, plan.required_frame_bytes,
                                 &rebuilt_len) != PO_OK ||
        rebuilt_len != plan.required_frame_bytes) {
      fprintf(stderr, "Cannot rebuild the requested PO-35 speech edit.\n");
      goto cleanup;
    }
  } else {
    e35_copy_t copy = {0};
    po35_decode_result_t result;
    rebuilt = (uint8_t *)malloc(frame_len ? frame_len : 1u);
    if (rebuilt == NULL || po35_builder_init(&copy.builder, rebuilt, frame_len) != PO_OK ||
        po35_frame_parse(frame, frame_len, e35_copy_packet, &copy, &result) != PO_OK ||
        !result.done || result.stopped || result.frame_bytes != frame_len || copy.failed ||
        po35_builder_finish(&copy.builder, &rebuilt_len) != PO_OK || rebuilt_len != frame_len ||
        memcmp(frame, rebuilt, frame_len)) {
      fprintf(stderr, "Input must contain exactly one complete, byte-preserving PO-35 frame.\n");
      goto cleanup;
    }
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
  free(workspace);
  free(rebuilt);
  free(frame);
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i)
    free(options.edits[i].records);
  return ok ? 0 : 1;
}
