#define _POSIX_C_SOURCE 200809L
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

#define main po_inspect_example_main
#include "../examples/po_inspect.c"
#undef main

#define main po_diff_example_main
#include "../examples/po_diff.c"
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

  assert(!ensure_directory("missing-parent/decode_capture_dir"));

  /* load_file with valid file + NULL out pointers */
  {
    uint8_t *loaded = NULL;
    size_t loaded_len = 0u;
    int result;

    result = load_file("decode_capture.bin", NULL, &loaded_len);
    assert(!result);
    result = load_file("decode_capture.bin", &loaded, NULL);
    assert(!result);
  }

  /* load_file with empty file (0-byte) */
  {
    FILE *fp = fopen("decode_capture_empty.bin", "wb");
    uint8_t *loaded = NULL;
    size_t loaded_len = 99u;
    int result;

    assert(fp != NULL);
    fclose(fp);
    result = load_file("decode_capture_empty.bin", &loaded, &loaded_len);
    assert(result);
    assert(loaded_len == 0u);
    free(loaded);
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

  {
    FILE *fp = fopen("decode_capture_unsupported.wav", "wb");
    uint32_t sample_rate = 44100u;
    uint16_t channels = 1u;
    uint16_t audio_format = 1u;
    uint16_t bits_per_sample = 8u;
    uint16_t block_align = 1u;
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t fmt_size = 16u;
    uint32_t data_bytes = 1u;
    uint32_t file_size = 36u + data_bytes;
    uint8_t sample = 0x80u;

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
    fwrite(&sample, 1u, 1u, fp);
    fclose(fp);
  }
  ok = decode_wav("decode_capture_unsupported.wav", &wav);
  assert(!ok);
}

static void write_pcm24_wav(const char *path, const uint8_t *data, size_t frame_count,
                            uint32_t sample_rate, uint16_t channels) {
  FILE *fp = fopen(path, "wb");
  uint16_t bits = 24u;
  uint16_t audio_format = 1u;
  uint32_t fmt_size = 16u;
  size_t bytes_per_sample = 3u;
  uint32_t data_bytes = (uint32_t)(frame_count * channels * bytes_per_sample);
  uint32_t file_size = 36u + data_bytes;
  uint32_t byte_rate = sample_rate * channels * (uint32_t)bytes_per_sample;
  uint16_t block_align = (uint16_t)(channels * bytes_per_sample);

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
  fwrite(&bits, 2u, 1u, fp);
  fwrite("data", 1u, 4u, fp);
  fwrite(&data_bytes, 4u, 1u, fp);
  fwrite(data, 1u, data_bytes, fp);
  fclose(fp);
}

static void write_pcm32_wav(const char *path, const int32_t *samples, size_t sample_count,
                            uint32_t sample_rate, uint16_t channels) {
  FILE *fp = fopen(path, "wb");
  uint16_t bits = 32u;
  uint16_t audio_format = 1u;
  uint32_t fmt_size = 16u;
  uint32_t data_bytes = (uint32_t)(sample_count * sizeof(int32_t));
  uint32_t file_size = 36u + data_bytes;
  uint32_t byte_rate = sample_rate * channels * (uint32_t)sizeof(int32_t);
  uint16_t block_align = (uint16_t)(channels * sizeof(int32_t));

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
  fwrite(&bits, 2u, 1u, fp);
  fwrite("data", 1u, 4u, fp);
  fwrite(&data_bytes, 4u, 1u, fp);
  fwrite(samples, sizeof(int32_t), sample_count, fp);
  fclose(fp);
}

static void test_decode_wav_pcm24_and_pcm32(void) {
  int ok;

  /* 24-bit mono PCM */
  {
    wav_data_t wav24;
    /* Two frames: +0.5 and -0.5 (approx) in 24-bit */
    uint8_t pcm24[] = {0x00, 0x00, 0x40,  /* +0.5 * 8388608 = 4194304 = 0x400000 */
                       0x00, 0x00, 0xC0}; /* -0.5 * 8388608 = -4194304 = 0xC00000 (sign-extended) */
    memset(&wav24, 0, sizeof(wav24));
    write_pcm24_wav("decode_capture_pcm24.wav", pcm24, 2u, 44100u, 1u);
    ok = decode_wav("decode_capture_pcm24.wav", &wav24);
    assert(ok);
    assert(wav24.sample_count == 2u);
    assert(wav24.sample_rate == 44100u);
    assert(wav24.samples[0] > 0.4f && wav24.samples[0] < 0.6f);
    assert(wav24.samples[1] < -0.4f && wav24.samples[1] > -0.6f);
    free(wav24.samples);
  }

  /* 32-bit integer mono PCM */
  {
    wav_data_t wav32;
    int32_t pcm32[] = {1073741824, -1073741824}; /* ~0.5 and ~-0.5 */
    memset(&wav32, 0, sizeof(wav32));
    write_pcm32_wav("decode_capture_pcm32.wav", pcm32, 2u, 44100u, 1u);
    ok = decode_wav("decode_capture_pcm32.wav", &wav32);
    assert(ok);
    assert(wav32.sample_count == 2u);
    assert(wav32.samples[0] > 0.4f && wav32.samples[0] < 0.6f);
    assert(wav32.samples[1] < -0.4f && wav32.samples[1] > -0.6f);
    free(wav32.samples);
  }
}

static void test_decode_wav_edge_cases(void) {
  wav_data_t wav;
  int ok;

  /* WAV with zero channels → should fail */
  {
    FILE *fp = fopen("decode_capture_zeroch.wav", "wb");
    uint32_t sample_rate = 44100u;
    uint16_t channels = 0u;
    uint16_t audio_format = 1u;
    uint16_t bits_per_sample = 16u;
    uint16_t block_align = 2u;
    uint32_t byte_rate = 88200u;
    uint32_t fmt_size = 16u;
    uint32_t data_bytes = 4u;
    uint32_t file_size = 36u + data_bytes;
    int16_t samples[] = {0, 0};

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
    fwrite(samples, sizeof(int16_t), 2u, fp);
    fclose(fp);
  }
  memset(&wav, 0, sizeof(wav));
  ok = decode_wav("decode_capture_zeroch.wav", &wav);
  assert(!ok);

  /* Missing file */
  memset(&wav, 0, sizeof(wav));
  ok = decode_wav("nonexistent_file.wav", &wav);
  assert(!ok);

  /* WAV with misaligned data (pcm_len not divisible by bytes_per_sample * channels) */
  {
    FILE *fp = fopen("decode_capture_misaligned.wav", "wb");
    uint32_t sample_rate = 44100u;
    uint16_t channels = 1u;
    uint16_t audio_format = 1u;
    uint16_t bits_per_sample = 16u;
    uint16_t block_align = 2u;
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t fmt_size = 16u;
    uint32_t data_bytes = 3u;                   /* not divisible by 2 */
    uint32_t file_size = 36u + data_bytes + 1u; /* +1 for padding */
    uint8_t data[] = {0, 0, 0, 0};              /* 3 bytes data + 1 pad */

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
    fwrite(data, 1u, 4u, fp);
    fclose(fp);
  }
  memset(&wav, 0, sizeof(wav));
  ok = decode_wav("decode_capture_misaligned.wav", &wav);
  assert(!ok);

  /* WAV with no data chunk (only fmt) */
  {
    FILE *fp = fopen("decode_capture_nodata.wav", "wb");
    uint32_t sample_rate = 44100u;
    uint16_t channels = 1u;
    uint16_t audio_format = 1u;
    uint16_t bits_per_sample = 16u;
    uint16_t block_align = 2u;
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t fmt_size = 16u;
    uint32_t file_size = 28u; /* WAVE + fmt chunk only, no data chunk */

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
    fclose(fp);
  }
  memset(&wav, 0, sizeof(wav));
  ok = decode_wav("decode_capture_nodata.wav", &wav);
  assert(!ok);
}

static void test_decode_wav_extensible(void) {
  /* PCM32 with 24 valid bits, two mono samples at +0.5 and -0.5. */
  uint8_t bytes[] = {'R',  'I', 'F', 'F',  68, 0,    0,    0,    'W',  'A', 'V', 'E',  'f',
                     'm',  't', ' ', 40,   0,  0,    0,    0xfe, 0xff, 1,   0,   0x80, 0xbb,
                     0,    0,   0,   0xee, 2,  0,    4,    0,    32,   0,   22,  0,    24,
                     0,    4,   0,   0,    0,  1,    0,    0,    0,    0,   0,   0x10, 0,
                     0x80, 0,   0,   0xaa, 0,  0x38, 0x9b, 0x71, 'd',  'a', 't', 'a',  8,
                     0,    0,   0,   0,    0,  0,    0x40, 0,    0,    0,   0xc0};
  wav_data_t wav = {0};
  int ok;
  for (unsigned floating = 0; floating < 2; ++floating) {
    if (floating) {
      bytes[38] = 32;
      bytes[44] = 3;
      bytes[71] = 0x3f;
      bytes[75] = 0xbf;
    }
    assert(write_binary_file("decode_capture_extensible.wav", bytes, sizeof(bytes)));
    ok = decode_wav("decode_capture_extensible.wav", &wav);
    assert(ok);
    assert(wav.sample_rate == 48000u && wav.sample_count == 2u);
    assert(wav.samples[0] == 0.5f && wav.samples[1] == -0.5f);
    free(wav.samples);
  }
  bytes[59] ^= 1; /* A matching format number alone is not a valid subtype GUID. */
  assert(write_binary_file("decode_capture_extensible.wav", bytes, sizeof(bytes)));
  ok = decode_wav("decode_capture_extensible.wav", &wav);
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
  char *bad_output_argv[] = {"po32_decode_capture", "decode_capture_input.wav",
                             "decode_capture_file", NULL};
  char *bad_signal_argv[] = {"po32_decode_capture", "decode_capture_pcm16.wav",
                             "decode_capture_fail", NULL};
  int result;

  result = po32_decode_capture_example_main(1, usage_argv);
  assert(result == 64);
  result = po32_decode_capture_example_main(3, missing_argv);
  assert(result == 1);

  build_demo_transfer_wav("decode_capture_input.wav");
  result = po32_decode_capture_example_main(3, bad_output_argv);
  assert(result == 1);
  result = po32_decode_capture_example_main(3, good_argv);
  assert(result == 0);
  result = po32_decode_capture_example_main(3, bad_signal_argv);
  assert(result == 2);

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
  po32_patch_packet_t patch;
  po32_knob_packet_t knob;
  po32_reset_packet_t reset;
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

  patch.instrument = 1u;
  patch.side = PO32_PATCH_LEFT;
  po32_patch_params_zero(&patch.params);
  assert(po32_packet_encode(PO32_TAG_PATCH, &patch, &encoded) == PO32_OK);
  packet = encoded;
  packet.offset = 1u;
  assert(dump_packet(&packet, &ctx) == 0);

  knob.instrument = 1u;
  knob.kind = PO32_KNOB_PITCH;
  knob.value = 128u;
  assert(po32_packet_encode(PO32_TAG_KNOB, &knob, &encoded) == PO32_OK);
  packet = encoded;
  packet.offset = 3u;
  assert(dump_packet(&packet, &ctx) == 0);

  reset.instrument = 1u;
  assert(po32_packet_encode(PO32_TAG_RESET, &reset, &encoded) == PO32_OK);
  packet = encoded;
  packet.offset = 4u;
  assert(dump_packet(&packet, &ctx) == 0);

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
    FILE *fp = fopen("decode_capture_dump/packet_04_pattern/packet.txt", "rb");
    FILE *summary = fopen("decode_capture_dump/pattern_summary.txt", "rb");
    FILE *patch_info = fopen("decode_capture_dump/packet_01_patch/packet.txt", "rb");
    FILE *knob_info = fopen("decode_capture_dump/packet_02_knob/packet.txt", "rb");
    FILE *reset_info = fopen("decode_capture_dump/packet_03_reset/packet.txt", "rb");
    FILE *state_info = fopen("decode_capture_dump/packet_05_state/packet.txt", "rb");
    assert(fp != NULL);
    assert(summary != NULL);
    assert(patch_info != NULL);
    assert(knob_info != NULL);
    assert(reset_info != NULL);
    assert(state_info != NULL);
    fclose(fp);
    fclose(summary);
    fclose(patch_info);
    fclose(knob_info);
    fclose(reset_info);
    fclose(state_info);
  }

  ctx.output_dir = "decode_capture_file";
  ctx.pattern_summary_fp = NULL;
  assert(dump_packet(&packet, &ctx) == 1);
}

static void test_summary_helpers_direct(void) {
  po32_patch_packet_t patch;
  po32_knob_packet_t knob;
  po32_reset_packet_t reset;
  po32_state_packet_t state;
  po32_pattern_packet_t pattern;
  FILE *fp;

  memset(&patch, 0, sizeof(patch));
  patch.instrument = 7u;
  patch.side = PO32_PATCH_RIGHT;
  fp = fopen("decode_capture_patch_fields.txt", "w");
  assert(fp != NULL);
  write_patch_fields(fp, &patch);
  fclose(fp);

  memset(&knob, 0, sizeof(knob));
  knob.instrument = 2u;
  knob.kind = PO32_KNOB_MORPH;
  knob.value = 42u;
  fp = fopen("decode_capture_knob_fields.txt", "w");
  assert(fp != NULL);
  write_knob_fields(fp, &knob);
  fclose(fp);

  memset(&reset, 0, sizeof(reset));
  reset.instrument = 3u;
  fp = fopen("decode_capture_reset_fields.txt", "w");
  assert(fp != NULL);
  write_reset_fields(fp, &reset);
  fclose(fp);

  memset(&state, 0, sizeof(state));
  state.tempo = 140u;
  state.swing_times_12 = 5u;
  state.pattern_numbers[0] = 1u;
  state.pattern_numbers[1] = 3u;
  state.pattern_count = 2u;
  fp = fopen("decode_capture_state_fields.txt", "w");
  assert(fp != NULL);
  write_state_fields(fp, &state);
  fclose(fp);

  fp = fopen("decode_capture_state_summary.txt", "w");
  assert(fp != NULL);
  write_state_summary(fp, 9, &state);
  fclose(fp);
  write_state_summary(NULL, 9, &state);

  memset(&pattern, 0, sizeof(pattern));
  po32_pattern_init(&pattern, 5u);
  assert(po32_pattern_set_trigger(&pattern, 0u, 1u, 1u) == PO32_OK);
  assert(po32_pattern_set_trigger(&pattern, 1u, 2u, 3u) == PO32_OK);
  fp = fopen("decode_capture_pattern_fields.txt", "w");
  assert(fp != NULL);
  write_pattern_fields(fp, &pattern);
  fclose(fp);

  fp = fopen("decode_capture_pattern_summary.txt", "w");
  assert(fp != NULL);
  write_pattern_summary(fp, 4, &pattern);
  fclose(fp);
  write_pattern_summary(NULL, 4, &pattern);
}

static void forensics_write_file(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  size_t written = 0;
  int status;

  assert(file != NULL);
  if (length != 0u)
    written = fwrite(bytes, 1u, length, file);
  assert(length == 0u || written == length);
  status = fclose(file);
  assert(status == 0);
}

static void forensics_read_text(const char *path, char *out, size_t capacity) {
  FILE *file = fopen(path, "rb");
  long end;
  size_t length;
  size_t read = 0;
  int status;

  assert(file != NULL && capacity != 0u);
  status = fseek(file, 0, SEEK_END);
  assert(status == 0);
  end = ftell(file);
  assert(end >= 0 && (uintmax_t)end < capacity);
  length = (size_t)end;
  status = fseek(file, 0, SEEK_SET);
  assert(status == 0);
  if (length != 0u)
    read = fread(out, 1u, length, file);
  assert(length == 0u || read == length);
  out[length] = '\0';
  status = fclose(file);
  assert(status == 0);
}

static int forensics_run_cli(int (*entry)(int, char **), int argc, char **argv, char *stdout_text,
                             size_t stdout_capacity, char *stderr_text, size_t stderr_capacity) {
  const char *stdout_path = "forensics-cli-stdout.txt";
  const char *stderr_path = "forensics-cli-stderr.txt";
  FILE *stdout_file = fopen(stdout_path, "wb");
  FILE *stderr_file = fopen(stderr_path, "wb");
  int stdout_fd;
  int stderr_fd;
  int rc;
  int status;
  int stdout_close;
  int stderr_close;

  assert(stdout_file != NULL && stderr_file != NULL);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  stdout_fd = dup(STDOUT_FILENO);
  stderr_fd = dup(STDERR_FILENO);
  assert(stdout_fd >= 0 && stderr_fd >= 0);
  status = dup2(fileno(stdout_file), STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(fileno(stderr_file), STDERR_FILENO);
  assert(status == STDERR_FILENO);
  rc = entry(argc, argv);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  status = dup2(stdout_fd, STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(stderr_fd, STDERR_FILENO);
  assert(status == STDERR_FILENO);
  status = close(stdout_fd);
  assert(status == 0);
  status = close(stderr_fd);
  assert(status == 0);
  stdout_close = fclose(stdout_file);
  stderr_close = fclose(stderr_file);
  assert(stdout_close == 0 && stderr_close == 0);
  forensics_read_text(stdout_path, stdout_text, stdout_capacity);
  forensics_read_text(stderr_path, stderr_text, stderr_capacity);
  return rc;
}

static size_t forensics_frame33(uint8_t *frame, size_t capacity, uint16_t raw_tag,
                                uint8_t first_pattern_byte) {
  uint8_t settings[PO33_SETTINGS_BYTES] = {0};
  uint8_t pattern[PO33_PATTERN_BYTES] = {0};
  const uint8_t selector = 2u;
  const uint8_t raw[] = {0xa5u};
  po33_builder_t builder;
  size_t length = 0;
  size_t offset = 0;

  pattern[0] = first_pattern_byte;
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  while (offset < sizeof(settings)) {
    size_t count = sizeof(settings) - offset;
    if (count > PO_MAX_PAYLOAD)
      count = PO_MAX_PAYLOAD;
    assert(po33_builder_append_packet(&builder, PO33_TAG_SETTINGS, settings + offset, count,
                                      NULL) == PO_OK);
    offset += count;
  }
  assert(po33_builder_append_packet(&builder, PO33_TAG_PATTERN_SELECT, &selector, sizeof(selector),
                                    NULL) == PO_OK);
  offset = 0;
  while (offset < sizeof(pattern)) {
    size_t count = sizeof(pattern) - offset;
    if (count > PO_MAX_PAYLOAD)
      count = PO_MAX_PAYLOAD;
    assert(po33_builder_append_packet(&builder, PO33_TAG_PATTERN_DATA, pattern + offset, count,
                                      NULL) == PO_OK);
    offset += count;
  }
  assert(po33_builder_append_packet(&builder, raw_tag, raw, sizeof(raw), NULL) == PO_OK);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static size_t forensics_frame35(uint8_t *frame, size_t capacity, uint8_t record_count_hi,
                                uint8_t record_count_lo, uint8_t first_speech_byte) {
  const uint8_t pattern[] = {4u};
  const uint8_t speech_header[PO35_SPEECH_HEADER_BYTES] = {
      3u, record_count_hi, record_count_lo, 80u, 140u, 1u, 2u, 3u, 4u};
  uint8_t speech_data[PO35_SPEECH_RECORD_BYTES] = {0};
  const uint8_t raw[] = {0x50u, 0x60u};
  po35_builder_t builder;
  size_t length = 0;

  speech_data[0] = first_speech_byte;
  speech_data[PO35_SPEECH_RECORD_BYTES - 1u] = 0x40u;
  assert(po35_builder_init(&builder, frame, capacity) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_PATTERN_EMPTY, pattern, sizeof(pattern),
                                    NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_SPEECH_HEADER, speech_header,
                                    sizeof(speech_header), NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_SPEECH_DATA, speech_data,
                                    sizeof(speech_data), NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, PO35_TAG_SPEECH_END, NULL, 0u, NULL) == PO_OK);
  assert(po35_builder_append_packet(&builder, 0xabcdu, raw, sizeof(raw), NULL) == PO_OK);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static void test_capture_forensics_cli(void) {
  uint8_t before[8192];
  uint8_t after[8192];
  uint8_t po35[8192];
  char stdout_text[4096];
  char stderr_text[1024];
  size_t before_length;
  size_t after_length;
  size_t po35_length;
  int rc;
  char *inspect33[] = {"po_inspect", "--family", "po33", "forensics33-before.bin", NULL};
  char *inspect35[] = {"po_inspect", "--family", "po35", "forensics35.bin", NULL};
  char *inspect_bad[] = {"po_inspect", "--family", "po33", "forensics-invalid.bin", NULL};
  char *inspect_trailing[] = {"po_inspect", "--family", "po33", "forensics-trailing.bin", NULL};
  char *inspect_wrong_family[] = {"po_inspect", "--family", "po35", "forensics33-before.bin", NULL};
  char *diff_identical[] = {
      "po_diff", "--family", "po33", "forensics33-before.bin", "forensics33-identical.bin", NULL};
  char *diff_packet[] = {
      "po_diff", "--family", "po33", "forensics33-before.bin", "forensics33-packet.bin", NULL};
  char *diff_record[] = {
      "po_diff", "--family", "po33", "forensics33-before.bin", "forensics33-record.bin", NULL};
  char *diff35_record[] = {
      "po_diff", "--family", "po35", "forensics35.bin", "forensics35-record.bin", NULL};
  char *diff35_raw[] = {"po_diff",
                        "--family",
                        "po35",
                        "forensics35-invalid-before.bin",
                        "forensics35-invalid-after.bin",
                        NULL};
  char *diff_invalid[] = {
      "po_diff", "--family", "po33", "forensics33-before.bin", "forensics-invalid.bin", NULL};
  char *diff_trailing[] = {
      "po_diff", "--family", "po33", "forensics33-before.bin", "forensics-trailing.bin", NULL};

  before_length = forensics_frame33(before, sizeof(before), 0x1234u, 0x20u);
  after_length = forensics_frame33(after, sizeof(after), 0x4321u, 0x20u);
  po35_length = forensics_frame35(po35, sizeof(po35), 0u, 1u, 0x10u);
  forensics_write_file("forensics33-before.bin", before, before_length);
  forensics_write_file("forensics33-identical.bin", before, before_length);
  forensics_write_file("forensics33-packet.bin", after, after_length);
  after_length = forensics_frame33(after, sizeof(after), 0x1234u, 0x21u);
  forensics_write_file("forensics33-record.bin", after, after_length);
  forensics_write_file("forensics35.bin", po35, po35_length);
  po35_length = forensics_frame35(po35, sizeof(po35), 0u, 1u, 0x11u);
  forensics_write_file("forensics35-record.bin", po35, po35_length);
  po35_length = forensics_frame35(po35, sizeof(po35), 1u, 0u, 0x10u);
  forensics_write_file("forensics35-invalid-before.bin", po35, po35_length);
  po35_length = forensics_frame35(po35, sizeof(po35), 1u, 0u, 0x11u);
  forensics_write_file("forensics35-invalid-after.bin", po35, po35_length);
  forensics_write_file("forensics-invalid.bin", before, before_length - 1u);
  before[before_length] = 0x5au;
  forensics_write_file("forensics-trailing.bin", before, before_length + 1u);

  rc = forensics_run_cli(po_inspect_example_main, 4, inspect33, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(
      stdout_text,
      "po33 parser=ok complete=yes stopped=no packets=11 input=5695 consumed=5695 trailing=0 "
      "unparsed=0 state=0x521a selectors=patterns[2x1];sounds[-];speech[-];invalid=0/0/0 "
      "unknown=packets:1,bytes:1,tags[0x1234x1/1]\n"));
  rc = forensics_run_cli(po_inspect_example_main, 4, inspect35, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(
      stdout_text,
      "po35 parser=ok complete=yes stopped=no packets=5 input=4182 consumed=4182 trailing=0 "
      "unparsed=0 state=0x7b17 selectors=patterns[4x1];sounds[-];speech[3x1];invalid=0/0/0 "
      "unknown=packets:1,bytes:2,tags[0xabcdx1/2]\n"));

  rc = forensics_run_cli(po_diff_example_main, 5, diff_identical, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(stdout_text, "family=po33\n"
                              "bytes identical count=5695\n"
                              "packets topology identical count=11\n"
                              "packets payload identical count=11\n"
                              "records topology identical count=3\n"
                              "records payload identical count=3\n"));
  rc = forensics_run_cli(po_diff_example_main, 5, diff_packet, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(
      stdout_text,
      "family=po33\n"
      "bytes changed before=5695 after=5695 changed=11 first_offset=5684 before=0x4b after=0x38\n"
      "packets topology changed before=11 after=11 first_index=9 "
      "before=raw(tag=0x1234,raw_bytes=1) after=raw(tag=0x4321,raw_bytes=1)\n"
      "packets payload not-compared topology=changed\n"
      "records topology changed before=3 after=3 first_index=2 "
      "before=raw(tag=0x1234,packets=1,raw_bytes=1) "
      "after=raw(tag=0x4321,packets=1,raw_bytes=1)\n"
      "records payload not-compared topology=changed\n"));
  rc = forensics_run_cli(po_diff_example_main, 5, diff_record, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(
      stdout_text,
      "family=po33\n"
      "bytes changed before=5695 after=5695 changed=1062 first_offset=4630 before=0x00 after=0x01\n"
      "packets topology identical count=11\n"
      "packets payload changed count=1 first_index=4 first_raw_offset=0 before=0x20 after=0x21\n"
      "records topology identical count=3\n"
      "records payload changed count=1 first_index=1 first_packet=4 first_raw_offset=0 "
      "before=0x20 after=0x21 record=po33-pattern(selector_raw=2,packets=6,raw_bytes=1033)\n"));
  rc = forensics_run_cli(po_diff_example_main, 5, diff35_record, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(
      stdout_text,
      "family=po35\n"
      "bytes changed before=4182 after=4182 changed=63 first_offset=4119 before=0xf3 after=0xf4\n"
      "packets topology identical count=5\n"
      "packets payload changed count=1 first_index=2 first_raw_offset=0 before=0x10 after=0x11\n"
      "records topology identical count=3\n"
      "records payload changed count=1 first_index=1 first_packet=2 first_raw_offset=0 "
      "before=0x10 after=0x11 record=po35-speech(slot_raw=3,packets=3,raw_bytes=53)\n"));
  rc = forensics_run_cli(po_diff_example_main, 5, diff35_raw, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 0 && stderr_text[0] == '\0');
  assert(!strcmp(
      stdout_text,
      "family=po35\n"
      "bytes changed before=4182 after=4182 changed=63 first_offset=4119 before=0xfb after=0xfc\n"
      "packets topology identical count=5\n"
      "packets payload changed count=1 first_index=2 first_raw_offset=0 before=0x10 after=0x11\n"
      "records topology identical count=5\n"
      "records payload changed count=1 first_index=2 first_packet=2 first_raw_offset=0 "
      "before=0x10 after=0x11 record=raw(tag=0x73b0,packets=1,raw_bytes=44)\n"));

  rc = forensics_run_cli(po_inspect_example_main, 4, inspect_bad, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 1 && stdout_text[0] == '\0' && strstr(stderr_text, "not one complete po33 frame"));
  rc = forensics_run_cli(po_inspect_example_main, 4, inspect_trailing, stdout_text,
                         sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 1 && stdout_text[0] == '\0' && strstr(stderr_text, "not one complete po33 frame"));
  rc = forensics_run_cli(po_inspect_example_main, 4, inspect_wrong_family, stdout_text,
                         sizeof(stdout_text), stderr_text, sizeof(stderr_text));
  assert(rc == 1 && stdout_text[0] == '\0' && strstr(stderr_text, "not one complete po35 frame"));
  rc = forensics_run_cli(po_diff_example_main, 5, diff_invalid, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 1 && stdout_text[0] == '\0' &&
         strstr(stderr_text, "After input is not one complete"));
  rc = forensics_run_cli(po_diff_example_main, 5, diff_trailing, stdout_text, sizeof(stdout_text),
                         stderr_text, sizeof(stderr_text));
  assert(rc == 1 && stdout_text[0] == '\0' &&
         strstr(stderr_text, "After input is not one complete"));
}

int main(void) {
  test_decode_helpers();
  test_decode_wav_formats();
  test_decode_wav_pcm24_and_pcm32();
  test_decode_wav_edge_cases();
  test_decode_wav_extensible();
  test_decode_capture_main_paths();
  test_packet_dump_helpers();
  test_summary_helpers_direct();
  test_capture_forensics_cli();
  return 0;
}
