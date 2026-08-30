/*
 * PO-32 drum synthesizer.
 *
 * All transcendentals (sinf, cosf, powf, expf, sqrtf) replaced with
 * lookup tables:
 *
 *   - 2048-entry sine table with linear interpolation
 *   - 256-entry exp2 fractional table + integer bit shift
 *   - log2 via float bit extraction + degree-3 minimax polynomial
 *   - Quake-style fast inverse sqrt + Newton refinement
 *
 * The render loop uses only adds, multiplies, and table lookups.
 * <math.h> is NOT included; only <stdint.h> and <string.h> are needed.
 *
 * Total static table footprint: (2048 + 256) * 4 = ~9 KB.
 */

#include "po32_synth.h"
#include "po32_lut.h"

#include <stdint.h>

static void synth_zero(void *ptr, size_t n) {
  unsigned char *p = (unsigned char *)ptr;
  while (n-- > 0u)
    *p++ = 0u;
}

/* ── Aliases so the rest of the file reads cleanly ──────────────── */

#define LUT_PI         PO32_LUT_PI
#define LUT_TWO_PI     PO32_LUT_TWO_PI
#define LUT_INV_TWO_PI PO32_LUT_INV_TWO_PI

#define lut_sinf   po32_lut_sinf
#define lut_cosf   po32_lut_cosf
#define lut_exp2f  po32_lut_exp2f
#define lut_log2f  po32_lut_log2f
#define lut_powf   po32_lut_powf
#define lut_expf   po32_lut_expf
#define lut_pow10f po32_lut_pow10f

/* sqrtf via Quake inv-sqrt — only used in synth, not worth sharing */
static float lut_sqrtf(float x) {
  union {
    float f;
    uint32_t u;
  } conv;
  float y, half_x;

  if (x <= 0.0f)
    return 0.0f;

  half_x = x * 0.5f;
  conv.f = x;
  conv.u = 0x5F375A86u - (conv.u >> 1);
  y = conv.f;
  y = y * (1.5f - half_x * y * y);
  y = y * (1.5f - half_x * y * y);

  return x * y;
}

/* ── Synth constants ────────────────────────────────────────────── */

#define SYNTH_ENV_FLOOR_GAIN 0.001f
#define SYNTH_ENV_SPAN       (1.0f / SYNTH_ENV_FLOOR_GAIN)

#define SYNTH_FREQ_MIN_HZ 20.0f
#define SYNTH_FREQ_MAX_HZ 20000.0f

#define SYNTH_DECAY_MIN_SECONDS 0.01f
#define SYNTH_DECAY_MAX_SECONDS 10.0f

/* Precomputed: powf(10, -7/5) */
#define SYNTH_ATTACK_SCALE_AT_ZERO 0.01995262315f
#define SYNTH_ATTACK_EXPONENT_SPAN (12.0f / 5.0f)

#define SYNTH_MIDI_MAX_VELOCITY 127.0f
#define SYNTH_MIDI_HALF_RANGE   63.0f

#define SYNTH_VELOCITY_CURVE_DB 37.0f
#define SYNTH_MIX_TILT_DB       25.0f

#define SYNTH_MOD_MAX_SEMITONES    96.0f
#define SYNTH_SEMITONES_PER_OCTAVE 12.0f
#define SYNTH_SINE_MOD_MAX_HZ      2000.0f

#define SYNTH_EQ_MAX_ABS_DB       40.0f
#define SYNTH_EQ_ENABLE_THRESH_DB 0.5f

#define SYNTH_FILTER_Q_MIN 0.1f
#define SYNTH_FILTER_Q_MAX 10000.0f

#define SYNTH_FILTER_FREQ_LIMIT_FRAC_SR 0.45f

#define SYNTH_ONE_THIRD  (1.0f / 3.0f)
#define SYNTH_TWO_THIRDS (2.0f / 3.0f)

#define SYNTH_MOD_ALPHA_MIN 0.001f

#define SYNTH_MOD_ENV_PERIOD_THRESH_S   1.5e-3f
#define SYNTH_MOD_ENV_PERIOD_SCALE_SLOW 2.0f
#define SYNTH_MOD_ENV_PERIOD_SCALE_FAST 8.0f
#define SYNTH_MOD_ENV_FAST_GAIN_NUM     0.5f

#define SYNTH_LEVEL_MIN_PARAM 1.0e-4f
#define SYNTH_GAIN_SILENCE_DB (-500.0f)

#define SYNTH_DISTORT_MIN_AMOUNT 0.01f

#define SYNTH_DISTORT_DRIVE_SCALE    200.0f
#define SYNTH_DISTORT_LOW_GAIN_SLOPE (1.0f / 30.0f)
#define SYNTH_DISTORT_CENTER_OFFSET  (1.0f / 3.0f)
#define SYNTH_DISTORT_LOW_DC_SLOPE   (-2.0f / 27.0f)
#define SYNTH_DISTORT_LOW_DC_BIAS    (-9.0f / 27.0f)
#define SYNTH_DISTORT_STRONG_DC_BIAS (-11.0f / 27.0f)

#define SYNTH_DISTORT_STRONG_GAIN_NUM    0.2f
#define SYNTH_DISTORT_STRONG_GAIN_OFFSET 0.4827586f
#define SYNTH_DISTORT_STRONG_GAIN_BASE   0.58f

#define SYNTH_RAND_MULTIPLIER 1103515245u
#define SYNTH_RAND_INCREMENT  12345u
#define SYNTH_RAND_MASK       0x7fffffffu
#define SYNTH_RAND_MAX        2147483647.0f

/*
 * Exclusive upper bound for float sample counts.  SIZE_MAX + 1 is always a
 * power of two, so every float strictly below (float)SIZE_MAX lands inside
 * size_t's range no matter which way that cast rounds.
 */
#define SYNTH_SAMPLES_LIMIT_F ((float)SIZE_MAX)

/* ── Small helpers ──────────────────────────────────────────────── */

static float synth_db_to_gain(float db) {
  return lut_pow10f(db / 20.0f);
}

static float synth_clamp(float x, float lo, float hi) {
  if (x < lo)
    return lo;
  if (x > hi)
    return hi;
  return x;
}

static float synth_clamp_sr_freq(float freq, float sample_rate) {
  float max_freq = sample_rate * SYNTH_FILTER_FREQ_LIMIT_FRAC_SR;
  return (freq > max_freq) ? max_freq : freq;
}

static float synth_param_exp_range(float x, float min_value, float max_value) {
  return min_value * lut_powf(max_value / min_value, x);
}

static float synth_wrap_phase01(float x) {
  x = x - (float)(int)x;
  return (x < 0.0f) ? (x + 1.0f) : x;
}

/* fabsf without <math.h> */
static float synth_fabsf(float x) {
  return (x < 0.0f) ? -x : x;
}

static float synth_sym_limit(float x) {
  return 0.5f * (synth_fabsf(x + 1.0f) - synth_fabsf(x - 1.0f));
}

/* ── Envelope curves ─────────────────────────────────────────────── */

static float synth_attack_time(float x) {
  if (x <= 0.0f)
    return 0.0f;
  return x * SYNTH_ATTACK_SCALE_AT_ZERO * lut_pow10f(SYNTH_ATTACK_EXPONENT_SPAN * x);
}

static float synth_decay_time(float x) {
  return synth_param_exp_range(x, SYNTH_DECAY_MIN_SECONDS, SYNTH_DECAY_MAX_SECONDS);
}

static float synth_exp_decay_env(float t, float decay_time) {
  if (decay_time <= 0.0f)
    return 0.0f;
  return lut_powf(SYNTH_ENV_FLOOR_GAIN, t / decay_time);
}

/* ── Gain curves ─────────────────────────────────────────────────── */

static float synth_level_gain(float level) {
  float db;

  if (level < SYNTH_LEVEL_MIN_PARAM)
    return 0.0f;

  db = 60.0f * level - 49.0f - 1.0f / level;
  if (db < SYNTH_GAIN_SILENCE_DB)
    return 0.0f;

  return synth_db_to_gain(db);
}

static float synth_velocity_gain(int velocity, float sensitivity) {
  float normalized_inverse = (SYNTH_MIDI_MAX_VELOCITY - (float)velocity) / SYNTH_MIDI_HALF_RANGE;
  float x = 1.0f - normalized_inverse * sensitivity;
  x = synth_clamp(x, 0.0f, 1.0f);
  return x * synth_db_to_gain(SYNTH_VELOCITY_CURVE_DB * (x - 1.0f));
}

static float synth_mix_osc_gain(float mix) {
  float pan = mix * 2.0f - 1.0f;
  if (pan < 0.0f)
    return 1.0f;
  return (1.0f - pan) * synth_db_to_gain(-SYNTH_MIX_TILT_DB * pan);
}

static float synth_mix_noise_gain(float mix) {
  float pan = mix * 2.0f - 1.0f;
  if (pan >= 0.0f)
    return 1.0f;
  return (1.0f + pan) * synth_db_to_gain(SYNTH_MIX_TILT_DB * pan);
}

/* ── Frequency mapping ───────────────────────────────────────────── */

static float synth_param_to_hz(float x) {
  return synth_param_exp_range(x, SYNTH_FREQ_MIN_HZ, SYNTH_FREQ_MAX_HZ);
}

static float synth_param_to_q(float x) {
  return synth_param_exp_range(x, SYNTH_FILTER_Q_MIN, SYNTH_FILTER_Q_MAX);
}

/* ── Distortion waveshaper ───────────────────────────────────────── */

static float synth_distort(float x, float amount) {
  float drive = SYNTH_DISTORT_DRIVE_SCALE * amount * amount * amount;
  float gain, xc, xp, pos, xn, neg;

  if (drive <= 0.0f)
    return x;

  if (drive < 1.0f) {
    float clip_limit;
    float dc_bias;

    gain = 1.0f - drive * SYNTH_DISTORT_LOW_GAIN_SLOPE;
    clip_limit = (1.0f + lut_sqrtf((drive + 3.0f) / drive)) * SYNTH_DISTORT_CENTER_OFFSET;
    dc_bias = SYNTH_DISTORT_LOW_DC_SLOPE * drive + SYNTH_DISTORT_LOW_DC_BIAS;

    xc = x - SYNTH_DISTORT_CENTER_OFFSET;
    xc = synth_clamp(xc, -clip_limit, clip_limit);

    return (xc * (drive * (synth_fabsf(xc) - xc * xc) + 1.0f) - dc_bias) * gain;
  }

  gain = SYNTH_DISTORT_STRONG_GAIN_NUM / (drive - SYNTH_DISTORT_STRONG_GAIN_OFFSET) +
         SYNTH_DISTORT_STRONG_GAIN_BASE;

  xp = x * drive - SYNTH_DISTORT_CENTER_OFFSET;
  xc = synth_sym_limit(xp);
  pos = xc * (synth_fabsf(xc) - xc * xc + 1.0f) - SYNTH_DISTORT_STRONG_DC_BIAS;

  xn = -x * drive - SYNTH_DISTORT_CENTER_OFFSET;
  xc = synth_sym_limit(xn);
  neg = xc * (synth_fabsf(xc) - xc * xc + 1.0f) - SYNTH_DISTORT_STRONG_DC_BIAS;

  return 0.5f * (pos - neg) * gain;
}

/* ── PRNG ───────────────────────────────────────────────────────── */

static float synth_randf(uint32_t *state) {
  *state = *state * SYNTH_RAND_MULTIPLIER + SYNTH_RAND_INCREMENT;
  return (float)(*state & SYNTH_RAND_MASK) * (1.0f / SYNTH_RAND_MAX);
}

/* ── Biquad filter ──────────────────────────────────────────────── */

typedef struct {
  float b0, b1, b2, a1, a2;
  float z1, z2;
} synth_biquad_t;

static void synth_biquad_reset(synth_biquad_t *bq) {
  bq->z1 = 0.0f;
  bq->z2 = 0.0f;
}

static void synth_biquad_design_lp(synth_biquad_t *bq, float freq, float q, float sr) {
  float w0 = LUT_TWO_PI * freq / sr;
  float alpha = lut_sinf(w0) / (2.0f * q);
  float cos_w0 = lut_cosf(w0);
  float a0 = 1.0f + alpha;

  bq->b0 = ((1.0f - cos_w0) * 0.5f) / a0;
  bq->b1 = (1.0f - cos_w0) / a0;
  bq->b2 = bq->b0;
  bq->a1 = (-2.0f * cos_w0) / a0;
  bq->a2 = (1.0f - alpha) / a0;
  synth_biquad_reset(bq);
}

static void synth_biquad_design_bp(synth_biquad_t *bq, float freq, float q, float sr) {
  float w0 = LUT_TWO_PI * freq / sr;
  float alpha = lut_sinf(w0) / (2.0f * q);
  float cos_w0 = lut_cosf(w0);
  float a0 = 1.0f + alpha;

  bq->b0 = alpha / a0;
  bq->b1 = 0.0f;
  bq->b2 = -alpha / a0;
  bq->a1 = (-2.0f * cos_w0) / a0;
  bq->a2 = (1.0f - alpha) / a0;
  synth_biquad_reset(bq);
}

static void synth_biquad_design_hp(synth_biquad_t *bq, float freq, float q, float sr) {
  float w0 = LUT_TWO_PI * freq / sr;
  float alpha = lut_sinf(w0) / (2.0f * q);
  float cos_w0 = lut_cosf(w0);
  float a0 = 1.0f + alpha;

  bq->b0 = ((1.0f + cos_w0) * 0.5f) / a0;
  bq->b1 = (-(1.0f + cos_w0)) / a0;
  bq->b2 = bq->b0;
  bq->a1 = (-2.0f * cos_w0) / a0;
  bq->a2 = (1.0f - alpha) / a0;
  synth_biquad_reset(bq);
}

static void synth_biquad_design_peak(synth_biquad_t *bq, float freq, float gain_db, float sr) {
  float A = lut_pow10f(gain_db / 40.0f);
  float w0 = LUT_TWO_PI * freq / sr;
  float alpha = lut_sinf(w0) * 0.5f;
  float cos_w0 = lut_cosf(w0);
  float a0 = 1.0f + alpha / A;

  bq->b0 = (1.0f + alpha * A) / a0;
  bq->b1 = (-2.0f * cos_w0) / a0;
  bq->b2 = (1.0f - alpha * A) / a0;
  bq->a1 = (-2.0f * cos_w0) / a0;
  bq->a2 = (1.0f - alpha / A) / a0;
  synth_biquad_reset(bq);
}

static float synth_biquad_process(synth_biquad_t *bq, float in) {
  float out = bq->b0 * in + bq->z1;
  bq->z1 = bq->b1 * in - bq->a1 * out + bq->z2;
  bq->z2 = bq->b2 * in - bq->a2 * out;
  return out;
}

/*
 * Duration (seconds) to sample count, or 0 when the request cannot be
 * rendered.
 *
 * Every public entry point that accepts a duration funnels through this so
 * the float-to-size_t conversion below stays defined.  Converting a value
 * outside size_t's range - a negative, infinite, or simply enormous product
 * - is undefined behaviour (C99 6.3.1.4), and NaN has no integer value at
 * all.  The comparisons are written negated so NaN, which compares false
 * against everything, takes the reject branch.  A zero sample rate yields
 * no samples, which keeps callers from dividing by it downstream.
 */
static size_t synth_duration_to_samples(float sample_rate, float seconds) {
  float samples;

  if (!(sample_rate > 0.0f) || !(seconds > 0.0f))
    return 0u;

  samples = sample_rate * seconds;
  if (!(samples < SYNTH_SAMPLES_LIMIT_F))
    return 0u;

  return (size_t)samples;
}

/* ── Public API ──────────────────────────────────────────────────── */

void po32_synth_init(po32_synth_t *synth, uint32_t sample_rate) {
  if (synth == NULL)
    return;
  synth->sample_rate = sample_rate;
}

size_t po32_synth_samples_for_duration(const po32_synth_t *synth, float seconds) {
  if (synth == NULL)
    return 0;
  return synth_duration_to_samples((float)synth->sample_rate, seconds);
}

/*
 * One-shot render.
 *
 * A thin wrapper over the streaming voice so there is exactly one render
 * loop in this file: a duplicate loop here would silently drift out of
 * step with the voice as either side is optimized.
 */
po32_status_t po32_synth_render(const po32_synth_t *synth, const po32_patch_params_t *params,
                                int velocity, float duration, float *out, size_t out_capacity,
                                size_t *out_len) {
  po32_synth_voice_t voice;

  if (synth == NULL || params == NULL || out == NULL || out_len == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  po32_synth_voice_init(&voice, synth->sample_rate, params, velocity, duration);
  return po32_synth_voice_render_f32(&voice, out, out_capacity, out_len);
}

/* ── Streaming voice ───────────────────────────────────────────── */

static void synth_biquad_to_flat(const synth_biquad_t *bq, float *flat) {
  flat[0] = bq->b0;
  flat[1] = bq->b1;
  flat[2] = bq->b2;
  flat[3] = bq->a1;
  flat[4] = bq->a2;
  flat[5] = bq->z1;
  flat[6] = bq->z2;
}

static void synth_biquad_from_flat(const float *flat, synth_biquad_t *bq) {
  bq->b0 = flat[0];
  bq->b1 = flat[1];
  bq->b2 = flat[2];
  bq->a1 = flat[3];
  bq->a2 = flat[4];
  bq->z1 = flat[5];
  bq->z2 = flat[6];
}

void po32_synth_voice_init(po32_synth_voice_t *v, uint32_t sample_rate,
                           const po32_patch_params_t *params, int velocity, float duration) {
  float sr, inv_sr, mix, eq_db, eq_freq_hz, mod_rate, mod_vel_g;
  float osc_freq, mod_sm, mod_mode, mod_decay_time, mod_rate_hz;
  float nf_freq, nf_q;
  synth_biquad_t nf, ef;

  if (v == NULL)
    return;

  /*
   * Zero before rejecting the parameters: this initializer reports no
   * status, so a refused init must still leave an inert voice rather than
   * an indeterminate one - or, on a reused voice, the still-live state of
   * the previous hit.
   */
  synth_zero(v, sizeof(*v));
  if (params == NULL)
    return;

  v->sample_rate = sample_rate;
  sr = (float)sample_rate;
  v->sr = sr;
  v->total_samples = synth_duration_to_samples(sr, duration);

  /*
   * A zero sample rate leaves the voice inert: the precomputation below
   * divides by sr, and the resulting NaNs would reach the LUT helpers,
   * whose float-to-int conversions are undefined on NaN.  The zeroed
   * struct already reports done with nothing left to render.
   */
  if (sample_rate == 0u)
    return;

  inv_sr = 1.0f / sr;
  v->inv_sr = inv_sr;

  osc_freq = synth_param_to_hz(params->OscFreq);
  v->osc_atk = synth_attack_time(params->OscAtk);
  v->osc_dcy = synth_decay_time(params->OscDcy);

  mod_sm = (params->ModAmt - 0.5f) * (2.0f * SYNTH_MOD_MAX_SEMITONES);
  mod_mode = params->ModMode;
  mod_rate = params->ModRate;

  nf_freq = synth_clamp_sr_freq(synth_param_to_hz(params->NFilFrq), sr);
  nf_q = synth_param_to_q(params->NFilQ);
  v->n_atk = synth_attack_time(params->NEnvAtk);
  v->n_dcy = synth_decay_time(params->NEnvDcy);

  mix = params->Mix;
  v->dist_amt = params->DistAmt;

  eq_db = (params->EQGain - 0.5f) * (2.0f * SYNTH_EQ_MAX_ABS_DB);
  eq_freq_hz = synth_clamp_sr_freq(synth_param_to_hz(params->EQFreq), sr);
  v->level = synth_level_gain(params->Level);

  v->osc_gain_mix = synth_mix_osc_gain(mix);
  v->noise_gain_mix = synth_mix_noise_gain(mix);
  v->osc_vel_g = synth_velocity_gain(velocity, params->OscVel);
  v->noise_vel_g = synth_velocity_gain(velocity, params->NVel);

  mod_vel_g = synth_velocity_gain(velocity, params->ModVel);
  mod_sm *= mod_vel_g;

  if (params->NFilMod < SYNTH_ONE_THIRD)
    synth_biquad_design_lp(&nf, nf_freq, nf_q, sr);
  else if (params->NFilMod < SYNTH_TWO_THIRDS)
    synth_biquad_design_bp(&nf, nf_freq, nf_q, sr);
  else
    synth_biquad_design_hp(&nf, nf_freq, nf_q, sr);
  synth_biquad_to_flat(&nf, v->noise_filt);

  v->use_eq = (synth_fabsf(eq_db) > SYNTH_EQ_ENABLE_THRESH_DB);
  if (v->use_eq) {
    synth_biquad_design_peak(&ef, eq_freq_hz, eq_db, sr);
    synth_biquad_to_flat(&ef, v->eq_filt);
  }

  mod_decay_time = synth_decay_time(mod_rate);
  mod_rate_hz = (mod_mode < SYNTH_TWO_THIRDS) ? (SYNTH_SINE_MOD_MAX_HZ * mod_rate)
                                              : synth_param_to_hz(mod_rate);

  v->mod_alpha_precomp = 1.0f - lut_expf(-LUT_TWO_PI * mod_rate_hz / sr);
  v->mod_alpha_precomp = synth_clamp(v->mod_alpha_precomp, SYNTH_MOD_ALPHA_MIN, 1.0f);
  v->mod_one_minus_alpha = 1.0f - v->mod_alpha_precomp;

  v->wave_sel = (params->OscWave < SYNTH_ONE_THIRD)    ? 0
                : (params->OscWave < SYNTH_TWO_THIRDS) ? 1
                                                       : 2;
  v->mod_is_exp = (mod_mode < SYNTH_ONE_THIRD);
  v->mod_is_sine = (!v->mod_is_exp && mod_mode < SYNTH_TWO_THIRDS);
  v->nenv_is_ring = (params->NEnvMod > SYNTH_TWO_THIRDS);
  v->nenv_is_linear = (!v->nenv_is_ring && params->NEnvMod > SYNTH_ONE_THIRD);

  v->osc_freq_step = osc_freq * inv_sr;
  v->mod_pitch_scale = mod_sm * (1.0f / SYNTH_SEMITONES_PER_OCTAVE);
  v->mod_pitch_active = (v->mod_pitch_scale != 0.0f);

  /* Oscillator amplitude envelope recursion.
   * attack(t) = FLOOR * SPAN^(t/atk), decay(t) = FLOOR^(t/dcy), so each
   * advances by a constant per-sample ratio. */
  v->osc_env_in_attack = (v->osc_atk > 0.0f);
  v->osc_atk_coeff = v->osc_env_in_attack ? lut_powf(SYNTH_ENV_SPAN, inv_sr / v->osc_atk) : 1.0f;
  v->osc_dcy_coeff = lut_powf(SYNTH_ENV_FLOOR_GAIN, inv_sr / v->osc_dcy);

  /* Noise envelope: exp mode shares the same recursion; ring mode reuses
   * the decay recursion for its decay factor. */
  v->nenv_in_attack = (v->n_atk > 0.0f);
  v->nenv_atk_coeff = v->nenv_in_attack ? lut_powf(SYNTH_ENV_SPAN, inv_sr / v->n_atk) : 1.0f;
  v->nenv_dcy_coeff = lut_powf(SYNTH_ENV_FLOOR_GAIN, inv_sr / v->n_dcy);

  /* Linear noise envelope: reciprocals hoisted out of the loop. The
   * attack branch is only reachable when lin_atk > 0 (t >= 0); lin_dcy
   * is always > 0 (n_dcy >= SYNTH_DECAY_MIN_SECONDS). */
  v->lin_atk = v->n_atk * SYNTH_TWO_THIRDS;
  v->lin_dcy = v->n_dcy * SYNTH_TWO_THIRDS;
  v->inv_lin_atk = (v->lin_atk > 0.0f) ? (1.0f / v->lin_atk) : 0.0f;
  v->inv_lin_dcy = 1.0f / v->lin_dcy;

  /* Pitch-mod source: exponential decay is a geometric recursion; the
   * sine LFO is a recursive complex rotation (same scheme as the DPSK
   * modulator oscillator). mod_decay_time is always > 0
   * (synth_decay_time returns >= SYNTH_DECAY_MIN_SECONDS). */
  v->mod_dcy_coeff = lut_powf(SYNTH_ENV_FLOOR_GAIN, inv_sr / mod_decay_time);
  po32_lut_rot_step(LUT_TWO_PI * mod_rate_hz * inv_sr, &v->mod_rot_s, &v->mod_rot_c);

  v->mod_env_amp_correction = 1.0f;
  if (v->nenv_is_ring) {
    float dcy_param = params->NEnvDcy;
    if (dcy_param > SYNTH_LEVEL_MIN_PARAM) {
      v->mod_env_period = (dcy_param / sr) * lut_powf(SYNTH_FILTER_Q_MAX, dcy_param);
      if (v->mod_env_period < SYNTH_MOD_ENV_PERIOD_THRESH_S) {
        v->mod_env_period *= SYNTH_MOD_ENV_PERIOD_SCALE_FAST;
        v->mod_env_amp_correction = lut_sqrtf(SYNTH_MOD_ENV_FAST_GAIN_NUM / v->mod_env_period);
      } else {
        v->mod_env_period *= SYNTH_MOD_ENV_PERIOD_SCALE_SLOW;
      }
    }
  }

  /* Must come after mod_env_period is computed above. */
  v->inv_mod_env_period = (v->mod_env_period > 0.0f) ? (1.0f / v->mod_env_period) : 0.0f;

  po32_synth_voice_reset(v);
}

void po32_synth_voice_reset(po32_synth_voice_t *v) {
  if (v == NULL)
    return;

  v->samples_rendered = 0u;
  v->phase01 = 0.0f;
  v->mod_state = 0.0f;
  v->rand_state = 123456789u;

  /* Envelope and LFO recursions restart from their t = 0 seeds. */
  v->osc_env_v = SYNTH_ENV_FLOOR_GAIN;
  v->osc_env_synced = 0;
  v->nenv_v = SYNTH_ENV_FLOOR_GAIN;
  v->nenv_synced = 0;
  v->mod_env_v = 1.0f;
  v->mod_osc_s = 0.0f;
  v->mod_osc_c = 1.0f;

  /* Reset filter z1/z2 (indices 5, 6) */
  v->noise_filt[5] = 0.0f;
  v->noise_filt[6] = 0.0f;
  v->eq_filt[5] = 0.0f;
  v->eq_filt[6] = 0.0f;
}

size_t po32_synth_voice_samples_remaining(const po32_synth_voice_t *v) {
  if (v == NULL || v->samples_rendered >= v->total_samples)
    return 0u;
  return v->total_samples - v->samples_rendered;
}

int po32_synth_voice_done(const po32_synth_voice_t *v) {
  return v == NULL || v->samples_rendered >= v->total_samples;
}

/* ── Main render loop ───────────────────────────────────────────
 *
 * Only adds, multiplies, compares, and (for the sine oscillator and
 * ring envelope) table lookups per sample. Envelopes advance by one
 * multiply; exp2 runs only while pitch mod is active.
 *
 * Every envelope and LFO here is a recursion whose state lives in the
 * voice, so splitting a render across calls emits exactly the samples a
 * single call would. po32_synth_render() is that single call.
 */
po32_status_t po32_synth_voice_render_f32(po32_synth_voice_t *v, float *out, size_t out_capacity,
                                          size_t *out_len) {
  size_t n, i;
  float inv_sr;
  synth_biquad_t nf, ef;

  if (v == NULL || out == NULL || out_len == NULL)
    return PO32_ERR_INVALID_ARG;

  inv_sr = v->inv_sr;
  n = v->total_samples - v->samples_rendered;
  if (n > out_capacity)
    n = out_capacity;
  *out_len = n;
  if (n == 0u)
    return PO32_OK;

  /* Loaded unconditionally: seven floats once per call, and it keeps ef
     provably initialized for the guarded use inside the loop. */
  synth_biquad_from_flat(v->noise_filt, &nf);
  synth_biquad_from_flat(v->eq_filt, &ef);

  for (i = 0; i < n; ++i) {
    float t = (float)(v->samples_rendered + i) * inv_sr;
    float osc_env, noise_env;
    float mod_sig, freq_mult;
    float osc_sample, noise_raw, noise_sample;
    float sample;

    /* osc_dcy is always > 0 (synth_decay_time returns >= SYNTH_DECAY_MIN_SECONDS). */
    if (v->osc_env_in_attack && t < v->osc_atk) {
      osc_env = v->osc_env_v;
      v->osc_env_v *= v->osc_atk_coeff;
    } else {
      if (!v->osc_env_synced) {
        v->osc_env_v = synth_exp_decay_env(t - v->osc_atk, v->osc_dcy);
        v->osc_env_synced = 1;
      }
      osc_env = v->osc_env_v;
      v->osc_env_v *= v->osc_dcy_coeff;
    }

    if (v->mod_is_exp) {
      mod_sig = v->mod_env_v;
      v->mod_env_v *= v->mod_dcy_coeff;
    } else if (v->mod_is_sine) {
      float ms = v->mod_osc_s * v->mod_rot_c + v->mod_osc_c * v->mod_rot_s;
      float mc = v->mod_osc_c * v->mod_rot_c - v->mod_osc_s * v->mod_rot_s;
      mod_sig = v->mod_osc_s * v->mod_env_v;
      v->mod_osc_s = ms;
      v->mod_osc_c = mc;
      v->mod_env_v *= v->mod_dcy_coeff;
    } else {
      float noise_in = synth_randf(&v->rand_state) * 2.0f - 1.0f;
      v->mod_state = v->mod_state * v->mod_one_minus_alpha + noise_in * v->mod_alpha_precomp;
      mod_sig = v->mod_state;
    }

    freq_mult = v->mod_pitch_active ? lut_exp2f(mod_sig * v->mod_pitch_scale) : 1.0f;
    v->phase01 = synth_wrap_phase01(v->phase01 + v->osc_freq_step * freq_mult);

    if (v->wave_sel == 0) {
      osc_sample = po32_lut_sinf_turns01(v->phase01);
    } else if (v->wave_sel == 1) {
      osc_sample = 2.0f * synth_fabsf(2.0f * v->phase01 - 1.0f) - 1.0f;
    } else {
      osc_sample = 2.0f * v->phase01 - 1.0f;
    }

    osc_sample *= osc_env;

    /* n_dcy is always > 0 (synth_decay_time returns >= SYNTH_DECAY_MIN_SECONDS). */
    if (v->nenv_is_ring) {
      if (t < v->n_atk) {
        noise_env = 1.0f;
      } else {
        float dt = t - v->n_atk;
        float decay;
        if (!v->nenv_synced) {
          v->nenv_v = synth_exp_decay_env(dt, v->n_dcy);
          v->nenv_synced = 1;
        }
        decay = v->nenv_v;
        v->nenv_v *= v->nenv_dcy_coeff;
        if (v->mod_env_period > 0.0f) {
          float saw = synth_wrap_phase01(dt * v->inv_mod_env_period);
          float tri = 1.0f - 2.0f * synth_fabsf(saw - 0.5f);
          noise_env = lut_cosf(LUT_PI * tri) * decay * v->mod_env_amp_correction;
        } else {
          noise_env = decay;
        }
      }
    } else if (v->nenv_is_linear) {
      if (t < v->lin_atk)
        noise_env = t * v->inv_lin_atk;
      else if (t < v->lin_atk + v->lin_dcy)
        noise_env = 1.0f - (t - v->lin_atk) * v->inv_lin_dcy;
      else
        noise_env = 0.0f;
    } else {
      if (v->nenv_in_attack && t < v->n_atk) {
        noise_env = v->nenv_v;
        v->nenv_v *= v->nenv_atk_coeff;
      } else {
        if (!v->nenv_synced) {
          v->nenv_v = synth_exp_decay_env(t - v->n_atk, v->n_dcy);
          v->nenv_synced = 1;
        }
        noise_env = v->nenv_v;
        v->nenv_v *= v->nenv_dcy_coeff;
      }
    }

    noise_raw = synth_randf(&v->rand_state) * 2.0f - 1.0f;
    noise_sample = synth_biquad_process(&nf, noise_raw) * noise_env;

    sample = osc_sample * v->osc_gain_mix * v->osc_vel_g +
             noise_sample * v->noise_gain_mix * v->noise_vel_g;

    if (v->dist_amt > SYNTH_DISTORT_MIN_AMOUNT) {
      sample = synth_distort(sample, v->dist_amt);
    }

    if (v->use_eq) {
      sample = synth_biquad_process(&ef, sample);
    }

    sample *= v->level;
    if (sample > 1.0f)
      sample = 1.0f;
    if (sample < -1.0f)
      sample = -1.0f;
    out[i] = sample;
  }

  /* Persist filter state back */
  synth_biquad_to_flat(&nf, v->noise_filt);
  synth_biquad_to_flat(&ef, v->eq_filt);

  v->samples_rendered += n;
  return PO32_OK;
}
