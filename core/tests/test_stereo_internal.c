#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/po_stereo.c"
#include "../src/po_wire_internal.h"

static void nibbles_from_bytes(const uint8_t *src, size_t count, uint8_t *out, size_t parity) {
  if (parity)
    out[0] = 7;
  for (size_t i = 0; i < count; i++) {
    out[2 * i + parity] = src[i] & 15;
    out[2 * i + parity + 1] = src[i] >> 4;
  }
}
static void test_counter_search(void) {
  uint8_t wire[8192], nibbles[16385], out[8192];
  size_t written;
  po_status_t status;
  po_wire_counter_write(wire);
  po_wire_counter_write(wire + 4096);
  for (size_t parity = 0; parity < 2; parity++) {
    nibbles_from_bytes(wire, 4096, nibbles, parity);
    memset(out, 0xa5, sizeof(out));
    written = 19;
    status = find_frame(nibbles, 8192 + parity, out, 4095, &written);
    assert(status == PO_ERR_BUFFER_TOO_SMALL && written == 4096 && out[0] == 0xa5);
    status = find_frame(nibbles, 8192 + parity, out, sizeof(out), &written);
    assert(status == PO_OK && written == 4096 && !memcmp(wire, out, 4096));
  }
  for (size_t i = 4092; i < 4096; i++) {
    wire[i] ^= 1;
    nibbles_from_bytes(wire, 4096, nibbles, 0);
    written = 0;
    status = find_frame(nibbles, 8192, out, sizeof(out), &written);
    assert(status == PO_ERR_FRAME);
    wire[i] ^= 1;
  }
  wire[0] ^= 1;
  nibbles_from_bytes(wire, sizeof(wire), nibbles, 1);
  status = find_frame(nibbles, sizeof(nibbles), out, sizeof(out), &written);
  assert(status == PO_OK && written == 4096 && !memcmp(wire + 4096, out, 4096));
  status = find_frame(nibbles, 8191, out, sizeof(out), &written);
  assert(status == PO_ERR_FRAME);
}
static void test_workspace_guards(void) {
  double scalars[4] = {0};
  uint8_t scratch[1], out[1];
  float samples[2] = {0};
  po_stereo_workspace_t work = {scalars, 4, scratch, 1};
  po_stereo_modulator_t mod = {0};
  size_t a = 99, b = 99, c = 99, n = 99;
  po_status_t s;
  s = po_stereo_decode_requirements(1, 48000, &a, NULL, &c);
  assert(s == PO_ERR_INVALID_ARG && a == 0 && c == 0);
  s = po_stereo_decode_requirements(1, 48000, &a, &b, NULL);
  assert(s == PO_ERR_INVALID_ARG && a == 0 && b == 0);
#if SIZE_MAX > UINT32_MAX
  s = po_stereo_decode_requirements((size_t)UINT64_C(9007199254740992), 48000, &a, &b, &c);
  assert(s == PO_ERR_RANGE && a == 0 && b == 0 && c == 0);
#endif
  assert(po_stereo_modulator_remaining(&mod) == 0);
  s = po_stereo_modulator_render_f32(NULL, samples, 1, &n);
  assert(s == PO_ERR_INVALID_ARG && n == 0);
  s = po_stereo_modulator_render_f32(&mod, samples, 1, &n);
  assert(s == PO_ERR_INVALID_ARG && n == 0);
  work.scalars = NULL;
  s = po_stereo_decode_f32(samples, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &work, out, 1, &n);
  assert(s == PO_ERR_INVALID_ARG && n == 0);
  work.scalars = scalars;
  work.bytes = NULL;
  s = po_stereo_decode_f32(samples, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &work, out, 1, &n);
  assert(s == PO_ERR_INVALID_ARG && n == 0);
  work.bytes = scratch;
  samples[1] = NAN;
  s = po_stereo_decode_f32(samples, 1, 48000, PO_STEREO_CHANNEL_NORMAL, &work, out, 1, &n);
  assert(s == PO_ERR_RANGE && n == 0);
}
static void test_clock_drift(void) {
  uint8_t wire[4352];
  po_wire_counter_write(wire);
  for (size_t i = 4096; i < sizeof(wire); ++i)
    wire[i] = (uint8_t)(i * 71u);
  po_stereo_modulator_t tx;
  po_status_t status = po_stereo_modulator_init(&tx, wire, sizeof(wire), 48000);
  assert(status == PO_OK);
  size_t original_count = po_stereo_modulator_remaining(&tx), written;
  float *original = malloc(original_count * 2 * sizeof(float));
  assert(original);
  status = po_stereo_modulator_render_f32(&tx, original, original_count, &written);
  assert(status == PO_OK && written == original_count);
  for (unsigned direction = 0; direction < 2; ++direction) {
    double speed = direction ? 1.0002 : 0.9998;
    size_t frames = (size_t)((original_count - 1) / speed), capacity;
    po_stereo_workspace_t work;
    status = po_stereo_decode_requirements(frames, 48000, &work.scalar_count, &work.byte_count,
                                           &capacity);
    assert(status == PO_OK);
    float *shifted = malloc(frames * 2 * sizeof(float));
    work.scalars = malloc(work.scalar_count * sizeof(double));
    work.bytes = malloc(work.byte_count);
    uint8_t *result = malloc(capacity);
    assert(shifted && work.scalars && work.bytes && result);
    for (size_t i = 0; i < frames; ++i) {
      double position = i * speed;
      size_t base = (size_t)position;
      double fraction = position - base;
      for (unsigned channel = 0; channel < 2; ++channel)
        shifted[2 * i + channel] = (float)((1 - fraction) * original[2 * base + channel] +
                                           fraction * original[2 * (base + 1) + channel]);
    }
    status = po_stereo_decode_f32(shifted, frames, 48000, PO_STEREO_CHANNEL_NORMAL, &work, result,
                                  capacity, &written);
    assert(status == PO_OK && written >= sizeof(wire));
    assert(memcmp(result, wire, sizeof(wire)) == 0);
    free(result);
    free(work.bytes);
    free(work.scalars);
    free(shifted);
  }
  free(original);
}

int main(void) {
  test_counter_search();
  test_workspace_guards();
  test_clock_drift();
  puts("Stereo internal tests passed");
  return 0;
}
