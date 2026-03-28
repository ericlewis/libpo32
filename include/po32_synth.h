#ifndef PO32_PO32_SYNTH_H
#define PO32_PO32_SYNTH_H

/*
 * PO-32 drum synthesizer.
 *
 * Renders drum sounds from po32_patch_params_t parameters.
 * All output is float [-1, 1]. Internally uses lookup tables for all
 * transcendentals (no libm trig required).
 */

#include "po32.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct po32_synth {
  uint32_t sample_rate;
} po32_synth_t;

void po32_synth_init(po32_synth_t *synth, uint32_t sample_rate);

size_t po32_synth_samples_for_duration(const po32_synth_t *synth, float seconds);

/*
 * Render a single drum hit.
 *
 * params:   patch parameters (all fields 0.0-1.0)
 * velocity: MIDI velocity 0-127
 * duration: render length in seconds
 * out:      caller-allocated float buffer, receives samples in [-1, 1]
 * out_capacity: max floats that fit in out
 * out_len:  set to actual number of samples written
 */
po32_status_t po32_synth_render(const po32_synth_t *synth, const po32_patch_params_t *params,
                                int velocity, float duration, float *out, size_t out_capacity,
                                size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
