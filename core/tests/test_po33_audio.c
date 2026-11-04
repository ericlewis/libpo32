#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float pcm[192000];
static uint8_t encoded[65536], again[65536];

static void test_count(void) {
  static const uint32_t rates[] = {8000, 24000, 44100, 48000, 192000};
  size_t count = 99;
  po_status_t status;
  for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
    status = po33_sample_count_for_pcm(rates[i], rates[i], &count);
    assert(status == PO_OK && count == 23438);
    status = po33_sample_count_for_pcm(2u * rates[i], rates[i], &count);
    assert(status == PO_OK && count == 46875);
  }
  status = po33_sample_count_for_pcm(333, 23438, &count);
  assert(status == PO_OK && count == 333);
  status = po33_sample_count_for_pcm(1, 192000, &count);
  assert(status == PO_OK && count == 1);
  status = po33_sample_count_for_pcm(0, 48000, &count);
  assert(status == PO_OK && count == 0);
  status = po33_sample_count_for_pcm(1, 7999, &count);
  assert(status == PO_ERR_RANGE && count == 0);
  status = po33_sample_count_for_pcm(1, 192001, &count);
  assert(status == PO_ERR_RANGE && count == 0);
  status = po33_sample_count_for_pcm((size_t)-1, 48000, &count);
  assert(status == PO_ERR_RANGE && count == 0);
  /* These fit a 32-bit float-array extent but overflow naive ratio products. */
  status = po33_sample_count_for_pcm(1073741823u, 8000, &count);
  assert(status == PO_OK && count == UINT32_C(3145727998));
  status = po33_sample_count_for_pcm(1073741823u, 48000, &count);
  assert(status == PO_OK && count == 524288000u);
  status = po33_sample_count_for_pcm(1, 48000, NULL);
  assert(status == PO_ERR_INVALID_ARG);
}

static void assert_untouched(void) {
  for (size_t i = 0; i < sizeof(encoded); ++i)
    assert(encoded[i] == 0xa5);
}

static void test_contract(void) {
  size_t count = 99;
  po_status_t status;
  memset(encoded, 0xa5, sizeof(encoded));
  status = po33_samples_encode_f32(NULL, 0, 48000, NULL, 0, &count);
  assert(status == PO_OK && count == 0);
  status = po33_samples_encode_f32(NULL, 1, 48000, encoded, sizeof(encoded), &count);
  assert(status == PO_ERR_INVALID_ARG && count == 0);
  status = po33_samples_encode_f32(pcm, 1, 48000, NULL, sizeof(encoded), &count);
  assert(status == PO_ERR_INVALID_ARG && count == 0);
  status = po33_samples_encode_f32(pcm, 1, 48000, encoded, sizeof(encoded), NULL);
  assert(status == PO_ERR_INVALID_ARG);
  status = po33_samples_encode_f32(pcm, 48000, 48000, encoded, 23437, &count);
  assert(status == PO_ERR_BUFFER_TOO_SMALL && count == 0);
  status = po33_samples_encode_f32(pcm, 48000, 7999, encoded, sizeof(encoded), &count);
  assert(status == PO_ERR_RANGE && count == 0);
  status = po33_samples_encode_f32(pcm, SIZE_MAX, 23438, encoded, sizeof(encoded), &count);
  assert(status == PO_ERR_RANGE && count == 0);
  pcm[47999] = NAN;
  status = po33_samples_encode_f32(pcm, 48000, 48000, encoded, sizeof(encoded), &count);
  assert(status == PO_ERR_RANGE && count == 0);
  pcm[47999] = INFINITY;
  status = po33_samples_encode_f32(pcm, 48000, 48000, encoded, sizeof(encoded), &count);
  assert(status == PO_ERR_RANGE && count == 0);
  pcm[47999] = -1.01f;
  status = po33_samples_encode_f32(pcm, 48000, 48000, encoded, sizeof(encoded), &count);
  assert(status == PO_ERR_RANGE && count == 0);
  pcm[47999] = 0.0f;
  assert_untouched();
  status = po33_samples_encode_f32(pcm, 1, 192000, encoded, sizeof(encoded), &count);
  assert(status == PO_OK && count == 1 && encoded[0] == 0);
  for (size_t i = 1; i < sizeof(encoded); ++i)
    assert(encoded[i] == 0xa5);
}

static void test_native_alias(void) {
  size_t count;
  po_status_t status;
  for (size_t i = 0; i < 65536; ++i)
    pcm[i] = (float)((int)i - 32768) / 32768.0f;
  status = po33_samples_encode_f32(pcm, 65536, 23438, encoded, sizeof(encoded), &count);
  assert(status == PO_OK && count == 65536);
  for (size_t i = 0; i < count; ++i) {
    uint8_t expected = po33_sample_encode((int16_t)((int)i - 32768));
    assert(encoded[i] == expected);
  }
}

static void test_tones(void) {
  static const uint32_t rates[] = {8000, 24000, 44100, 48000, 192000};
  static const double tones[] = {440.0, 1000.0, 3000.0};
  const double pi = 3.14159265358979323846;
  for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
    for (size_t t = 0; t < sizeof(tones) / sizeof(tones[0]); ++t) {
      size_t count = rates[r] / 4u, written, repeated;
      po_status_t status;
      double real = 0.0, imaginary = 0.0, power = 0.0;
      for (size_t i = 0; i < count; ++i)
        pcm[i] = (float)(0.35 * sin(2.0 * pi * tones[t] * i / rates[r]));
      status = po33_samples_encode_f32(pcm, count, rates[r], encoded, sizeof(encoded), &written);
      assert(status == PO_OK && written == 5860);
      status = po33_samples_encode_f32(pcm, count, rates[r], again, sizeof(again), &repeated);
      assert(status == PO_OK && repeated == written);
      assert(memcmp(encoded, again, written) == 0);
      for (size_t i = 128; i < written - 128; ++i) {
        double value = (double)po33_sample_decode(encoded[i]) / 32768.0;
        double angle = 2.0 * pi * tones[t] * i / 23437.5;
        real += value * cos(angle);
        imaginary += value * sin(angle);
        power += value * value;
      }
      size_t measured = written - 256;
      double rms = sqrt(power / measured);
      double fraction = 2.0 * (real * real + imaginary * imaginary) / (power * measured);
      assert(rms > 0.24 && rms < 0.255);
      assert(fraction > 0.99);
    }
}

static void test_alias_rejection(void) {
  size_t count;
  po_status_t status;
  double power = 0.0;
  for (size_t i = 0; i < 48000; ++i)
    pcm[i] = (float)(0.5 * sin(2.0 * 3.14159265358979323846 * 20000.0 * i / 48000.0));
  status = po33_samples_encode_f32(pcm, 48000, 48000, encoded, sizeof(encoded), &count);
  assert(status == PO_OK && count == 23438);
  for (size_t i = 128; i < count - 128; ++i) {
    double value = (double)po33_sample_decode(encoded[i]) / 32768.0;
    power += value * value;
  }
  double rms = sqrt(power / (count - 256));
  assert(rms < 0.005);
}

static void test_silence_and_full_scale(void) {
  static const uint32_t rates[] = {8000, 23438, 24000, 44100, 48000, 192000};
  size_t count;
  po_status_t status;
  memset(pcm, 0, sizeof(pcm));
  for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
    status = po33_samples_encode_f32(pcm, rates[r], rates[r], encoded, sizeof(encoded), &count);
    assert(status == PO_OK && count == 23438);
    for (size_t i = 0; i < count; ++i)
      assert(encoded[i] == 0);
  }
  /* Sharp full-scale transitions exercise saturation after filter ringing. */
  for (size_t i = 0; i < 8000; ++i)
    pcm[i] = i % 64 < 32 ? 1.0f : -1.0f;
  status = po33_samples_encode_f32(pcm, 8000, 8000, encoded, sizeof(encoded), &count);
  assert(status == PO_OK && count == 23438);
  for (size_t i = 128; i < count - 128; ++i) {
    double input_position = i * 8000.0 / 23437.5;
    size_t offset = (size_t)input_position % 64;
    if (offset >= 8 && offset < 24)
      assert(encoded[i] == 127);
    if (offset >= 40 && offset < 56)
      assert(encoded[i] == 128);
  }
}

int main(void) {
  test_count();
  test_contract();
  test_native_alias();
  test_tones();
  test_alias_rejection();
  test_silence_and_full_scale();
  puts("PO-33 PCM conversion tests passed");
  return 0;
}
