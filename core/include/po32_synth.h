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

/* ── Streaming voice ────────────────────────────────────── */

/*
 * Streaming voice renderer.
 *
 * Pre-computes all per-render constants at init time and persists DSP
 * state (phase, filters, PRNG) across render_f32 calls. This allows
 * rendering a drum hit in fixed-size chunks suitable for real-time
 * audio callbacks and memory-constrained targets.
 */
typedef struct po32_synth_voice {
  uint32_t sample_rate;
  size_t total_samples;
  size_t samples_rendered;

  /* persistent DSP state */
  float phase;
  float mod_state;
  uint32_t rand_state;
  float noise_filt[7]; /* biquad: b0 b1 b2 a1 a2 z1 z2 */
  float eq_filt[7];

  /* precomputed constants */
  float sr;
  float osc_freq, osc_atk, osc_dcy;
  float mod_sm, mod_mode;
  float nf_freq, nf_q, n_atk, n_dcy;
  float dist_amt, level;
  float osc_gain_mix, noise_gain_mix;
  float osc_vel_g, noise_vel_g;
  float mod_decay_time, mod_rate_hz;
  float mod_alpha_precomp;
  float mod_env_period, mod_env_amp_correction;
  int use_eq;
  float osc_wave;
  float n_env_mod;
} po32_synth_voice_t;

void po32_synth_voice_init(po32_synth_voice_t *v, uint32_t sample_rate,
                           const po32_patch_params_t *params, int velocity, float duration);

void po32_synth_voice_reset(po32_synth_voice_t *v);

size_t po32_synth_voice_samples_remaining(const po32_synth_voice_t *v);

int po32_synth_voice_done(const po32_synth_voice_t *v);

po32_status_t po32_synth_voice_render_f32(po32_synth_voice_t *v, float *out, size_t out_capacity,
                                          size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
