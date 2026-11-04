#ifndef PO35_PO35_SYNTH_H
#define PO35_PO35_SYNTH_H

#include "po_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental speech reconstruction, not the original PO-35 decoder.
 * Model 1: Bitspeek-informed BLIT, LAR step-up, residual-energy gain, and
 * xorshift32/Marsaglia Gaussian noise. The deterministic noise stream is
 * part of this reconstruction model, not a claim of device parity. */
#define PO35_SYNTH_MODEL_VERSION 1
#define PO35_SYNTH_RECORD_BYTES  44
#define PO35_SYNTH_MAX_ORDER     30
#define PO35_SYNTH_DEFAULT_SEED  350u

typedef enum po35_synth_bank {
  PO35_SYNTH_BANK_LOW = 0, /* 10 coefficients, native 8000 Hz */
  PO35_SYNTH_BANK_HIGH = 1 /* 30 coefficients, native 24000 Hz */
} po35_synth_bank_t;

typedef struct po35_synth {
  po35_synth_bank_t bank;
  uint32_t seed;
} po35_synth_t;

/* Concrete caller-owned storage; DSP fields are private implementation state.
 * Keep records immutable and alive whenever the voice may render them,
 * including subsequent renders after reset. Reset does not copy the records.
 * No allocation, sample-rate conversion, clipping or normalization occurs.
 * This implementation requires IEEE-754 binary64 double and binary32 float. */
typedef struct po35_synth_voice {
  const uint8_t *records;
  size_t record_count;
  size_t total_samples;
  size_t samples_rendered;
  size_t record_index;
  size_t record_sample;
  size_t record_samples;
  uint32_t sample_rate;
  uint32_t fps;
  uint32_t seed;
  uint32_t rand_state;
  unsigned order;
  int frame_ready;
  int noise_cached;
  po_status_t status;
  double phase;
  double increment;
  double harmonics;
  double periodic_scale;
  double noise_scale;
  double residual_gain;
  double noise_cache;
  double lpc[PO35_SYNTH_MAX_ORDER + 1];
  double history[PO35_SYNTH_MAX_ORDER];
  double delay[PO35_SYNTH_MAX_ORDER];
} po35_synth_voice_t;

po_status_t po35_synth_init(po35_synth_t *synth, po35_synth_bank_t bank, uint32_t seed);

/* fps must be 20, 40, 60 or 80. Boundaries use cumulative nearest rounding:
 * low-bank 60 fps gives 133, 134, 133 samples in each three-record cycle.
 * Returns RANGE for overflow; *out_samples is zero on error. */
po_status_t po35_synth_samples_for_records(const po35_synth_t *synth, size_t record_count,
                                           uint32_t fps, size_t *out_samples);

/* record_bytes must be a multiple of 44. Empty records are valid and inert.
 * Invalid initialization also leaves an inert voice. A zero seed maps to a
 * fixed nonzero PRNG state; resetting a voice repeats the same noise stream. */
po_status_t po35_synth_voice_init(po35_synth_voice_t *voice, const po35_synth_t *synth,
                                  const uint8_t *records, size_t record_bytes, uint32_t fps);
void po35_synth_voice_reset(po35_synth_voice_t *voice);
size_t po35_synth_voice_samples_remaining(const po35_synth_voice_t *voice);
int po35_synth_voice_done(const po35_synth_voice_t *voice);

/* Writes finite, unnormalized float samples. Chunking and reset are exact
 * within one build. Zero capacity is a no-op; out may then be NULL.
 * RANGE indicates numerical overflow or instability. It latches until reset;
 * *out_len reports only committed finite samples and the remainder is untouched.
 * A numerically failed voice is not done while samples remain. */
po_status_t po35_synth_voice_render_f32(po35_synth_voice_t *voice, float *out, size_t out_capacity,
                                        size_t *out_len);

/* One-shot wrapper. Insufficient capacity writes nothing and returns
 * BUFFER_TOO_SMALL. Numerical failure can commit a finite prefix as above. */
po_status_t po35_synth_render(const po35_synth_t *synth, const uint8_t *records,
                              size_t record_bytes, uint32_t fps, float *out, size_t out_capacity,
                              size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif
