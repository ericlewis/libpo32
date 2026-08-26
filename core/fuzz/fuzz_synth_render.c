/*
 * Fuzz harness for po32_synth_render().
 *
 * Patch parameters are taken from the raw 42-byte wire format (guaranteed
 * in [0, 1], matching the documented contract), while velocity is an
 * arbitrary 32-bit int and the duration and sample rate vary. The renderer
 * must always succeed and every output sample must be a real number in
 * [-1, 1] — a NaN, infinity, or out-of-range sample is a bug.
 */

#include "po32.h"
#include "po32_synth.h"
#include "fuzz_driver.h"

#include <string.h>

#define FUZZ_MAX_SECONDS  0.5f
#define FUZZ_MAX_SAMPLES  24000u /* FUZZ_MAX_SECONDS at the highest rate below */
#define FUZZ_HEADER_BYTES 7u

static const uint32_t fuzz_rates[] = {8000u, 22050u, 44100u, 48000u};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static float samples[FUZZ_MAX_SAMPLES];
  po32_synth_t synth;
  po32_patch_params_t params;
  uint32_t sample_rate;
  int32_t velocity;
  float duration;
  size_t out_len = 0u;

  if (size < FUZZ_HEADER_BYTES + PO32_PARAM_COUNT * 2u) {
    return 0;
  }

  sample_rate = fuzz_rates[data[0] % (sizeof(fuzz_rates) / sizeof(fuzz_rates[0]))];
  memcpy(&velocity, data + 1u, sizeof(velocity));
  duration = ((float)((uint16_t)data[5] | (uint16_t)((uint16_t)data[6] << 8)) / 65535.0f) *
             FUZZ_MAX_SECONDS;

  fuzz_check(po32_decode_patch(data + FUZZ_HEADER_BYTES, PO32_PARAM_COUNT * 2u, &params) ==
             PO32_OK);

  po32_synth_init(&synth, sample_rate);
  fuzz_check(po32_synth_samples_for_duration(&synth, duration) <= FUZZ_MAX_SAMPLES);
  fuzz_check(po32_synth_render(&synth, &params, (int)velocity, duration, samples, FUZZ_MAX_SAMPLES,
                               &out_len) == PO32_OK);
  fuzz_check(out_len <= FUZZ_MAX_SAMPLES);

  for (size_t i = 0u; i < out_len; ++i) {
    /* Also rejects NaN: NaN fails both comparisons. */
    fuzz_check(samples[i] >= -1.0f && samples[i] <= 1.0f);
  }

  return 0;
}
