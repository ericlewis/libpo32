/* Edit validated PO-33 frame templates without inferring metadata or resizing. */
#include "po33.h"
#include "po_stereo_wav.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { EDIT_SAMPLE, EDIT_PATTERN, EDIT_PATTERN_BYTE } edit_kind_t;
typedef struct {
  edit_kind_t kind;
  unsigned index;
  size_t offset;
  uint8_t value;
  const char *path;
} edit_t;
typedef struct {
  po32_packet_t *packets;
  size_t count, capacity;
  int failed;
} template_t;

static uint16_t edit_read16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
static uint32_t edit_read32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void edit_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s frame.bin output.wav [--sample INDEX mono.wav]\n"
          "       [--pattern INDEX pattern.bin] [--pattern-byte INDEX OFFSET VALUE]\n"
          "       [--rate 44100|48000|96000]\n"
          "Indexes and offsets are zero-based; numeric values accept decimal or 0x hex.\n"
          "Sample input: mono PCM16 WAV at 23438 Hz, exactly the existing pool length.\n"
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
static int edit_options(int argc, char **argv, edit_t *edits, size_t *count, uint32_t *rate) {
  int have_rate = 0;
  *count = 0;
  *rate = 48000;
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
    if (!strcmp(argv[i], "--sample") || !strcmp(argv[i], "--pattern")) {
      if (i + 2 >= argc || !edit_number(argv[i + 1], 255, &index)) {
        fprintf(stderr, "Expected index in 0..255 and input file after %s.\n", argv[i]);
        return 0;
      }
      edit.kind = !strcmp(argv[i], "--sample") ? EDIT_SAMPLE : EDIT_PATTERN;
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
  return 1;
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
static int edit_load_pcm(const char *path, size_t expected_samples, uint8_t **out_file,
                         const uint8_t **out_pcm) {
  uint8_t *file = NULL;
  const uint8_t *pcm = NULL;
  size_t size = 0, position = 12, pcm_size = 0;
  int format_seen = 0, data_seen = 0, ok = 0;
  if (!edit_load_file(path, &file, &size) || size < 12 || memcmp(file, "RIFF", 4) ||
      memcmp(file + 8, "WAVE", 4) || edit_read32(file + 4) < 4 ||
      (size_t)edit_read32(file + 4) != size - 8)
    goto cleanup;
  while (position < size) {
    const uint8_t *chunk;
    size_t length;
    if (size - position < 8)
      goto cleanup;
    chunk = file + position;
    length = edit_read32(chunk + 4);
    if (length > size - position - 8)
      goto cleanup;
    if (!memcmp(chunk, "fmt ", 4)) {
      if (format_seen || length < 16 || edit_read16(chunk + 8) != 1 ||
          edit_read16(chunk + 10) != 1 || edit_read32(chunk + 12) != 23438 ||
          edit_read32(chunk + 16) != 23438u * 2u || edit_read16(chunk + 20) != 2 ||
          edit_read16(chunk + 22) != 16)
        goto cleanup;
      format_seen = 1;
    } else if (!memcmp(chunk, "data", 4)) {
      if (data_seen)
        goto cleanup;
      data_seen = 1;
      pcm = chunk + 8;
      pcm_size = length;
    }
    position += 8 + length;
    if (length & 1u) {
      if (position == size)
        goto cleanup;
      position++;
    }
  }
  if (!format_seen || !data_seen || pcm_size % 2 || pcm_size / 2 != expected_samples)
    goto cleanup;
  *out_file = file;
  *out_pcm = pcm;
  ok = 1;
cleanup:
  if (!ok) {
    free(file);
    fprintf(stderr, "Sample WAV must be mono PCM16 at 23438 Hz with exactly %zu frames.\n",
            expected_samples);
  }
  return ok;
}
static int edit_apply(template_t *frame, const edit_t *edit) {
  uint16_t selector = edit->kind == EDIT_SAMPLE ? PO33_TAG_SOUND_SELECT : PO33_TAG_PATTERN_SELECT;
  uint16_t data_tag = edit->kind == EDIT_SAMPLE ? PO33_TAG_SOUND_DATA : PO33_TAG_PATTERN_DATA;
  uint8_t *region = NULL, *file = NULL;
  size_t size = 0, replacement_size = 0;
  const uint8_t *pcm = NULL;
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
    if (size <= 9) {
      fprintf(stderr,
              "Sound %u has metadata only; replacing a referenced or empty pool is unsupported.\n",
              edit->index);
      goto cleanup;
    }
    if (!edit_load_pcm(edit->path, size - 9, &file, &pcm))
      goto cleanup;
    for (size_t i = 0; i < size - 9; i++) {
      uint16_t word = edit_read16(pcm + 2 * i);
      int16_t sample = (int16_t)(word >= 0x8000u ? (int32_t)word - 65536 : (int32_t)word);
      /* Retain every unchanged code, including the FF negative-zero alias. */
      if (sample != po33_sample_decode(region[9 + i]))
        region[9 + i] = po33_sample_encode(sample);
    }
  }
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
int main(int argc, char **argv) {
  uint8_t *input = NULL, *output = NULL;
  size_t input_length = 0, output_length = 0, edit_count = 0;
  uint32_t rate;
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
  if (!edit_options(argc, argv, edits, &edit_count, &rate)) {
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
  output = (uint8_t *)malloc(input_length);
  if (!frame.packets || !output)
    goto cleanup;
  if (po33_frame_parse(input, input_length, edit_collect, &frame, &result) != PO32_OK ||
      !result.done || frame.failed)
    goto cleanup;
  for (size_t i = 0; i < edit_count; i++)
    if (!edit_apply(&frame, edits + i))
      goto cleanup;
  if (!edit_rebuild(&frame, output, input_length, &output_length) ||
      output_length != input_length ||
      po33_frame_parse(output, output_length, NULL, NULL, &result) != PO32_OK || !result.done ||
      result.frame_bytes != output_length) {
    fprintf(stderr, "Failed to rebuild the template without changing its packet layout.\n");
    goto cleanup;
  }
  /* Output creation happens only after all input and edit validation. */
  if (!po33_write_transfer_wav(argv[2], output, output_length, rate)) {
    fprintf(stderr, "Cannot write transfer WAV; use a new output path.\n");
    goto cleanup;
  }
  printf("Encoded %zu packets with %zu edits at %u Hz.\n", frame.count, edit_count, rate);
  rc = 0;
cleanup:
  free(edits);
  free(frame.packets);
  free(input);
  free(output);
  return rc;
}
