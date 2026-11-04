#include "po_capture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int po_audio_read(const char *path, po_audio_t *out) {
  FILE *fp;
  po_audio_t audio = {0};
  if (out == NULL)
    return 0;
  *out = (po_audio_t){0};
  if (path == NULL)
    return 0;
  fp = fopen(path, "rb");
  uint8_t *file = NULL;
  const uint8_t *pcm = NULL;
  long length;
  size_t size, pos, pcm_size = 0, width;
  uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
  uint32_t rate = 0;
  int ok = 0;
  if (!fp)
    return 0;
  if (fseek(fp, 0, SEEK_END) || (length = ftell(fp)) < 12 || fseek(fp, 0, SEEK_SET))
    goto cleanup;
  size = (size_t)length;
  file = (uint8_t *)malloc(size);
  if (!file || fread(file, 1, size, fp) != size || memcmp(file, "RIFF", 4) ||
      memcmp(file + 8, "WAVE", 4) || le32(file + 4) < 4 || (size_t)le32(file + 4) > size - 8)
    goto cleanup;
  /* Bytes following RIFF are not chunks belonging to this waveform. */
  size = (size_t)le32(file + 4) + 8;
  for (pos = 12; pos <= size - 8;) {
    const uint8_t *chunk = file + pos;
    size_t chunk_size = le32(chunk + 4);
    if (chunk_size > size - pos - 8)
      goto cleanup;
    if (!memcmp(chunk, "fmt ", 4) && chunk_size >= 16) {
      format = le16(chunk + 8);
      channels = le16(chunk + 10);
      rate = le32(chunk + 12);
      block_align = le16(chunk + 20);
      bits = le16(chunk + 22);
      if (format == 0xfffe) {
        static const uint8_t pcm_guid[16] = {1,    0, 0, 0,    0, 0,    0x10, 0,
                                             0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
        static const uint8_t float_guid[16] = {3,    0, 0, 0,    0, 0,    0x10, 0,
                                               0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
        uint16_t extension_size, valid_bits;
        if (chunk_size < 40)
          goto cleanup;
        extension_size = le16(chunk + 24);
        valid_bits = le16(chunk + 26);
        if (extension_size < 22 || extension_size > chunk_size - 18 || valid_bits == 0 ||
            valid_bits > bits)
          goto cleanup;
        if (!memcmp(chunk + 32, pcm_guid, sizeof(pcm_guid)))
          format = 1;
        else if (!memcmp(chunk + 32, float_guid, sizeof(float_guid)) && valid_bits == 32)
          format = 3;
        else
          goto cleanup;
      }
    } else if (!memcmp(chunk, "data", 4)) {
      pcm = chunk + 8;
      pcm_size = chunk_size;
    }
    pos += 8 + chunk_size;
    if (chunk_size & 1) {
      if (pos == size)
        break;
      ++pos;
    }
  }
  if (!pcm || (channels != 1 && channels != 2) || rate < 8000 || rate > 192000 ||
      !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)))
    goto cleanup;
  width = bits / 8;
  if (block_align != channels * width || pcm_size % block_align)
    goto cleanup;
  audio.frames = pcm_size / block_align;
  if (audio.frames == 0 || audio.frames > SIZE_MAX / (channels * sizeof(float)))
    goto cleanup;
  audio.samples = (float *)malloc(audio.frames * channels * sizeof(float));
  if (!audio.samples)
    goto cleanup;
  for (size_t i = 0; i < audio.frames * channels; ++i) {
    const uint8_t *p = pcm + i * width;
    double value;
    if (format == 3) {
      uint32_t word = le32(p);
      float sample;
      memcpy(&sample, &word, sizeof(sample));
      value = sample;
    } else if (bits == 16) {
      uint16_t word = le16(p);
      value = (word >= 0x8000u ? (int32_t)word - 65536 : (int32_t)word) / 32768.0;
    } else if (bits == 24) {
      uint32_t word = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
      value = (word >= 0x800000u ? (int32_t)word - 16777216 : (int32_t)word) / 8388608.0;
    } else {
      uint32_t word = le32(p);
      value = ((double)word - (word >= 0x80000000u ? 4294967296.0 : 0.0)) / 2147483648.0;
    }
    if (!isfinite(value))
      goto cleanup;
    audio.samples[i] = (float)value;
  }
  audio.rate = rate;
  audio.channels = channels;
  ok = 1;
cleanup:
  free(file);
  if (fclose(fp) != 0)
    ok = 0;
  if (ok)
    *out = audio;
  else
    free(audio.samples);
  return ok;
}

int po_capture_read(const char *path, po_capture_t *out) {
  po_audio_t audio = {0};
  if (out == NULL)
    return 0;
  *out = (po_capture_t){0};
  if (!po_audio_read(path, &audio))
    return 0;
  if (audio.channels != 2 || audio.rate < 32000 || audio.frames < audio.rate / 2) {
    free(audio.samples);
    return 0;
  }
  out->samples = audio.samples;
  out->frames = audio.frames;
  out->rate = audio.rate;
  return 1;
}

int po_capture_decode(const po_capture_t *capture, po_stereo_channel_order_t order,
                      uint8_t **out_wire, size_t *out_length) {
  po_stereo_workspace_t work = {0};
  uint8_t *wire = NULL;
  size_t wire_capacity = 0, length = 0;
  int ok = 0;
  if (out_wire == NULL || out_length == NULL)
    return 0;
  *out_wire = NULL;
  *out_length = 0;
  if (capture == NULL || capture->samples == NULL || capture->frames == 0)
    return 0;
  if (po_stereo_decode_requirements(capture->frames, capture->rate, &work.scalar_count,
                                    &work.byte_count, &wire_capacity) != PO32_OK ||
      work.scalar_count > (size_t)-1 / sizeof(double))
    return 0;
  work.scalars = (double *)malloc(work.scalar_count * sizeof(double));
  work.bytes = (uint8_t *)malloc(work.byte_count);
  wire = (uint8_t *)malloc(wire_capacity);
  if (work.scalars != NULL && work.bytes != NULL && wire != NULL &&
      po_stereo_decode_f32(capture->samples, capture->frames, capture->rate, order, &work, wire,
                           wire_capacity, &length) == PO32_OK) {
    *out_wire = wire;
    *out_length = length;
    ok = 1;
  }
  if (!ok)
    free(wire);
  free(work.scalars);
  free(work.bytes);
  return ok;
}
