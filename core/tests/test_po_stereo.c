#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po_stereo.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TEST_PI 3.14159265358979323846

/* Deliberately no model grammar: transport must preserve arbitrary bytes. */
static void wire_fixture(uint8_t *wire, size_t size) {
  size_t i;
  for (i = 0; i < 4092; ++i)
    wire[i] = (uint8_t)i;
  wire[4092] = 0x4d;
  wire[4093] = 0xb8;
  wire[4094] = 0x16;
  wire[4095] = 0x5d;
  for (i = 4096; i < size; ++i)
    wire[i] = (uint8_t)(i * 37u + (i >> 5));
}

static po_stereo_workspace_t workspace_create(size_t frames, uint32_t rate) {
  po_stereo_workspace_t w;
  size_t wire_capacity;
  assert(po_stereo_decode_requirements(frames, rate, &w.scalar_count, &w.byte_count,
                                       &wire_capacity) == PO32_OK);
  w.scalars = (double *)malloc(w.scalar_count * sizeof(double));
  w.bytes = (uint8_t *)malloc(w.byte_count);
  assert(w.scalars && w.bytes && wire_capacity <= frames / 2);
  return w;
}

static void workspace_free(po_stereo_workspace_t *w) {
  free(w->scalars);
  free(w->bytes);
}

/* Detect phase/timing changes against analytic quadrature, plus chunk/reset. */
static void test_render(void) {
  const uint8_t wire[] = {0xe4, 0x1b, 0xa5, 0x09};
  const uint32_t rates[] = {44100, 48000, 96000};
  size_t r;
  for (r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
    po_stereo_modulator_t state;
    size_t frames, written, offset, i;
    float *whole, *split;
    uint32_t rate = rates[r];
    assert(po_stereo_modulator_init(&state, wire, sizeof(wire), rate) == PO32_OK);
    frames = po_stereo_modulator_remaining(&state);
    assert(frames == (size_t)(rate / 2u + ((137u * (uint64_t)rate * 2u + 15624u) / 15625u)));
    whole = (float *)malloc(2 * frames * sizeof(float));
    split = (float *)malloc(2 * frames * sizeof(float));
    assert(whole && split && !po_stereo_modulator_done(&state));
    assert(po_stereo_modulator_render_f32(&state, NULL, 0, &written) == PO32_OK && !written);
    assert(po_stereo_modulator_render_f32(&state, whole, frames, &written) == PO32_OK);
    assert(written == frames && po_stereo_modulator_done(&state));
    assert(po_stereo_modulator_remaining(&state) == 0);
    assert(po_stereo_modulator_render_f32(&state, split, 1, &written) == PO32_OK && !written);
    assert(po_stereo_modulator_reset(&state) == PO32_OK);
    for (offset = 0; offset < frames; offset += written) {
      size_t chunk = (offset * 13u + 7u) % 257u + 1u;
      if (chunk > frames - offset)
        chunk = frames - offset;
      assert(po_stereo_modulator_render_f32(&state, split + 2 * offset, chunk, &written) ==
             PO32_OK);
      assert(written == chunk);
    }
    assert(!memcmp(whole, split, frames * 2 * sizeof(float)));
    for (i = 0; i < frames; ++i) {
      size_t ch;
      for (ch = 0; ch < 2; ++ch)
        assert(fabs(whole[2 * i + ch]) <= 0.600001);
      if (i < rate / 4u || i >= frames - rate / 4u) {
        assert(whole[2 * i] == 0 && whole[2 * i + 1] == 0);
      } else {
        uint64_t ticks = (uint64_t)(i - rate / 4u) * 15625u;
        uint64_t denominator = (uint64_t)rate * 2;
        size_t symbol = (size_t)(ticks / denominator), s;
        double fraction = (double)(ticks % denominator) / (double)denominator;
        double mix = fraction < 0.6 ? 0 : fraction > 0.9 ? 1 : (fraction - 0.6) / 0.3;
        for (ch = 0; ch < 2; ++ch) {
          unsigned phase = 0, next = 0;
          double expected;
          for (s = 0; s <= symbol && s < 2 * sizeof(wire); ++s) {
            unsigned nibble = (wire[s / 2] >> ((s & 1u) * 4u)) & 15u;
            if (s < symbol)
              phase += ch ? nibble & 3u : nibble >> 2;
            else
              next = ch ? nibble & 3u : nibble >> 2;
          }
          expected =
              0.6 * ((1 - mix) * sin(2 * TEST_PI * fraction + (phase & 3u) * TEST_PI / 2) +
                     mix * sin(2 * TEST_PI * fraction + ((phase + next) & 3u) * TEST_PI / 2));
          assert(fabs(whole[2 * i + ch] - expected) < 8e-8);
        }
      }
    }
    free(whole);
    free(split);
  }
}

static void test_roundtrip(void) {
  uint8_t wire[4608];
  const uint32_t rates[] = {44100, 48000, 96000};
  size_t r;
  wire_fixture(wire, sizeof(wire));
  for (r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
    po_stereo_modulator_t state;
    po_stereo_workspace_t w;
    size_t frames, written, length, i;
    float *audio, *original;
    uint8_t *decoded;
    assert(po_stereo_modulator_init(&state, wire, sizeof(wire), rates[r]) == PO32_OK);
    frames = po_stereo_modulator_remaining(&state);
    audio = (float *)malloc(frames * 2 * sizeof(float));
    original = (float *)malloc(frames * 2 * sizeof(float));
    decoded = (uint8_t *)malloc(frames / 2);
    assert(audio && original && decoded);
    w = workspace_create(frames, rates[r]);
    assert(po_stereo_modulator_render_f32(&state, audio, frames, &written) == PO32_OK);
    memcpy(original, audio, frames * 2 * sizeof(float));
    assert(po_stereo_decode_f32(audio, frames, rates[r], PO_STEREO_CHANNEL_NORMAL, &w, decoded,
                                frames / 2, &length) == PO32_OK);
    assert(length >= sizeof(wire) && !memcmp(decoded, wire, sizeof(wire)));
    assert(!memcmp(original, audio, frames * 2 * sizeof(float)));
    memset(decoded, 0xa6, frames / 2);
    assert(po_stereo_decode_f32(audio, frames, rates[r], PO_STEREO_CHANNEL_NORMAL, &w, decoded,
                                length - 1, &written) == PO32_ERR_BUFFER_TOO_SMALL);
    assert(written == length);
    assert(po_stereo_decode_f32(audio, frames, rates[r], PO_STEREO_CHANNEL_NORMAL, &w, NULL, 0,
                                &written) == PO32_ERR_BUFFER_TOO_SMALL);
    assert(written == length);
    for (i = 0; i < frames / 2; ++i)
      assert(decoded[i] == 0xa6);
    assert(po_stereo_decode_f32(audio, frames, rates[r], PO_STEREO_CHANNEL_NORMAL, &w, decoded,
                                length, &written) == PO32_OK &&
           written == length);
    for (i = 0; i < frames; ++i) {
      float temporary = audio[2 * i];
      audio[2 * i] = audio[2 * i + 1];
      audio[2 * i + 1] = temporary;
    }
    assert(po_stereo_decode_f32(audio, frames, rates[r], PO_STEREO_CHANNEL_SWAPPED, &w, decoded,
                                frames / 2, &length) == PO32_OK);
    assert(length >= sizeof(wire) && !memcmp(decoded, wire, sizeof(wire)));
    workspace_free(&w);
    free(audio);
    free(original);
    free(decoded);
  }
}

static void test_errors(void) {
  po_stereo_modulator_t state;
  uint8_t wire = 0, output[16];
  size_t a = 99, b = 99, c = 99, length = 99;
  float sample[2] = {0, 0};
  float *silence = (float *)calloc(48000u, 2 * sizeof(float));
  po_stereo_workspace_t w = workspace_create(48000, 48000);
  assert(silence);
  memset(output, 0xa7, sizeof(output));
  assert(po_stereo_modulator_init(NULL, &wire, 1, 48000) == PO32_ERR_INVALID_ARG);
  assert(!po_stereo_modulator_done(NULL) && !po_stereo_modulator_remaining(NULL));
  assert(po_stereo_modulator_reset(NULL) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_modulator_init(&state, &wire, SIZE_MAX, 48000) == PO32_ERR_RANGE);
  assert(!po_stereo_modulator_done(&state));
  assert(po_stereo_modulator_reset(&state) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_modulator_init(&state, NULL, 1, 48000) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_modulator_init(&state, &wire, 0, 48000) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_modulator_init(&state, &wire, 1, 48001) == PO32_ERR_RANGE);
  assert(po_stereo_modulator_init(&state, &wire, 1, 48000) == PO32_OK);
  assert(po_stereo_modulator_render_f32(&state, sample, SIZE_MAX, &length) == PO32_ERR_RANGE &&
         !length);
  assert(po_stereo_modulator_render_f32(&state, NULL, 1, &length) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_modulator_render_f32(&state, sample, 1, NULL) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_decode_requirements(1, 48000, NULL, &b, &c) == PO32_ERR_INVALID_ARG);
  assert(po_stereo_decode_requirements(SIZE_MAX, 48000, &a, &b, &c) == PO32_ERR_RANGE);
  assert(a == 0 && b == 0 && c == 0);
  assert(po_stereo_decode_requirements(1, 31999, &a, &b, &c) == PO32_ERR_RANGE);
  assert(po_stereo_decode_requirements(1, 192001, &a, &b, &c) == PO32_ERR_RANGE);
  assert(po_stereo_decode_requirements(1, 32000, &a, &b, &c) == PO32_OK);
  assert(a == 4 && b == 1 && !c);
  assert(po_stereo_decode_requirements(1, 192000, &a, &b, &c) == PO32_OK);
  assert(po_stereo_decode_requirements(0, 48000, &a, &b, &c) == PO32_OK && !a && !b && !c);
  assert(po_stereo_decode_f32(NULL, 0, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output, sizeof(output),
                              &length) == PO32_ERR_FRAME &&
         !length);
  assert(po_stereo_decode_f32(sample, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_FRAME &&
         !length);
  assert(po_stereo_decode_f32(sample, 1, 48000, (po_stereo_channel_order_t)2, &w, output,
                              sizeof(output), &length) == PO32_ERR_INVALID_ARG &&
         !length);
  assert(po_stereo_decode_f32(NULL, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output, sizeof(output),
                              &length) == PO32_ERR_INVALID_ARG &&
         !length);
  assert(po_stereo_decode_f32(sample, SIZE_MAX, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_RANGE &&
         !length);
  assert(po_stereo_decode_f32(sample, 1, 48000, PO_STEREO_CHANNEL_NORMAL, NULL, output,
                              sizeof(output), &length) == PO32_ERR_INVALID_ARG &&
         !length);
  assert(po_stereo_decode_f32(sample, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &w, NULL, 1, &length) ==
             PO32_ERR_INVALID_ARG &&
         !length);
  assert(po_stereo_decode_f32(sample, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), NULL) == PO32_ERR_INVALID_ARG);
  --w.scalar_count;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_BUFFER_TOO_SMALL &&
         !length);
  ++w.scalar_count;
  --w.byte_count;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_BUFFER_TOO_SMALL &&
         !length);
  ++w.byte_count;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_FRAME &&
         !length);
  silence[17000] = NAN;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_RANGE &&
         !length);
  silence[17000] = INFINITY;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_RANGE &&
         !length);
  silence[17000] = -INFINITY;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_RANGE &&
         !length);
  for (a = 0; a < 96000; ++a)
    silence[a] = a & 1u ? FLT_MAX : -FLT_MAX;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_FRAME &&
         !length);
  for (a = 0; a < 96000; ++a)
    silence[a] = (float)((int)((a * 1103515245u + 12345u) >> 8) % 65536) / 65536.0f;
  assert(po_stereo_decode_f32(silence, 48000, 48000, PO_STEREO_CHANNEL_NORMAL, &w, output,
                              sizeof(output), &length) == PO32_ERR_FRAME &&
         !length);
  for (a = 0; a < sizeof(output); ++a)
    assert(output[a] == 0xa7);
  workspace_free(&w);
  free(silence);
}

int main(void) {
  test_render();
  test_roundtrip();
  test_errors();
  puts("stereo transport tests passed");
  return 0;
}
