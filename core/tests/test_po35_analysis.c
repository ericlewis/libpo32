#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po35_analysis.h"
#include "po35_synth.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static po35_analysis_workspace_t workspace;
static float pcm[192000], rendered[48000];
static unsigned char records[80 * 44];
static double realfft[8192], imagfft[8192];
static double peak_hz(const float *audio, size_t count, double rate, double target,
                      double *fraction) {
  size_t n = 8192;
  if (count < n)
    n = 4096;
  for (size_t i = 0; i < n; i++) {
    realfft[i] = audio[count - n + i] * (.5 - .5 * cos(2 * 3.141592653589793 * i / (n - 1)));
    imagfft[i] = 0;
  }
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = realfft[i];
      realfft[i] = realfft[j];
      realfft[j] = t;
    }
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    double a = -2 * 3.141592653589793 / len, wr = cos(a), wi = sin(a);
    for (size_t i = 0; i < n; i += len) {
      double zr = 1, zi = 0;
      for (size_t j = 0; j < len / 2; j++) {
        size_t x = i + j, y = x + len / 2;
        double vr = realfft[y] * zr - imagfft[y] * zi, vi = realfft[y] * zi + imagfft[y] * zr;
        realfft[y] = realfft[x] - vr;
        imagfft[y] = imagfft[x] - vi;
        realfft[x] += vr;
        imagfft[x] += vi;
        double t = zr * wr - zi * wi;
        zi = zr * wi + zi * wr;
        zr = t;
      }
    }
  }
  size_t best = 1;
  double energy = 0, band = 0;
  for (size_t i = 1; i < n / 2; i++) {
    double e = realfft[i] * realfft[i] + imagfft[i] * imagfft[i];
    realfft[i] = e;
    energy += e;
    if (fabs((double)i * rate / n - target) < 50)
      band += e;
    if (e > realfft[best])
      best = i;
  }
  *fraction = energy ? band / energy : 0;
  return (double)best * rate / n;
}
static void test_contract(void) {
  size_t n = 99;
  assert(po35_analysis_size_for_pcm(24000, 24000, 40, &n) == PO_OK && n == 40 * 44);
  assert(po35_analysis_size_for_pcm(24001, 24000, 40, &n) == PO_OK && n == 41 * 44);
  assert(po35_analysis_size_for_pcm(1, 24000, 60, &n) == PO_OK && n == 44);
  assert(po35_analysis_size_for_pcm((size_t)-1, 24000, 40, &n) == PO_ERR_RANGE && n == 0);
  assert(po35_analysis_size_for_pcm(44100, 44100, 40, &n) == PO_OK && n == 40 * 44);
  assert(po35_analysis_size_for_pcm(1, 7999, 40, &n) == PO_ERR_RANGE && n == 0);
  assert(po35_analysis_size_for_pcm(1, 192001, 40, &n) == PO_ERR_RANGE && n == 0);
  assert(po35_analysis_size_for_pcm((size_t)-1, 192000, 20, &n) == PO_ERR_RANGE && n == 0);
  assert(po35_analysis_size_for_pcm(24000, 24000, 30, &n) == PO_ERR_RANGE && n == 0);
  assert(po35_analysis_size_for_pcm((size_t)65535 * 300, 24000, 80, &n) == PO_OK &&
         n == (size_t)65535 * 44);
  assert(po35_analysis_size_for_pcm((size_t)65535 * 300 + 1, 24000, 80, &n) == PO_ERR_RANGE &&
         n == 0);
  assert(po35_analysis_size_for_pcm(1, 24000, 40, NULL) == PO_ERR_INVALID_ARG);
  assert(po35_analysis_encode_f32(&workspace, NULL, 0, 24000, 40, NULL, 0, &n) == PO_OK && n == 0);
  memset(records, 0x55, sizeof(records));
  assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, 40, records, 100, &n) ==
             PO_ERR_BUFFER_TOO_SMALL &&
         n == 0 && records[0] == 0x55);
  pcm[100] = NAN;
  assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, 40, records, sizeof(records),
                                  &n) == PO_ERR_RANGE &&
         n == 0 && records[0] == 0x55);
  pcm[100] = 0;
  pcm[23999] = INFINITY;
  assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, 40, records, sizeof(records),
                                  &n) == PO_ERR_RANGE &&
         n == 0 && records[0] == 0x55);
  pcm[23999] = 1.01f;
  assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, 40, records, sizeof(records),
                                  &n) == PO_ERR_RANGE &&
         n == 0 && records[0] == 0x55);
  pcm[23999] = 0;
  pcm[47999] = -INFINITY;
  assert(po35_analysis_encode_f32(&workspace, pcm, 48000, 48000, 40, records, sizeof(records),
                                  &n) == PO_ERR_RANGE &&
         n == 0);
  for (size_t i = 0; i < sizeof(records); i++)
    assert(records[i] == 0x55);
  pcm[47999] = 0;
  assert(po35_analysis_encode_f32(NULL, pcm, 1, 24000, 40, records, sizeof(records), &n) ==
         PO_ERR_INVALID_ARG);
  assert(po35_analysis_encode_f32(&workspace, NULL, 1, 24000, 40, records, sizeof(records), &n) ==
         PO_ERR_INVALID_ARG);
  assert(po35_analysis_encode_f32(&workspace, pcm, 1, 24000, 40, NULL, 100, &n) ==
         PO_ERR_INVALID_ARG);
  static const size_t lengths[] = {1, 299, 300, 301, 399, 400, 401};
  for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
    unsigned char again[88];
    size_t m;
    assert(po35_analysis_encode_f32(&workspace, pcm, lengths[i], 24000, 80, records,
                                    sizeof(records), &n) == PO_OK);
    assert(po35_analysis_encode_f32(&workspace, pcm, lengths[i], 24000, 80, again, sizeof(again),
                                    &m) == PO_OK &&
           m == n && memcmp(records, again, n) == 0);
  }
  static const unsigned rates[] = {8000, 44100, 48000, 192000};
  for (unsigned r = 0; r < 4; r++)
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
      size_t required;
      assert(po35_analysis_size_for_pcm(lengths[i], rates[r], 80, &required) == PO_OK);
      assert(po35_analysis_encode_f32(&workspace, pcm, lengths[i], rates[r], 80, records,
                                      sizeof(records), &n) == PO_OK &&
             n == required);
      for (size_t f = 0; f < n / 44; f++)
        assert(records[f * 44 + 2] == 0);
    }
}
static void test_signal(const char *label, double freq, int noise) {
  size_t i, n, total;
  unsigned state = 7;
  double p = 0, g = 0, u = 0, energy = 0, peak = 0;
  po35_synth_t synth;
  for (i = 0; i < 24000; i++) {
    state = state * 1664525u + 1013904223u;
    pcm[i] = noise ? 0.2 * ((double)state / 2147483648.0 - 1.0)
                   : 0.2 * sin(2 * 3.141592653589793 * freq * i / 24000);
  }
  assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, 40, records, sizeof(records),
                                  &n) == PO_OK &&
         n == 1760);
  for (i = 2; i < 38; i++) {
    unsigned raw = records[44 * i] | (records[44 * i + 1] << 8);
    int q = raw < 32768 ? (int)raw : (int)raw - 65536;
    p += 261.6255653005986 * exp2(q / 2048.0);
    g += records[44 * i + 2];
    u += records[44 * i + 3];
  }
  p /= 36;
  g /= 36;
  u /= 36;
  if (freq > 0) {
    double expected = freq == 1000 ? 500 : freq == 3000 ? 750 : freq;
    assert(fabs(p - expected) / expected < 0.015);
  }
  if (freq == 0 && !noise)
    assert(g == 0);
  if (noise)
    assert(u > 240);
  else if (freq > 0)
    assert(u < 10);
  for (int bank = 0; bank < 2; bank++) {
    assert(po35_synth_init(&synth, (po35_synth_bank_t)bank, 350) == PO_OK);
    assert(po35_synth_render(&synth, records, n, 40, rendered, 48000, &total) == PO_OK);
    energy = peak = 0;
    for (i = 0; i < total; i++) {
      assert(isfinite(rendered[i]));
      energy += (double)rendered[i] * rendered[i];
      if (fabs(rendered[i]) > peak)
        peak = fabs(rendered[i]);
    }
    printf("%s bank=%d frames=%zu pitch=%.5f gain=%.3f unvoiced=%.3f rms=%.6f peak=%.6f\n", label,
           bank, n / 44, p, g, u, sqrt(energy / total), peak);
    if (freq > 0) {
      double fraction = 0,
             measured = peak_hz(rendered, total, bank ? 24000 : 8000, freq, &fraction);
      printf("  measured_peak_hz=%.6f target_band_energy=%.8f\n", measured, fraction);
      assert(fabs(measured - freq) / freq < .01);
      assert(fraction > .8);
      assert(sqrt(energy / total) < .2);
    }
  }
}
static void test_stress(void) {
  static const double frequencies[] = {30, 60, 110, 220, 440, 1000, 3000, 3900, 6000, 10000, 11900};
  static const unsigned rates[] = {20, 40, 60, 80};
  for (unsigned r = 0; r < 4; r++)
    for (unsigned signal = 0; signal < 15; signal++) {
      unsigned random = 3;
      size_t n = 0, total = 0;
      po35_synth_t synth;
      for (size_t i = 0; i < 24000; i++) {
        random = random * 1664525u + 1013904223u;
        pcm[i] = signal < 11    ? .5 * sin(2 * 3.141592653589793 * frequencies[signal] * i / 24000)
                 : signal == 11 ? .25
                 : signal == 12 ? (i % 2 ? .5 : -.5)
                 : signal == 13 ? (i % 127 ? 0 : 1)
                                : .8 * ((double)random / 2147483648.0 - 1);
      }
      assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, rates[r], records,
                                      sizeof(records), &n) == PO_OK);
      for (int bank = 0; bank < 2; bank++) {
        assert(po35_synth_init(&synth, (po35_synth_bank_t)bank, 350) == PO_OK);
        po_status_t status =
            po35_synth_render(&synth, records, n, rates[r], rendered, 48000, &total);
        if (status != PO_OK)
          fprintf(stderr, "stress failure signal=%u fps=%u bank=%d samples=%zu\n", signal, rates[r],
                  bank, total);
        assert(status == PO_OK);
        for (size_t i = 0; i < total; i++)
          assert(isfinite(rendered[i]));
      }
    }
  puts("60 signal/rate combinations, both banks finite");
}
static void test_native_golden(void) {
  /* Independent prototype result for deterministic, exactly representable PCM. */
  static const uint8_t expected[44] = {
      40, 252, 51, 16, 199, 16, 246, 11, 249, 8, 252, 6, 254, 5, 181, 0, 0, 0, 0, 0, 0, 0,
      0,  0,   1,  1,  1,   1,  1,   1,  1,   1, 1,   1, 1,   1, 1,   1, 1, 1, 1, 1, 1, 1};
  size_t bytes;
  for (unsigned i = 0; i < 24000; i++)
    pcm[i] = (float)((int)(i % 128) - 64) / 256.0f;
  assert(po35_analysis_encode_f32(&workspace, pcm, 24000, 24000, 40, records, sizeof(records),
                                  &bytes) == PO_OK);
  assert(bytes == 1760 && memcmp(records + 20 * 44, expected, 44) == 0);
}
static void test_resampling(void) {
  static const uint32_t rates[] = {8000, 24000, 44100, 48000, 192000};
  static const double tones[] = {440, 1000, 3000}, expected[] = {440, 500, 750};
  for (unsigned r = 0; r < 5; r++)
    for (unsigned t = 0; t < 3; t++) {
      size_t bytes, total;
      po35_synth_t synth;
      for (unsigned i = 0; i < rates[r]; i++)
        pcm[i] = (float)(.2 * sin(2 * 3.141592653589793 * tones[t] * i / rates[r]));
      assert(po35_analysis_encode_f32(&workspace, pcm, rates[r], rates[r], 40, records,
                                      sizeof(records), &bytes) == PO_OK &&
             bytes == 1760);
      for (unsigned f = 3; f < 37; f++) {
        unsigned raw = records[f * 44] | records[f * 44 + 1] << 8;
        int q = raw < 32768 ? (int)raw : (int)raw - 65536;
        double hz = 261.6255653005986 * exp2(q / 2048.0);
        assert(fabs(hz - expected[t]) / expected[t] < .015);
        assert(records[f * 44 + 3] < 10);
      }
      assert(po35_synth_init(&synth, PO35_SYNTH_BANK_HIGH, 350) == PO_OK);
      assert(po35_synth_render(&synth, records, bytes, 40, rendered, 48000, &total) == PO_OK);
      double fraction, hz = peak_hz(rendered, total, 24000, tones[t], &fraction);
      assert(fabs(hz - tones[t]) / tones[t] < .01 && fraction > .98);
    }
  /* Reject content above the 24k analysis Nyquist, rather than encoding its alias. */
  for (unsigned i = 0; i < 48000; i++)
    pcm[i] = (float)(.5 * sin(2 * 3.141592653589793 * 20000 * i / 48000));
  size_t bytes;
  assert(po35_analysis_encode_f32(&workspace, pcm, 48000, 48000, 40, records, sizeof(records),
                                  &bytes) == PO_OK);
  for (unsigned f = 3; f < 37; f++)
    assert(records[f * 44 + 2] <= 3);
  puts("resampled tone spectra and downsampling alias rejection passed");
}
static void test_syllable_resampling(void) {
  static const unsigned rates[] = {24000, 44100, 48000};
  uint8_t reference[40 * 44];
  for (unsigned r = 0; r < 3; r++) {
    size_t bytes;
    for (unsigned i = 0; i < rates[r]; i++) {
      double t = (double)i / rates[r],
             phase = 2 * 3.141592653589793 * 160 * t + 2 * sin(4 * 3.141592653589793 * t);
      double envelope = sin(3 * 3.141592653589793 * t);
      envelope *= envelope;
      pcm[i] = (float)(.12 * envelope *
                       (sin(phase) + .7 * sin(2 * phase) + .45 * sin(5 * phase) +
                        .25 * sin(10 * phase)));
    }
    assert(po35_analysis_encode_f32(&workspace, pcm, rates[r], rates[r], 40, records,
                                    sizeof(records), &bytes) == PO_OK &&
           bytes == sizeof(reference));
    if (!r)
      memcpy(reference, records, bytes);
    else
      for (unsigned f = 2; f < 38; f++) {
        for (unsigned j = 0; j < 4; j++) {
          int delta = (int)records[f * 44 + j] - reference[f * 44 + j];
          if (j < 2)
            assert(delta >= -1 && delta <= 1);
          else
            assert(delta >= -3 && delta <= 3);
        }
      }
  }
}
int main(void) {
  test_contract();
  test_native_golden();
  test_signal("silence", 0, 0);
  test_signal("440hz", 440, 0);
  test_signal("1000hz", 1000, 0);
  test_signal("3000hz", 3000, 0);
  test_signal("noise", 0, 1);
  test_stress();
  test_resampling();
  test_syllable_resampling();
  puts("analysis tests passed");
  return 0;
}
