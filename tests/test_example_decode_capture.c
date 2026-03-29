#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main po32_decode_capture_example_main
#include "../examples/po32_decode_capture.c"
#undef main

static void write_pcm16_wav(const char *path, const int16_t *samples, size_t sample_count,
                            uint32_t sample_rate, uint16_t channels) {
  FILE *fp = fopen(path, "wb");
  uint32_t data_bytes = (uint32_t)(sample_count * sizeof(int16_t));
  uint32_t file_size = 36u + data_bytes;
  uint32_t byte_rate = sample_rate * channels * (uint32_t)sizeof(int16_t);
  uint16_t block_align = (uint16_t)(channels * sizeof(int16_t));
  uint16_t audio_format = 1u;
  uint16_t bits_per_sample = 16u;
  uint32_t fmt_size = 16u;

  assert(fp != NULL);
  fwrite("RIFF", 1u, 4u, fp);
  fwrite(&file_size, 4u, 1u, fp);
  fwrite("WAVEfmt ", 1u, 8u, fp);
  fwrite(&fmt_size, 4u, 1u, fp);
  fwrite(&audio_format, 2u, 1u, fp);
  fwrite(&channels, 2u, 1u, fp);
  fwrite(&sample_rate, 4u, 1u, fp);
  fwrite(&byte_rate, 4u, 1u, fp);
  fwrite(&block_align, 2u, 1u, fp);
  fwrite(&bits_per_sample, 2u, 1u, fp);
  fwrite("data", 1u, 4u, fp);
  fwrite(&data_bytes, 4u, 1u, fp);
  fwrite(samples, sizeof(int16_t), sample_count, fp);
  fclose(fp);
}

static void write_float32_stereo_wav(const char *path, const float *samples, size_t frame_count,
                                     uint32_t sample_rate) {
  FILE *fp = fopen(path, "wb");
  uint16_t channels = 2u;
  uint16_t bits_per_sample = 32u;
  uint16_t audio_format = 3u;
  uint32_t fmt_size = 16u;
  uint32_t data_bytes = (uint32_t)(frame_count * channels * sizeof(float));
  uint32_t file_size = 36u + data_bytes;
  uint32_t byte_rate = sample_rate * channels * (uint32_t)sizeof(float);
  uint16_t block_align = (uint16_t)(channels * sizeof(float));

  assert(fp != NULL);
  fwrite("RIFF", 1u, 4u, fp);
  fwrite(&file_size, 4u, 1u, fp);
  fwrite("WAVEfmt ", 1u, 8u, fp);
  fwrite(&fmt_size, 4u, 1u, fp);
  fwrite(&audio_format, 2u, 1u, fp);
  fwrite(&channels, 2u, 1u, fp);
  fwrite(&sample_rate, 4u, 1u, fp);
  fwrite(&byte_rate, 4u, 1u, fp);
  fwrite(&block_align, 2u, 1u, fp);
  fwrite(&bits_per_sample, 2u, 1u, fp);
  fwrite("data", 1u, 4u, fp);
  fwrite(&data_bytes, 4u, 1u, fp);
  fwrite(samples, sizeof(float), frame_count * channels, fp);
  fclose(fp);
}

static void build_demo_transfer_wav(const char *path) {
  po32_patch_params_t patch;
  po32_patch_packet_t patch_packet;
  po32_packet_t encoded_packet;
  po32_builder_t builder;
  uint8_t frame[2048];
  size_t frame_len = 0u;
  const uint32_t sample_rate = 44100u;
  size_t sample_count;
  float *samples;

  po32_patch_params_zero(&patch);
  patch.OscFreq = 0.28f;
  patch.OscDcy = 0.42f;
  patch.Level = 0.88f;

  patch_packet.instrument = 1u;
  patch_packet.side = PO32_PATCH_LEFT;
  patch_packet.params = patch;
  assert(po32_packet_encode(PO32_TAG_PATCH, &patch_packet, &encoded_packet) == PO32_OK);

  po32_builder_init(&builder, frame, sizeof(frame));
  assert(po32_builder_append(&builder, &encoded_packet) == PO32_OK);
  assert(po32_builder_finish(&builder, &frame_len) == PO32_OK);

  sample_count = po32_render_sample_count(frame_len, sample_rate);
  samples = (float *)malloc(sample_count * sizeof(*samples));
  assert(samples != NULL);
  assert(po32_render_dpsk_f32(frame, frame_len, sample_rate, samples, sample_count) == PO32_OK);

  {
    int16_t *pcm = (int16_t *)malloc(sample_count * sizeof(*pcm));
    assert(pcm != NULL);
    for (size_t i = 0u; i < sample_count; ++i) {
      float sample = samples[i];
      if (sample > 1.0f)
        sample = 1.0f;
      if (sample < -1.0f)
        sample = -1.0f;
      pcm[i] = (int16_t)(sample * 32767.0f);
    }
    write_pcm16_wav(path, pcm, sample_count, sample_rate, 1u);
    free(pcm);
  }

  free(samples);
}

static void test_decode_helpers(void) {
  int ok;

  assert(read_le16((const uint8_t *)"\x34\x12") == 0x1234u);
  assert(read_le32((const uint8_t *)"\x78\x56\x34\x12") == 0x12345678u);

  assert(ensure_directory("decode_capture_dir"));
  assert(ensure_directory("decode_capture_dir"));

  {
    FILE *fp = fopen("decode_capture_file", "wb");
    assert(fp != NULL);
    fputs("x", fp);
    fclose(fp);
  }
  assert(!ensure_directory("decode_capture_file"));

  {
    const uint8_t bytes[] = {1u, 2u, 3u};
    uint8_t *loaded = NULL;
    size_t loaded_len = 0u;
    assert(write_binary_file("decode_capture.bin", bytes, sizeof(bytes)));
    ok = load_file("decode_capture.bin", &loaded, &loaded_len);
    assert(ok);
    assert(loaded_len == sizeof(bytes));
    assert(memcmp(bytes, loaded, sizeof(bytes)) == 0);
    free(loaded);
  }

  assert(!write_binary_file("missing-dir/out.bin", "x", 1u));
  ok = load_file("missing.bin", NULL, NULL);
  assert(!ok);

  {
    FILE *fp = fopen("decode_capture_short.bin", "wb");
    uint8_t bytes[] = {1u, 2u, 3u};
    assert(fp != NULL);
    fwrite(bytes, 1u, sizeof(bytes), fp);
    fclose(fp);
  }
}

static void test_decode_wav_formats(void) {
  wav_data_t wav;
  int16_t pcm16[] = {0, 32767, -32768, 16384};
  float stereo_float[] = {0.25f, -0.25f, 0.5f, -0.5f};
  int ok;

  memset(&wav, 0, sizeof(wav));

  write_pcm16_wav("decode_capture_pcm16.wav", pcm16, sizeof(pcm16) / sizeof(pcm16[0]), 44100u, 1u);
  ok = decode_wav("decode_capture_pcm16.wav", &wav);
  assert(ok);
  assert(wav.sample_rate == 44100u);
  assert(wav.sample_count == 4u);
  free(wav.samples);
  wav.samples = NULL;

  write_float32_stereo_wav("decode_capture_float.wav", stereo_float, 2u, 22050u);
  ok = decode_wav("decode_capture_float.wav", &wav);
  assert(ok);
  assert(wav.sample_rate == 22050u);
  assert(wav.sample_count == 2u);
  free(wav.samples);
  wav.samples = NULL;

  {
    FILE *fp = fopen("decode_capture_bad.wav", "wb");
    assert(fp != NULL);
    fwrite("notawav", 1u, 7u, fp);
    fclose(fp);
  }
  ok = decode_wav("decode_capture_bad.wav", &wav);
  assert(!ok);

  {
    FILE *fp = fopen("decode_capture_truncated.wav", "wb");
    uint32_t sample_rate = 44100u;
    uint16_t channels = 1u;
    uint16_t audio_format = 1u;
    uint16_t bits_per_sample = 16u;
    uint16_t block_align = 2u;
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t fmt_size = 16u;
    uint32_t data_bytes = 2u;
    uint32_t file_size = 36u + data_bytes;

    assert(fp != NULL);
    fwrite("RIFF", 1u, 4u, fp);
    fwrite(&file_size, 4u, 1u, fp);
    fwrite("WAVEfmt ", 1u, 8u, fp);
    fwrite(&fmt_size, 4u, 1u, fp);
    fwrite(&audio_format, 2u, 1u, fp);
    fwrite(&channels, 2u, 1u, fp);
    fwrite(&sample_rate, 4u, 1u, fp);
    fwrite(&byte_rate, 4u, 1u, fp);
    fwrite(&block_align, 2u, 1u, fp);
    fwrite(&bits_per_sample, 2u, 1u, fp);
    fwrite("data", 1u, 4u, fp);
    fwrite(&data_bytes, 4u, 1u, fp);
    fclose(fp);
  }
  ok = decode_wav("decode_capture_truncated.wav", &wav);
  assert(!ok);
}

static void test_decode_capture_main_paths(void) {
  char *usage_argv[] = {"po32_decode_capture", NULL};
  char *missing_argv[] = {"po32_decode_capture", "missing.wav", "out", NULL};
  char *good_argv[] = {
      "po32_decode_capture",
      "decode_capture_input.wav",
      "decode_capture_out",
      NULL,
  };
  int result;

  result = po32_decode_capture_example_main(1, usage_argv);
  assert(result == 64);
  result = po32_decode_capture_example_main(3, missing_argv);
  assert(result == 1);

  build_demo_transfer_wav("decode_capture_input.wav");
  result = po32_decode_capture_example_main(3, good_argv);
  assert(result == 0);

  {
    FILE *summary = fopen("decode_capture_out/summary.txt", "rb");
    FILE *pattern_summary = fopen("decode_capture_out/pattern_summary.txt", "rb");
    assert(summary != NULL);
    assert(pattern_summary != NULL);
    fclose(summary);
    fclose(pattern_summary);
  }
}

static void test_packet_dump_helpers(void) {
  packet_dump_ctx_t ctx;
  po32_packet_t packet;
  po32_pattern_packet_t pattern;
  po32_state_packet_t state;
  po32_packet_t encoded;
  FILE *pattern_summary;

  assert(ensure_directory("decode_capture_dump"));
  pattern_summary = fopen("decode_capture_dump/pattern_summary.txt", "w");
  assert(pattern_summary != NULL);

  ctx.output_dir = "decode_capture_dump";
  ctx.packet_index = 0;
  ctx.pattern_summary_fp = pattern_summary;

  memset(&pattern, 0, sizeof(pattern));
  po32_pattern_init(&pattern, 2u);
  assert(po32_pattern_set_trigger(&pattern, 0u, 1u, 1u) == PO32_OK);
  assert(po32_packet_encode(PO32_TAG_PATTERN, &pattern, &encoded) == PO32_OK);
  packet = encoded;
  packet.offset = 5u;
  assert(dump_packet(&packet, &ctx) == 0);

  memset(&state, 0, sizeof(state));
  po32_morph_pairs_default(state.morph_pairs, PO32_STATE_MORPH_PAIR_COUNT);
  state.tempo = 120u;
  state.swing_times_12 = 0u;
  state.pattern_numbers[0] = 2u;
  state.pattern_count = 1u;
  assert(po32_packet_encode(PO32_TAG_STATE, &state, &encoded) == PO32_OK);
  packet = encoded;
  packet.offset = 17u;
  assert(dump_packet(&packet, &ctx) == 0);

  fclose(pattern_summary);

  {
    FILE *fp = fopen("decode_capture_dump/packet_01_pattern/packet.txt", "rb");
    FILE *summary = fopen("decode_capture_dump/pattern_summary.txt", "rb");
    assert(fp != NULL);
    assert(summary != NULL);
    fclose(fp);
    fclose(summary);
  }
}

int main(void) {
  test_decode_helpers();
  test_decode_wav_formats();
  test_decode_capture_main_paths();
  test_packet_dump_helpers();
  return 0;
}
