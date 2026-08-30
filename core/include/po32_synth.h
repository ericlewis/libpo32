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
 * duration: render length in seconds; a non-positive or non-finite value,
 *           or a zero sample rate, renders nothing and sets *out_len to 0
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
  float phase01;
  float mod_state;
  uint32_t rand_state;
  float noise_filt[7]; /* biquad: b0 b1 b2 a1 a2 z1 z2 */
  float eq_filt[7];

  /* envelope and LFO recursion state; carried across render calls so a
     chunked render emits exactly the samples one big render would */
  float osc_env_v;
  int osc_env_synced;
  float nenv_v;
  int nenv_synced;
  float mod_env_v;
  float mod_osc_s, mod_osc_c;

  /* precomputed constants */
  float sr, inv_sr;
  int wave_sel;                     /* 0 = sine, 1 = triangle, 2 = saw */
  int mod_is_exp, mod_is_sine;      /* pitch-mod source select */
  int nenv_is_ring, nenv_is_linear; /* noise envelope mode select */
  int mod_pitch_active;
  int osc_env_in_attack, nenv_in_attack;
  int use_eq;
  float osc_atk, osc_dcy, n_atk, n_dcy;
  float osc_freq_step;   /* base osc step, cycles/sample */
  float mod_pitch_scale; /* semitone depth / 12, may be 0 */
  float osc_atk_coeff, osc_dcy_coeff;
  float nenv_atk_coeff, nenv_dcy_coeff;
  float mod_dcy_coeff, mod_rot_s, mod_rot_c;
  float mod_alpha_precomp, mod_one_minus_alpha;
  float lin_atk, lin_dcy, inv_lin_atk, inv_lin_dcy;
  float mod_env_period, inv_mod_env_period, mod_env_amp_correction;
  float dist_amt, level;
  float osc_gain_mix, noise_gain_mix;
  float osc_vel_g, noise_vel_g;
} po32_synth_voice_t;

/*
 * Prepare a voice to render one drum hit.
 *
 * A zero sample rate, or a non-positive or non-finite duration, leaves the
 * voice inert: it reports done immediately, has no samples remaining, and
 * renders nothing.
 */
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
