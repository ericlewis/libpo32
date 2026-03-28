#include "po32.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
  float *samples;
  size_t sample_count;
  uint32_t sample_rate;
} wav_data_t;

typedef struct {
  const char *output_dir;
  int packet_index;
  FILE *pattern_summary_fp;
} packet_dump_ctx_t;

static uint16_t read_le16(const uint8_t *data) {
  return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data) {
  return (uint32_t)data[0] |
         (uint32_t)((uint32_t)data[1] << 8) |
         (uint32_t)((uint32_t)data[2] << 16) |
         (uint32_t)((uint32_t)data[3] << 24);
}

static int ensure_directory(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  if (mkdir(path, 0777) == 0) {
    return 1;
  }
  return errno == EEXIST;
}

static int write_binary_file(const char *path, const void *data, size_t data_len) {
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return 0;
  }
  if (fwrite(data, 1u, data_len, fp) != data_len) {
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}

static int load_file(const char *path, uint8_t **out_data, size_t *out_len) {
  FILE *fp = fopen(path, "rb");
  uint8_t *data;
  long len_long;

  if (fp == NULL || out_data == NULL || out_len == NULL) {
    if (fp != NULL) fclose(fp);
    return 0;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 0;
  }
  len_long = ftell(fp);
  if (len_long < 0) {
    fclose(fp);
    return 0;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return 0;
  }

  data = (uint8_t *)malloc((size_t)len_long);
  if (data == NULL) {
    fclose(fp);
    return 0;
  }
  if (fread(data, 1u, (size_t)len_long, fp) != (size_t)len_long) {
    free(data);
    fclose(fp);
    return 0;
  }
  fclose(fp);
  *out_data = data;
  *out_len = (size_t)len_long;
  return 1;
}

static float pcm16_to_float(int16_t value) {
  return (float)value / 32768.0f;
}

static float pcm24_to_float(const uint8_t *data) {
  int32_t value = (int32_t)data[0] |
                  ((int32_t)data[1] << 8) |
                  ((int32_t)data[2] << 16);
  if ((value & 0x00800000) != 0) {
    value |= (int32_t)0xFF000000;
  }
  return (float)value / 8388608.0f;
}

static float pcm32_to_float(int32_t value) {
  return (float)value / 2147483648.0f;
}

static int decode_wav(const char *path, wav_data_t *out_wav) {
  uint8_t *file_data = NULL;
  size_t file_len = 0u;
  size_t pos = 12u;
  uint16_t audio_format = 0u;
  uint16_t channels = 0u;
  uint16_t bits_per_sample = 0u;
  uint32_t sample_rate = 0u;
  const uint8_t *pcm = NULL;
  uint32_t pcm_len = 0u;
  float *samples = NULL;
  size_t frame_count;

  if (out_wav == NULL || !load_file(path, &file_data, &file_len)) {
    return 0;
  }
  if (file_len < 44u || memcmp(file_data, "RIFF", 4u) != 0 || memcmp(file_data + 8u, "WAVE", 4u) != 0) {
    free(file_data);
    return 0;
  }

  while (pos + 8u <= file_len) {
    const uint8_t *chunk = file_data + pos;
    uint32_t chunk_size = read_le32(chunk + 4u);
    size_t next_pos = pos + 8u + chunk_size + (chunk_size & 1u);
    if (next_pos > file_len) {
      free(file_data);
      return 0;
    }

    if (memcmp(chunk, "fmt ", 4u) == 0 && chunk_size >= 16u) {
      audio_format = read_le16(chunk + 8u);
      channels = read_le16(chunk + 10u);
      sample_rate = read_le32(chunk + 12u);
      bits_per_sample = read_le16(chunk + 22u);
    } else if (memcmp(chunk, "data", 4u) == 0) {
      pcm = chunk + 8u;
      pcm_len = chunk_size;
    }
    pos = next_pos;
  }

  if (pcm == NULL || channels == 0u || sample_rate == 0u) {
    free(file_data);
    return 0;
  }

  {
    size_t bytes_per_sample = (size_t)((bits_per_sample + 7u) / 8u);
    if (bytes_per_sample == 0u || pcm_len % (bytes_per_sample * channels) != 0u) {
      free(file_data);
      return 0;
    }
    frame_count = pcm_len / (bytes_per_sample * channels);
    samples = (float *)malloc(frame_count * sizeof(*samples));
    if (samples == NULL) {
      free(file_data);
      return 0;
    }

    for (size_t frame_index = 0u; frame_index < frame_count; ++frame_index) {
      float mix = 0.0f;
      for (uint16_t ch = 0u; ch < channels; ++ch) {
        const uint8_t *sample_ptr = pcm + (frame_index * channels + ch) * bytes_per_sample;
        if (audio_format == 1u && bits_per_sample == 16u) {
          mix += pcm16_to_float((int16_t)read_le16(sample_ptr));
        } else if (audio_format == 1u && bits_per_sample == 24u) {
          mix += pcm24_to_float(sample_ptr);
        } else if (audio_format == 1u && bits_per_sample == 32u) {
          mix += pcm32_to_float((int32_t)read_le32(sample_ptr));
        } else if (audio_format == 3u && bits_per_sample == 32u) {
          float value;
          memcpy(&value, sample_ptr, sizeof(value));
          mix += value;
        } else {
          free(samples);
          free(file_data);
          return 0;
        }
      }
      samples[frame_index] = mix / (float)channels;
    }
  }

  free(file_data);
  out_wav->samples = samples;
  out_wav->sample_count = frame_count;
  out_wav->sample_rate = sample_rate;
  return 1;
}

static void write_patch_fields(FILE *fp, const po32_patch_packet_t *patch) {
  fprintf(fp, "instrument=%u\n", patch->instrument);
  fprintf(fp, "side=%s\n", patch->side == PO32_PATCH_LEFT ? "left" : "right");
}

static void write_knob_fields(FILE *fp, const po32_knob_packet_t *knob) {
  fprintf(fp, "instrument=%u\n", knob->instrument);
  fprintf(fp, "kind=%s\n", knob->kind == PO32_KNOB_PITCH ? "pitch" : "morph");
  fprintf(fp, "value=%u\n", knob->value);
}

static void write_reset_fields(FILE *fp, const po32_reset_packet_t *reset) {
  fprintf(fp, "instrument=%u\n", reset->instrument);
}

static void write_state_fields(FILE *fp, const po32_state_packet_t *state) {
  fprintf(fp, "tempo=%u\n", state->tempo);
  fprintf(fp, "swing_times_12=%u\n", state->swing_times_12);
  fprintf(fp, "pattern_count=%zu\n", state->pattern_count);
  fputs("pattern_numbers=", fp);
  for (size_t i = 0u; i < state->pattern_count; ++i) {
    fprintf(fp, "%s%u", i == 0u ? "" : ",", state->pattern_numbers[i]);
  }
  fputc('\n', fp);
}

static void write_pattern_fields(FILE *fp, const po32_pattern_packet_t *pattern) {
  fprintf(fp, "pattern_number=%u\n", pattern->pattern_number);
  fprintf(fp, "accent_bits=0x%04x\n", pattern->accent_bits);
  for (size_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    fprintf(fp, "lane_%zu=", lane + 1u);
    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane * PO32_PATTERN_STEP_COUNT + step;
      const po32_pattern_step_t *entry = &pattern->steps[index];
      if (entry->instrument == 0u) {
        fputs(step == 0u ? "--" : ",--", fp);
      } else {
        fprintf(fp,
                "%s%u/f%u%s",
                step == 0u ? "" : ",",
                entry->instrument,
                entry->fill_rate,
                entry->accent ? "a" : "");
      }
    }
    fputc('\n', fp);
  }
}

static void write_pattern_summary(FILE *fp, int packet_index, const po32_pattern_packet_t *pattern) {
  if (fp == NULL) {
    return;
  }
  fprintf(fp,
          "packet_%02d_pattern: pattern_number=%u accent_bits=0x%04x\n",
          packet_index,
          pattern->pattern_number,
          pattern->accent_bits);
  for (size_t lane = 0u; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    int any = 0;
    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane * PO32_PATTERN_STEP_COUNT + step;
      if (pattern->steps[index].instrument != 0u) {
        any = 1;
        break;
      }
    }
    if (!any) {
      continue;
    }
    fprintf(fp, "lane_%zu=", lane + 1u);
    for (size_t step = 0u; step < PO32_PATTERN_STEP_COUNT; ++step) {
      size_t index = lane * PO32_PATTERN_STEP_COUNT + step;
      const po32_pattern_step_t *entry = &pattern->steps[index];
      if (entry->instrument == 0u) {
        fputs(step == 0u ? "--" : ",--", fp);
      } else {
        fprintf(fp,
                "%s%u/f%u%s",
                step == 0u ? "" : ",",
                entry->instrument,
                entry->fill_rate,
                entry->accent ? "a" : "");
      }
    }
    fputc('\n', fp);
  }
  fputc('\n', fp);
}

static void write_state_summary(FILE *fp, int packet_index, const po32_state_packet_t *state) {
  if (fp == NULL) {
    return;
  }
  fprintf(fp,
          "packet_%02d_state: tempo=%u swing_times_12=%u pattern_count=%zu\n",
          packet_index,
          state->tempo,
          state->swing_times_12,
          state->pattern_count);
  fputs("pattern_numbers=", fp);
  for (size_t i = 0u; i < state->pattern_count; ++i) {
    fprintf(fp, "%s%u", i == 0u ? "" : ",", state->pattern_numbers[i]);
  }
  fputc('\n', fp);
  fputc('\n', fp);
}

static int dump_packet(const po32_packet_t *packet, void *user) {
  packet_dump_ctx_t *ctx = (packet_dump_ctx_t *)user;
  char dir_path[512];
  char payload_path[640];
  char info_path[640];
  FILE *fp;

  ++ctx->packet_index;
  (void)snprintf(dir_path, sizeof(dir_path), "%s/packet_%02d_%s",
                 ctx->output_dir, ctx->packet_index, po32_tag_name(packet->tag_code));
  if (!ensure_directory(dir_path)) {
    return 1;
  }

  (void)snprintf(payload_path, sizeof(payload_path), "%s/payload.bin", dir_path);
  if (!write_binary_file(payload_path, packet->payload, packet->payload_len)) {
    return 1;
  }

  (void)snprintf(info_path, sizeof(info_path), "%s/packet.txt", dir_path);
  fp = fopen(info_path, "w");
  if (fp == NULL) {
    return 1;
  }

  fprintf(fp, "tag=%s\n", po32_tag_name(packet->tag_code));
  fprintf(fp, "tag_code=0x%04x\n", packet->tag_code);
  fprintf(fp, "offset=%zu\n", packet->offset);
  fprintf(fp, "payload_len=%zu\n", packet->payload_len);
  fprintf(fp, "trailer_lo=0x%02x\n", packet->trailer.lo);
  fprintf(fp, "trailer_hi=0x%02x\n", packet->trailer.hi);
  fprintf(fp, "trailer_state=0x%04x\n", packet->trailer.state);
  fprintf(fp, "trailer_matches_state=%d\n", packet->trailer.matches_state);

  if (packet->tag_code == PO32_TAG_PATCH) {
    po32_patch_packet_t decoded;
    if (po32_packet_decode(packet->tag_code, packet->payload, packet->payload_len, &decoded) == PO32_OK) {
      write_patch_fields(fp, &decoded);
    }
  } else if (packet->tag_code == PO32_TAG_KNOB) {
    po32_knob_packet_t decoded;
    if (po32_packet_decode(packet->tag_code, packet->payload, packet->payload_len, &decoded) == PO32_OK) {
      write_knob_fields(fp, &decoded);
    }
  } else if (packet->tag_code == PO32_TAG_RESET) {
    po32_reset_packet_t decoded;
    if (po32_packet_decode(packet->tag_code, packet->payload, packet->payload_len, &decoded) == PO32_OK) {
      write_reset_fields(fp, &decoded);
    }
  } else if (packet->tag_code == PO32_TAG_STATE) {
    po32_state_packet_t decoded;
    if (po32_packet_decode(packet->tag_code, packet->payload, packet->payload_len, &decoded) == PO32_OK) {
      write_state_fields(fp, &decoded);
      write_state_summary(ctx->pattern_summary_fp, ctx->packet_index, &decoded);
    }
  } else if (packet->tag_code == PO32_TAG_PATTERN) {
    po32_pattern_packet_t decoded;
    if (po32_packet_decode(packet->tag_code, packet->payload, packet->payload_len, &decoded) == PO32_OK) {
      write_pattern_fields(fp, &decoded);
      write_pattern_summary(ctx->pattern_summary_fp, ctx->packet_index, &decoded);
    }
  }

  fclose(fp);
  return 0;
}

int main(int argc, char **argv) {
  wav_data_t wav;
  uint8_t *frame = NULL;
  size_t frame_len = 0u;
  packet_dump_ctx_t dump_ctx;
  po32_decode_result_t result;
  po32_final_tail_t tail;
  char summary_path[512];
  char pattern_summary_path[512];
  FILE *summary_fp;
  FILE *pattern_summary_fp;

  if (argc != 3) {
    fprintf(stderr, "usage: %s <input.wav> <output_dir>\n", argv[0]);
    return 64;
  }

  if (!decode_wav(argv[1], &wav)) {
    fprintf(stderr, "failed to decode wav: %s\n", argv[1]);
    return 1;
  }
  if (!ensure_directory(argv[2])) {
    fprintf(stderr, "failed to create output dir: %s\n", argv[2]);
    free(wav.samples);
    return 1;
  }

  frame = (uint8_t *)malloc(65536u);
  if (frame == NULL) {
    free(wav.samples);
    return 1;
  }

  if (po32_decode_f32(wav.samples,
                      wav.sample_count,
                      (float)wav.sample_rate,
                      &result,
                      frame,
                      65536u,
                      &frame_len) != PO32_OK) {
    fprintf(stderr, "po32_decode_f32 failed\n");
    free(wav.samples);
    free(frame);
    return 2;
  }

  {
    char frame_path[512];
    char body_path[512];
    (void)snprintf(frame_path, sizeof(frame_path), "%s/frame.bin", argv[2]);
    (void)snprintf(body_path, sizeof(body_path), "%s/body.bin", argv[2]);
    if (!write_binary_file(frame_path, frame, frame_len)) {
      free(wav.samples);
      free(frame);
      return 1;
    }
    if (frame_len >= PO32_PREAMBLE_BYTES + PO32_FINAL_TAIL_BYTES) {
      (void)write_binary_file(body_path,
                              frame + PO32_PREAMBLE_BYTES,
                              frame_len - PO32_PREAMBLE_BYTES - PO32_FINAL_TAIL_BYTES);
    }
  }

  dump_ctx.output_dir = argv[2];
  dump_ctx.packet_index = 0;
  (void)snprintf(pattern_summary_path, sizeof(pattern_summary_path), "%s/pattern_summary.txt", argv[2]);
  pattern_summary_fp = fopen(pattern_summary_path, "w");
  dump_ctx.pattern_summary_fp = pattern_summary_fp;
  if (po32_frame_parse(frame, frame_len, dump_packet, &dump_ctx, &tail) != PO32_OK) {
    if (pattern_summary_fp != NULL) fclose(pattern_summary_fp);
    free(wav.samples);
    free(frame);
    return 3;
  }
  if (pattern_summary_fp != NULL) {
    fclose(pattern_summary_fp);
  }

  (void)snprintf(summary_path, sizeof(summary_path), "%s/summary.txt", argv[2]);
  summary_fp = fopen(summary_path, "w");
  if (summary_fp != NULL) {
    fprintf(summary_fp, "sample_rate=%u\n", wav.sample_rate);
    fprintf(summary_fp, "sample_count=%zu\n", wav.sample_count);
    fprintf(summary_fp, "packet_count=%d\n", result.packet_count);
    fprintf(summary_fp, "done=%d\n", result.done);
    fprintf(summary_fp, "frame_len=%zu\n", frame_len);
    fprintf(summary_fp, "final_tail_state=0x%04x\n", tail.final_state);
    fclose(summary_fp);
  }

  printf("decoded packets=%d frame_len=%zu output=%s\n", result.packet_count, frame_len, argv[2]);
  free(wav.samples);
  free(frame);
  return 0;
}
