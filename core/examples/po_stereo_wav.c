#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "po_stereo_wav.h"
#include "po33.h"
#include "po35.h"
#include "po_stereo.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void wav_le16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}
static void wav_le32(uint8_t *out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[i] = (uint8_t)(value >> (8 * i));
}
static int write_stereo_wav(const char *path, const uint8_t *frame, size_t frame_len,
                            uint32_t rate) {
  po_stereo_modulator_t modulator;
  uint8_t header[44] = {0}, pcm[4096];
  float samples[2048];
  size_t total, count;
  FILE *fp = NULL;
  int fd = -1, created = 0, ok = 0;
  if (!path || po_stereo_modulator_init(&modulator, frame, frame_len, rate) != PO32_OK)
    return 0;
  total = po_stereo_modulator_remaining(&modulator);
  if (total > (UINT32_MAX - 36u) / 4u)
    return 0;
  memcpy(header, "RIFF", 4);
  wav_le32(header + 4, (uint32_t)(36u + total * 4u));
  memcpy(header + 8, "WAVEfmt ", 8);
  wav_le32(header + 16, 16);
  wav_le16(header + 20, 1);
  wav_le16(header + 22, 2);
  wav_le32(header + 24, rate);
  wav_le32(header + 28, rate * 4u);
  wav_le16(header + 32, 4);
  wav_le16(header + 34, 16);
  memcpy(header + 36, "data", 4);
  wav_le32(header + 40, (uint32_t)(total * 4u));
  fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    return 0;
  created = 1;
  fp = fdopen(fd, "wb");
  if (!fp)
    goto cleanup;
  fd = -1;
  if (fwrite(header, 1, sizeof(header), fp) != sizeof(header))
    goto cleanup;
  while (!po_stereo_modulator_done(&modulator)) {
    if (po_stereo_modulator_render_f32(&modulator, samples, 1024, &count) != PO32_OK || count == 0)
      goto cleanup;
    for (size_t i = 0; i < count * 2u; ++i)
      wav_le16(pcm + i * 2u, (uint16_t)(int16_t)lround((double)samples[i] * 32767.0));
    if (fwrite(pcm, 4u, count, fp) != count)
      goto cleanup;
  }
  if (fclose(fp) != 0) {
    fp = NULL;
    goto cleanup;
  }
  fp = NULL;
  ok = 1;
cleanup:
  if (fp)
    fclose(fp);
  if (fd >= 0)
    close(fd);
  if (!ok && created)
    unlink(path);
  return ok;
}
int po33_write_transfer_wav(const char *path, const uint8_t *frame, size_t frame_len,
                            uint32_t rate) {
  po33_decode_result_t result;
  if (!frame || frame_len > UINT32_MAX / 4u ||
      po33_frame_parse(frame, frame_len, NULL, NULL, &result) != PO32_OK || !result.done ||
      result.frame_bytes != frame_len)
    return 0;
  return write_stereo_wav(path, frame, frame_len, rate);
}
int po35_write_transfer_wav(const char *path, const uint8_t *frame, size_t frame_len,
                            uint32_t rate) {
  po35_decode_result_t result;
  if (!frame || frame_len > UINT32_MAX / 4u ||
      po35_frame_parse(frame, frame_len, NULL, NULL, &result) != PO_OK || !result.done ||
      result.frame_bytes != frame_len)
    return 0;
  return write_stereo_wav(path, frame, frame_len, rate);
}
