/*
 * po32_demo — minimal CLI that builds a transfer, renders audio,
 * decodes it back, and synthesizes a drum hit.
 *
 * Build:
 *   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
 *   cmake --build build --target po32_demo
 *   ./build/po32_demo
 */

#include "po32.h"
#include "po32_synth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Write a mono 16-bit WAV to disk. */
static int write_wav(const char *path, const float *samples, size_t sample_count,
                     uint32_t sample_rate_hz) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return -1;

  uint32_t data_bytes = (uint32_t)(sample_count * 2u);
  uint32_t file_size = 36 + data_bytes;
  uint16_t channels = 1, bits = 16;
  uint32_t byte_rate = sample_rate_hz * channels * (bits / 8);
  uint16_t block_align = channels * (bits / 8);

  fwrite("RIFF", 1, 4, fp);
  fwrite(&file_size, 4, 1, fp);
  fwrite("WAVEfmt ", 1, 8, fp);
  uint32_t fmt_size = 16;
  fwrite(&fmt_size, 4, 1, fp);
  uint16_t pcm = 1;
  fwrite(&pcm, 2, 1, fp);
  fwrite(&channels, 2, 1, fp);
  fwrite(&sample_rate_hz, 4, 1, fp);
  fwrite(&byte_rate, 4, 1, fp);
  fwrite(&block_align, 2, 1, fp);
  fwrite(&bits, 2, 1, fp);
  fwrite("data", 1, 4, fp);
  fwrite(&data_bytes, 4, 1, fp);

  for (size_t i = 0; i < sample_count; i++) {
    float s = samples[i];
    if (s > 1.0f) s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    int16_t pcm_sample = (int16_t)(s * 32767.0f);
    fwrite(&pcm_sample, 2, 1, fp);
  }

  fclose(fp);
  return 0;
}

int main(void) {
  const uint32_t sample_rate = 44100;

  /* ── 1. Build a transfer frame with one patch ── */

  po32_patch_params_t kick = {0};
  po32_patch_params_zero(&kick);
  kick.OscWave = 0.0f;   /* sine */
  kick.OscFreq = 0.18f;  /* ~130 Hz */
  kick.OscAtk  = 0.0f;
  kick.OscDcy  = 0.45f;
  kick.ModMode = 0.0f;   /* decay mod */
  kick.ModRate = 0.0f;
  kick.ModAmt  = 0.55f;
  kick.Mix     = 0.0f;   /* osc only */
  kick.Level   = 0.88f;

  uint8_t frame_buf[8192];
  po32_builder_t builder;
  po32_patch_packet_t patch_packet;
  po32_packet_t encoded_packet;
  po32_builder_init(&builder, frame_buf, sizeof(frame_buf));

  patch_packet.instrument = 1u;
  patch_packet.side = PO32_PATCH_LEFT;
  patch_packet.params = kick;
  if (po32_packet_encode(PO32_TAG_PATCH, &patch_packet, &encoded_packet) != PO32_OK) {
    fprintf(stderr, "failed to encode demo patch packet\n");
    return 1;
  }
  if (po32_builder_append(&builder, &encoded_packet) != PO32_OK) {
    fprintf(stderr, "failed to append demo patch packet\n");
    return 1;
  }

  size_t frame_len = 0;
  if (po32_builder_finish(&builder, &frame_len) != PO32_OK) {
    fprintf(stderr, "failed to finish demo frame\n");
    return 1;
  }

  printf("frame: %zu bytes\n", frame_len);

  /* ── 2. Render the frame to audio ── */

  size_t modem_count = po32_render_sample_count(frame_len, sample_rate);
  float *modem = malloc(modem_count * sizeof(float));
  po32_render_dpsk_f32(frame_buf, frame_len, sample_rate, modem, modem_count);

  printf("modem audio: %zu samples (%.2f s)\n",
         modem_count, (float)modem_count / (float)sample_rate);

  /* ── 3. Decode it back ── */

  po32_decode_result_t result;
  uint8_t decoded_frame[65536];
  size_t decoded_len = 0;
  po32_decode_f32(modem, modem_count, (float)sample_rate,
                  &result, decoded_frame, sizeof(decoded_frame), &decoded_len);

  printf("decoded: %zu bytes, %d packets, tail=%s\n",
         decoded_len, result.packet_count,
         result.done ? "yes" : "no");

  int match = (decoded_len == frame_len &&
               memcmp(frame_buf, decoded_frame, frame_len) == 0);
  printf("roundtrip: %s\n", match ? "PASS" : "FAIL");

  /* ── 4. Synthesize a drum hit ── */

  po32_synth_t synth;
  po32_synth_init(&synth, sample_rate);

  size_t hit_count = po32_synth_samples_for_duration(&synth, 0.5f);
  float *hit = malloc(hit_count * sizeof(float));
  size_t hit_len = 0;
  po32_synth_render(&synth, &kick, 100, 0.5f,
                    hit, hit_count, &hit_len);

  printf("synth: %zu samples (%.2f s)\n",
         hit_len, (float)hit_len / (float)sample_rate);

  /* ── 5. Write WAV files ── */

  if (write_wav("demo_modem.wav", modem, modem_count, sample_rate) == 0)
    printf("wrote demo_modem.wav\n");

  if (write_wav("demo_kick.wav", hit, hit_len, sample_rate) == 0)
    printf("wrote demo_kick.wav\n");

  free(modem);
  free(hit);
  return match ? 0 : 1;
}
