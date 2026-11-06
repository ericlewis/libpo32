/* Edit validated PO-33 templates while preserving raw metadata and references. */
#include "po33.h"
#include "po_capture.h"
#include "po_stereo_wav.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { EDIT_SAMPLE, EDIT_PATTERN, EDIT_PATTERN_BYTE } edit_kind_t;
typedef enum { CHANNEL_UNSET, CHANNEL_LEFT, CHANNEL_RIGHT, CHANNEL_MIX } edit_channel_t;
typedef struct {
  edit_kind_t kind;
  unsigned index;
  size_t offset;
  uint8_t value;
  const char *path;
  uint8_t *samples;
  size_t sound_record_index;
  size_t sample_count, allocation;
} edit_t;
typedef struct {
  po32_packet_t *packets;
  size_t count, capacity;
  int failed;
} template_t;

static void edit_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s frame.bin output.wav [--sample INDEX audio.wav]\n"
          "       [--pattern INDEX pattern.bin] [--pattern-byte INDEX OFFSET VALUE]\n"
          "       [--rate 44100|48000|96000] [--channel left|right|mix]\n"
          "Indexes and offsets are zero-based; numeric values accept decimal or 0x hex.\n"
          "Samples: mono/stereo PCM16/24/32 or float32 WAV at 8000..192000 Hz.\n"
          "Stereo requires --channel. Audio is resampled to the native clock;\n"
          "23438 Hz is the native WAV alias. Short inputs are padded with silence;\n"
          "long inputs grow existing block-aligned pools in 4096-sample blocks.\n"
          "Headers, trims and slice references stay unchanged: stored tails may\n"
          "extend past the original playback range. Device capacity is unverified.\n"
          "Pattern replacements must have the existing concatenated byte length.\n"
          "Default transfer rate: 48000 Hz. Output must not already exist.\n",
          program);
}
static int edit_number(const char *text, unsigned long long maximum, unsigned long long *out) {
  char *end;
  unsigned long long value;
  int base;
  if (!text || text[0] < '0' || text[0] > '9')
    return 0;
  base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
  errno = 0;
  value = strtoull(text, &end, base);
  if (errno || end == text || *end || value > maximum)
    return 0;
  *out = value;
  return 1;
}
static int edit_options(int argc, char **argv, edit_t *edits, size_t *count, uint32_t *rate,
                        edit_channel_t *channel) {
  int have_rate = 0, have_sample = 0;
  *count = 0;
  *rate = 48000;
  *channel = CHANNEL_UNSET;
  for (int i = 3; i < argc;) {
    edit_t edit = {0};
    unsigned long long index, offset, value;
    if (!strcmp(argv[i], "--rate")) {
      if (have_rate || i + 1 >= argc || !edit_number(argv[i + 1], 96000, &value) ||
          (value != 44100 && value != 48000 && value != 96000)) {
        fprintf(stderr, "Specify --rate once, using 44100, 48000, or 96000.\n");
        return 0;
      }
      *rate = (uint32_t)value;
      have_rate = 1;
      i += 2;
      continue;
    }
    if (!strcmp(argv[i], "--channel")) {
      if (*channel != CHANNEL_UNSET || i + 1 >= argc)
        return 0;
      if (!strcmp(argv[i + 1], "left"))
        *channel = CHANNEL_LEFT;
      else if (!strcmp(argv[i + 1], "right"))
        *channel = CHANNEL_RIGHT;
      else if (!strcmp(argv[i + 1], "mix"))
        *channel = CHANNEL_MIX;
      else
        return 0;
      i += 2;
      continue;
    }
    if (!strcmp(argv[i], "--sample") || !strcmp(argv[i], "--pattern")) {
      if (i + 2 >= argc || !edit_number(argv[i + 1], 255, &index)) {
        fprintf(stderr, "Expected index in 0..255 and input file after %s.\n", argv[i]);
        return 0;
      }
      edit.kind = !strcmp(argv[i], "--sample") ? EDIT_SAMPLE : EDIT_PATTERN;
      have_sample |= edit.kind == EDIT_SAMPLE;
      edit.index = (unsigned)index;
      edit.path = argv[i + 2];
      i += 3;
    } else if (!strcmp(argv[i], "--pattern-byte")) {
      if (i + 3 >= argc || !edit_number(argv[i + 1], 255, &index) ||
          !edit_number(argv[i + 2], (unsigned long long)SIZE_MAX, &offset) ||
          !edit_number(argv[i + 3], 255, &value)) {
        fprintf(stderr, "Expected --pattern-byte INDEX OFFSET VALUE, with VALUE in 0..255.\n");
        return 0;
      }
      edit.kind = EDIT_PATTERN_BYTE;
      edit.index = (unsigned)index;
      edit.offset = (size_t)offset;
      edit.value = (uint8_t)value;
      i += 4;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 0;
    }
    for (size_t j = 0; j < *count; j++) {
      const edit_t *previous = edits + j;
      if (previous->index != edit.index ||
          (previous->kind == EDIT_SAMPLE) != (edit.kind == EDIT_SAMPLE))
        continue;
      if (previous->kind != EDIT_PATTERN_BYTE || edit.kind != EDIT_PATTERN_BYTE ||
          previous->offset == edit.offset) {
        fprintf(stderr, "Conflicting or duplicate edits for index %u.\n", edit.index);
        return 0;
      }
    }
    edits[(*count)++] = edit;
  }
  return *channel == CHANNEL_UNSET || have_sample;
}
static int edit_load_file(const char *path, uint8_t **out, size_t *out_size) {
  FILE *fp = fopen(path, "rb");
  long length = 0;
  uint8_t *data = NULL;
  int ok = 0;
  if (!fp)
    return 0;
  if (fseek(fp, 0, SEEK_END) || (length = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET))
    goto cleanup;
  data = (uint8_t *)malloc(length ? (size_t)length : 1u);
  if (!data || fread(data, 1, (size_t)length, fp) != (size_t)length || ferror(fp))
    goto cleanup;
  ok = 1;
cleanup:
  if (fclose(fp))
    ok = 0;
  if (ok) {
    *out = data;
    *out_size = (size_t)length;
  } else
    free(data);
  return ok;
}
static int edit_collect(const po32_packet_t *packet, void *user) {
  template_t *frame = (template_t *)user;
  if (frame->count >= frame->capacity) {
    frame->failed = 1;
    return 1;
  }
  frame->packets[frame->count++] = *packet;
  return 0;
}
/* Repeated selectors are retained during no-op re-encoding but are ambiguous
 * edit targets. Unknown packets are never interpreted or removed. */
static int edit_region(const template_t *frame, uint16_t selector_tag, uint16_t data_tag,
                       unsigned index, uint8_t **out, size_t *out_size) {
  size_t total = 0, selectors = 0, cursor = 0;
  int selected = 0;
  uint8_t *bytes;
  for (size_t i = 0; i < frame->count; i++) {
    const po32_packet_t *p = frame->packets + i;
    if (p->tag_code == selector_tag) {
      if (p->payload_len != 1) {
        fprintf(stderr, "Malformed selector packet for requested section.\n");
        return 0;
      }
      selected = p->payload[0] == index;
      if (selected)
        selectors++;
    } else if (selected && p->tag_code == data_tag) {
      if (p->payload_len > SIZE_MAX - total)
        return 0;
      total += p->payload_len;
    }
  }
  if (selectors != 1 || total == 0) {
    fprintf(stderr, "Index %u must identify one existing populated section.\n", index);
    return 0;
  }
  bytes = (uint8_t *)malloc(total);
  if (!bytes)
    return 0;
  selected = 0;
  for (size_t i = 0; i < frame->count; i++) {
    const po32_packet_t *p = frame->packets + i;
    if (p->tag_code == selector_tag)
      selected = p->payload[0] == index;
    else if (selected && p->tag_code == data_tag) {
      memcpy(bytes + cursor, p->payload, p->payload_len);
      cursor += p->payload_len;
    }
  }
  *out = bytes;
  *out_size = total;
  return 1;
}
static void edit_replace_region(template_t *frame, uint16_t selector_tag, uint16_t data_tag,
                                unsigned index, const uint8_t *bytes) {
  size_t cursor = 0;
  int selected = 0;
  for (size_t i = 0; i < frame->count; i++) {
    po32_packet_t *p = frame->packets + i;
    if (p->tag_code == selector_tag)
      selected = p->payload[0] == index;
    else if (selected && p->tag_code == data_tag) {
      memcpy(p->payload, bytes + cursor, p->payload_len);
      cursor += p->payload_len;
    }
  }
}
/* Match the topology's record order, which omits selectors without data. The
 * region scan has already rejected duplicate/malformed target selectors. */
static size_t edit_sound_record_index(const template_t *frame, unsigned index) {
  size_t record_index = 0;
  int selected = 0, have_data = 0;
  for (size_t i = 0; i < frame->count; ++i) {
    const po32_packet_t *p = frame->packets + i;
    if (p->tag_code == PO33_TAG_SOUND_SELECT || p->tag_code == PO33_TAG_END) {
      if (have_data) {
        if (selected)
          return record_index;
        ++record_index;
      }
      have_data = 0;
      if (p->tag_code == PO33_TAG_END)
        break;
      selected = p->payload[0] == index;
    } else if (p->tag_code == PO33_TAG_SOUND_DATA)
      have_data = 1;
  }
  return SIZE_MAX;
}
static int edit_sample(const template_t *frame, edit_t *edit, const uint8_t *region, size_t size,
                       edit_channel_t channel) {
  po_audio_t audio = {0};
  uint8_t *encoded = NULL;
  size_t count = 0, written = 0;
  int ok = 0;
  if (size <= PO33_SOUND_HEADER_BYTES) {
    fprintf(stderr, "Sound %u has no populated pool to replace.\n", edit->index);
    return 0;
  }
  size -= PO33_SOUND_HEADER_BYTES;
  if (!po_audio_read(edit->path, &audio)) {
    fprintf(stderr, "Cannot read nonempty mono/stereo PCM or float WAV: %s\n", edit->path);
    goto cleanup;
  }
  if ((audio.channels == 2 && channel == CHANNEL_UNSET) ||
      (audio.channels == 1 && channel == CHANNEL_RIGHT)) {
    fprintf(stderr, "Stereo input requires --channel left|right|mix; mono has no right channel.\n");
    goto cleanup;
  }
  for (size_t i = 0; i < audio.frames * audio.channels; ++i)
    if (audio.samples[i] < -1.0f || audio.samples[i] > 1.0f) {
      fprintf(stderr, "Sample PCM must be normalized to [-1,1].\n");
      goto cleanup;
    }
  if (audio.channels == 2)
    for (size_t i = 0; i < audio.frames; ++i) {
      float left = audio.samples[2 * i], right = audio.samples[2 * i + 1];
      audio.samples[i] = channel == CHANNEL_LEFT    ? left
                         : channel == CHANNEL_RIGHT ? right
                                                    : left * 0.5f + right * 0.5f;
    }
  if (po33_sample_count_for_pcm(audio.frames, audio.rate, &count) != PO_OK || count == 0)
    goto cleanup;
  encoded = (uint8_t *)malloc(count);
  if (!encoded ||
      po33_samples_encode_f32(audio.samples, audio.frames, audio.rate, encoded, count, &written) !=
          PO_OK ||
      written != count)
    goto cleanup;
  /* Native-rate PCM export/import is lossless, including the negative-zero
   * alias. Replaced tail padding is canonical silence, never stale audio. */
  if (audio.rate == 23438u)
    for (size_t i = 0; i < count && i < size; ++i)
      if (audio.samples[i] * 32768.0f == po33_sample_decode(region[PO33_SOUND_HEADER_BYTES + i]))
        encoded[i] = region[PO33_SOUND_HEADER_BYTES + i];
  edit->sound_record_index = edit_sound_record_index(frame, edit->index);
  if (edit->sound_record_index == SIZE_MAX)
    goto cleanup;
  edit->samples = encoded;
  encoded = NULL;
  edit->sample_count = count;
  ok = 1;
cleanup:
  free(encoded);
  free(audio.samples);
  return ok;
}
static int edit_apply(template_t *frame, edit_t *edit, edit_channel_t channel) {
  uint16_t selector = edit->kind == EDIT_SAMPLE ? PO33_TAG_SOUND_SELECT : PO33_TAG_PATTERN_SELECT;
  uint16_t data_tag = edit->kind == EDIT_SAMPLE ? PO33_TAG_SOUND_DATA : PO33_TAG_PATTERN_DATA;
  uint8_t *region = NULL, *file = NULL;
  size_t size = 0, replacement_size = 0;
  int ok = 0;
  if (!edit_region(frame, selector, data_tag, edit->index, &region, &size))
    goto cleanup;
  if (edit->kind == EDIT_PATTERN_BYTE) {
    if (edit->offset >= size) {
      fprintf(stderr, "Pattern %u byte offset must be below %zu.\n", edit->index, size);
      goto cleanup;
    }
    region[edit->offset] = edit->value;
  } else if (edit->kind == EDIT_PATTERN) {
    if (!edit_load_file(edit->path, &file, &replacement_size) || replacement_size != size) {
      fprintf(stderr, "Pattern %u replacement must contain exactly %zu bytes.\n", edit->index,
              size);
      goto cleanup;
    }
    memcpy(region, file, size);
  } else {
    if (!edit_sample(frame, edit, region, size, channel))
      goto cleanup;
  }
  if (edit->kind != EDIT_SAMPLE)
    edit_replace_region(frame, selector, data_tag, edit->index, region);
  ok = 1;
cleanup:
  free(region);
  free(file);
  return ok;
}
static int edit_rebuild(const template_t *frame, uint8_t *output, size_t capacity, size_t *length) {
  po33_builder_t builder;
  if (po33_builder_init(&builder, output, capacity) != PO32_OK)
    return 0;
  for (size_t i = 0; i < frame->count; i++) {
    const po32_packet_t *p = frame->packets + i;
    if (p->tag_code == PO33_TAG_BEGIN || p->tag_code == PO33_TAG_END)
      continue;
    if (po33_builder_append_packet(&builder, p->tag_code, p->payload, p->payload_len, NULL) !=
        PO32_OK)
      return 0;
  }
  return po33_builder_finish(&builder, length) == PO32_OK;
}
/* Each transaction replaces only the in-memory working frame. No output file
 * exists until every sample and pattern edit has succeeded. Record indices
 * stay stable because a transaction changes data packets, not sound records. */
static int edit_sample_rebuild(uint8_t **frame, size_t *frame_bytes, edit_t *edit) {
  po33_pool_edit_plan_t plan;
  uint8_t *workspace = NULL, *output = NULL;
  size_t output_bytes = 0;
  int ok = 0;
  if (po33_pool_edit_plan(*frame, *frame_bytes, edit->sound_record_index, edit->samples,
                          edit->sample_count, &plan) != PO_OK) {
    fprintf(stderr, "Sound %u replacement rejected (pool reason %u).\n", edit->index,
            (unsigned)plan.replacement.reason);
    return 0;
  }
  workspace = (uint8_t *)malloc(plan.required_workspace_bytes);
  output = (uint8_t *)malloc(plan.required_frame_bytes);
  if (!workspace || !output ||
      po33_pool_edit_rebuild(*frame, *frame_bytes, edit->sound_record_index, edit->samples,
                             edit->sample_count, &plan, workspace, plan.required_workspace_bytes,
                             output, plan.required_frame_bytes, &output_bytes) != PO_OK)
    goto cleanup;
  free(*frame);
  *frame = output;
  *frame_bytes = output_bytes;
  edit->allocation = plan.replacement.required_allocation_bytes;
  output = NULL;
  ok = 1;
cleanup:
  free(workspace);
  free(output);
  return ok;
}
int main(int argc, char **argv) {
  uint8_t *input = NULL, *output = NULL;
  size_t input_length = 0, output_length = 0, edit_count = 0, output_capacity = 0;
  uint32_t rate;
  edit_channel_t channel;
  edit_t *edits = NULL;
  template_t frame = {0};
  po33_decode_result_t result;
  int rc = 1;
  if (argc == 2 && !strcmp(argv[1], "--help")) {
    edit_usage(argv[0]);
    return 0;
  }
  if (argc < 3) {
    edit_usage(argv[0]);
    return 2;
  }
  edits = (edit_t *)calloc((size_t)argc, sizeof(*edits));
  if (!edits)
    goto cleanup;
  if (!edit_options(argc, argv, edits, &edit_count, &rate, &channel)) {
    rc = 2;
    goto cleanup;
  }
  if (!edit_load_file(argv[1], &input, &input_length)) {
    fprintf(stderr, "Cannot read template frame: %s\n", argv[1]);
    goto cleanup;
  }
  if (po33_frame_parse(input, input_length, NULL, NULL, &result) != PO32_OK || !result.done ||
      result.frame_bytes != input_length) {
    fprintf(stderr,
            "Template must be one complete CRC-valid PO-33 frame without trailing bytes.\n");
    goto cleanup;
  }
  if (result.packet_count > SIZE_MAX / sizeof(*frame.packets))
    goto cleanup;
  frame.capacity = result.packet_count;
  frame.packets = (po32_packet_t *)calloc(frame.capacity, sizeof(*frame.packets));
  if (!frame.packets)
    goto cleanup;
  if (po33_frame_parse(input, input_length, edit_collect, &frame, &result) != PO32_OK ||
      !result.done || frame.failed)
    goto cleanup;
  for (size_t i = 0; i < edit_count; i++)
    if (!edit_apply(&frame, edits + i, channel))
      goto cleanup;
  output_capacity = input_length;
  output = (uint8_t *)malloc(output_capacity);
  if (!output)
    goto cleanup;
  if (!edit_rebuild(&frame, output, output_capacity, &output_length) ||
      output_length != output_capacity ||
      po33_frame_parse(output, output_length, NULL, NULL, &result) != PO32_OK || !result.done ||
      result.frame_bytes != output_length) {
    fprintf(stderr, "Failed to rebuild the edited template.\n");
    goto cleanup;
  }
  for (size_t i = 0; i < edit_count; ++i)
    if (edits[i].kind == EDIT_SAMPLE && !edit_sample_rebuild(&output, &output_length, edits + i))
      goto cleanup;
  if (po33_frame_parse(output, output_length, NULL, NULL, &result) != PO_OK || !result.done ||
      result.frame_bytes != output_length)
    goto cleanup;
  /* Output creation happens only after all input and edit validation. */
  if (!po33_write_transfer_wav(argv[2], output, output_length, rate)) {
    fprintf(stderr, "Cannot write transfer WAV; use a new output path.\n");
    goto cleanup;
  }
  printf("Encoded %zu packets with %zu edits at %u Hz.\n", result.packet_count, edit_count, rate);
  for (size_t i = 0; i < edit_count; ++i)
    if (edits[i].kind == EDIT_SAMPLE)
      printf("Sound %u: %zu converted samples, %zu allocated; original metadata retained.\n",
             edits[i].index, edits[i].sample_count, edits[i].allocation);
  rc = 0;
cleanup:
  for (size_t i = 0; i < edit_count; ++i)
    free(edits[i].samples);
  free(edits);
  free(frame.packets);
  free(input);
  free(output);
  return rc;
}
