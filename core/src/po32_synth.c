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

static float synth_exp_attack_env(float t, float attack_time) {
  if (attack_time <= 0.0f)
    return 0.0f;
  return SYNTH_ENV_FLOOR_GAIN * lut_powf(SYNTH_ENV_SPAN, t / attack_time);
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

po32_status_t po32_synth_render(const po32_synth_t *synth, const po32_patch_params_t *params,
                                int velocity, float duration, float *out, size_t out_capacity,
                                size_t *out_len) {
  float sr;
  size_t n, i;
  float osc_freq, osc_atk, osc_dcy;
  float mod_sm, mod_mode, mod_rate;
  float nf_freq, nf_q, n_atk, n_dcy;
  float mix, dist_amt, eq_db, eq_freq_hz, level;
  float osc_gain_mix, noise_gain_mix, osc_vel_g, noise_vel_g;
  float phase01, mod_decay_time, mod_rate_hz;
  float mod_state;
  float mod_vel_g;
  float mod_env_period = 0.0f;
  float mod_env_amp_correction = 1.0f;
  uint32_t rand_state = 123456789u;
  synth_biquad_t noise_filt, eq_filt;
  int use_eq;

  /* Precomputed noise mod alpha (constant per render) */
  float mod_alpha_precomp, mod_one_minus_alpha;

  /* Per-render constants for the sample loop.
   *
   * The exponential envelopes are geometric sequences, so each one is
   * advanced with a single multiply per sample instead of a powf call.
   * The value is re-seeded from the closed form exactly once, at the
   * attack -> decay transition, so the emitted values match the direct
   * evaluation up to float rounding.
   */
  float inv_sr;
  int wave_sel;                     /* 0 = sine, 1 = triangle, 2 = saw */
  int mod_is_exp, mod_is_sine;      /* pitch-mod source select */
  int nenv_is_ring, nenv_is_linear; /* noise envelope mode select */
  float osc_freq_step;              /* base osc step, cycles/sample */
  float mod_pitch_scale;            /* semitone depth / 12, may be 0 */
  int mod_pitch_active;
  int osc_env_in_attack, osc_env_synced;
  float osc_env_v, osc_atk_coeff, osc_dcy_coeff;
  int nenv_in_attack, nenv_synced;
  float nenv_v, nenv_atk_coeff, nenv_dcy_coeff;
  float mod_env_v, mod_dcy_coeff;
  float mod_osc_s, mod_osc_c, mod_rot_s, mod_rot_c;
  float lin_atk, lin_dcy, inv_lin_atk, inv_lin_dcy;
  float inv_mod_env_period;

  if (synth == NULL || params == NULL || out == NULL || out_len == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  sr = (float)synth->sample_rate;
  n = synth_duration_to_samples(sr, duration);
  if (n > out_capacity)
    n = out_capacity;
  *out_len = n;

  /*
   * Nothing to render.  Returning here also keeps a zero sample rate away
   * from the precomputation below, which would otherwise divide by it and
   * feed the resulting NaNs to the LUT helpers.
   */
  if (n == 0u)
    return PO32_OK;

  osc_freq = synth_param_to_hz(params->OscFreq);
  osc_atk = synth_attack_time(params->OscAtk);
  osc_dcy = synth_decay_time(params->OscDcy);

  mod_sm = (params->ModAmt - 0.5f) * (2.0f * SYNTH_MOD_MAX_SEMITONES);
  mod_mode = params->ModMode;
  mod_rate = params->ModRate;

  nf_freq = synth_clamp_sr_freq(synth_param_to_hz(params->NFilFrq), sr);
  nf_q = synth_param_to_q(params->NFilQ);
  /* synth_param_to_q returns >= SYNTH_FILTER_Q_MIN by construction. */

  n_atk = synth_attack_time(params->NEnvAtk);
  n_dcy = synth_decay_time(params->NEnvDcy);

  mix = params->Mix;
  dist_amt = params->DistAmt;

  eq_db = (params->EQGain - 0.5f) * (2.0f * SYNTH_EQ_MAX_ABS_DB);
  eq_freq_hz = synth_clamp_sr_freq(synth_param_to_hz(params->EQFreq), sr);

  level = synth_level_gain(params->Level);

  osc_gain_mix = synth_mix_osc_gain(mix);
  noise_gain_mix = synth_mix_noise_gain(mix);
  osc_vel_g = synth_velocity_gain(velocity, params->OscVel);
  noise_vel_g = synth_velocity_gain(velocity, params->NVel);

  mod_vel_g = synth_velocity_gain(velocity, params->ModVel);
  mod_sm *= mod_vel_g;

  if (params->NFilMod < SYNTH_ONE_THIRD)
    synth_biquad_design_lp(&noise_filt, nf_freq, nf_q, sr);
  else if (params->NFilMod < SYNTH_TWO_THIRDS)
    synth_biquad_design_bp(&noise_filt, nf_freq, nf_q, sr);
  else
    synth_biquad_design_hp(&noise_filt, nf_freq, nf_q, sr);

  use_eq = (synth_fabsf(eq_db) > SYNTH_EQ_ENABLE_THRESH_DB);
  if (use_eq) {
    synth_biquad_design_peak(&eq_filt, eq_freq_hz, eq_db, sr);
  }

  mod_decay_time = synth_decay_time(mod_rate);
  mod_rate_hz = (mod_mode < SYNTH_TWO_THIRDS) ? (SYNTH_SINE_MOD_MAX_HZ * mod_rate)
                                              : synth_param_to_hz(mod_rate);
  mod_state = 0.0f;
  phase01 = 0.0f;

  /* Precompute noise mod alpha (constant for entire render) */
  mod_alpha_precomp = 1.0f - lut_expf(-LUT_TWO_PI * mod_rate_hz / sr);
  mod_alpha_precomp = synth_clamp(mod_alpha_precomp, SYNTH_MOD_ALPHA_MIN, 1.0f);
  mod_one_minus_alpha = 1.0f - mod_alpha_precomp;

  inv_sr = 1.0f / sr;

  wave_sel = (params->OscWave < SYNTH_ONE_THIRD) ? 0 : (params->OscWave < SYNTH_TWO_THIRDS) ? 1 : 2;
  mod_is_exp = (mod_mode < SYNTH_ONE_THIRD);
  mod_is_sine = (!mod_is_exp && mod_mode < SYNTH_TWO_THIRDS);
  nenv_is_ring = (params->NEnvMod > SYNTH_TWO_THIRDS);
  nenv_is_linear = (!nenv_is_ring && params->NEnvMod > SYNTH_ONE_THIRD);

  osc_freq_step = osc_freq * inv_sr;
  mod_pitch_scale = mod_sm * (1.0f / SYNTH_SEMITONES_PER_OCTAVE);
  mod_pitch_active = (mod_pitch_scale != 0.0f);

  /* Oscillator amplitude envelope recursion.
   * attack(t) = FLOOR * SPAN^(t/atk), decay(t) = FLOOR^(t/dcy), so each
   * advances by a constant per-sample ratio. */
  osc_env_in_attack = (osc_atk > 0.0f);
  osc_env_synced = 0;
  osc_env_v = SYNTH_ENV_FLOOR_GAIN;
  osc_atk_coeff = osc_env_in_attack ? lut_powf(SYNTH_ENV_SPAN, inv_sr / osc_atk) : 1.0f;
  osc_dcy_coeff = lut_powf(SYNTH_ENV_FLOOR_GAIN, inv_sr / osc_dcy);

  /* Noise envelope: exp mode shares the same recursion; ring mode reuses
   * the decay recursion for its decay factor. */
  nenv_in_attack = (n_atk > 0.0f);
  nenv_synced = 0;
  nenv_v = SYNTH_ENV_FLOOR_GAIN;
  nenv_atk_coeff = nenv_in_attack ? lut_powf(SYNTH_ENV_SPAN, inv_sr / n_atk) : 1.0f;
  nenv_dcy_coeff = lut_powf(SYNTH_ENV_FLOOR_GAIN, inv_sr / n_dcy);

  /* Linear noise envelope: reciprocals hoisted out of the loop. The
   * attack branch is only reachable when lin_atk > 0 (t >= 0); lin_dcy
   * is always > 0 (n_dcy >= SYNTH_DECAY_MIN_SECONDS). */
  lin_atk = n_atk * SYNTH_TWO_THIRDS;
  lin_dcy = n_dcy * SYNTH_TWO_THIRDS;
  inv_lin_atk = (lin_atk > 0.0f) ? (1.0f / lin_atk) : 0.0f;
  inv_lin_dcy = 1.0f / lin_dcy;

  /* Pitch-mod source: exponential decay is a geometric recursion; the
   * sine LFO is a recursive complex rotation (same scheme as the DPSK
   * modulator oscillator). mod_decay_time is always > 0
   * (synth_decay_time returns >= SYNTH_DECAY_MIN_SECONDS). */
  mod_env_v = 1.0f;
  mod_dcy_coeff = lut_powf(SYNTH_ENV_FLOOR_GAIN, inv_sr / mod_decay_time);
  mod_osc_s = 0.0f;
  mod_osc_c = 1.0f;
  po32_lut_rot_step(LUT_TWO_PI * mod_rate_hz * inv_sr, &mod_rot_s, &mod_rot_c);

  if (params->NEnvMod > SYNTH_TWO_THIRDS) {
    float dcy_param = params->NEnvDcy;
    if (dcy_param > SYNTH_LEVEL_MIN_PARAM) {
      mod_env_period = (dcy_param / sr) * lut_powf(SYNTH_FILTER_Q_MAX, dcy_param);
      if (mod_env_period < SYNTH_MOD_ENV_PERIOD_THRESH_S) {
        mod_env_period *= SYNTH_MOD_ENV_PERIOD_SCALE_FAST;
        if (mod_env_period > 0.0f) {
          mod_env_amp_correction = lut_sqrtf(SYNTH_MOD_ENV_FAST_GAIN_NUM / mod_env_period);
        }
      } else {
        mod_env_period *= SYNTH_MOD_ENV_PERIOD_SCALE_SLOW;
      }
    }
  }

  /* Must come after mod_env_period is computed above. */
  inv_mod_env_period = (mod_env_period > 0.0f) ? (1.0f / mod_env_period) : 0.0f;

  /* ── Main render loop ─────────────────────────────────────────
   *
   * Only adds, multiplies, compares, and (for the sine oscillator and
   * ring envelope) table lookups per sample. Envelopes advance by one
   * multiply; exp2 runs only while pitch mod is active.
   */
  for (i = 0; i < n; ++i) {
    float t = (float)i * inv_sr;
    float osc_env, noise_env;
    float mod_sig, freq_mult;
    float osc_sample, noise_raw, noise_sample;
    float sample;

    /* osc_dcy is always > 0 (synth_decay_time returns >= SYNTH_DECAY_MIN_SECONDS). */
    if (osc_env_in_attack && t < osc_atk) {
      osc_env = osc_env_v;
      osc_env_v *= osc_atk_coeff;
    } else {
      if (!osc_env_synced) {
        osc_env_v = synth_exp_decay_env(t - osc_atk, osc_dcy);
        osc_env_synced = 1;
      }
      osc_env = osc_env_v;
      osc_env_v *= osc_dcy_coeff;
    }

    if (mod_is_exp) {
      mod_sig = mod_env_v;
      mod_env_v *= mod_dcy_coeff;
    } else if (mod_is_sine) {
      float ms = mod_osc_s * mod_rot_c + mod_osc_c * mod_rot_s;
      float mc = mod_osc_c * mod_rot_c - mod_osc_s * mod_rot_s;
      mod_sig = mod_osc_s * mod_env_v;
      mod_osc_s = ms;
      mod_osc_c = mc;
      mod_env_v *= mod_dcy_coeff;
    } else {
      float noise_in = synth_randf(&rand_state) * 2.0f - 1.0f;
      mod_state = mod_state * mod_one_minus_alpha + noise_in * mod_alpha_precomp;
      mod_sig = mod_state;
    }

    freq_mult = mod_pitch_active ? lut_exp2f(mod_sig * mod_pitch_scale) : 1.0f;
    phase01 = synth_wrap_phase01(phase01 + osc_freq_step * freq_mult);

    if (wave_sel == 0) {
      osc_sample = po32_lut_sinf_turns01(phase01);
    } else if (wave_sel == 1) {
      osc_sample = 2.0f * synth_fabsf(2.0f * phase01 - 1.0f) - 1.0f;
    } else {
      osc_sample = 2.0f * phase01 - 1.0f;
    }

    osc_sample *= osc_env;

    /* n_dcy is always > 0 (synth_decay_time returns >= SYNTH_DECAY_MIN_SECONDS). */
    if (nenv_is_ring) {
      if (t < n_atk) {
        noise_env = 1.0f;
      } else {
        float dt = t - n_atk;
        float decay;
        if (!nenv_synced) {
          nenv_v = synth_exp_decay_env(dt, n_dcy);
          nenv_synced = 1;
        }
        decay = nenv_v;
        nenv_v *= nenv_dcy_coeff;
        if (mod_env_period > 0.0f) {
          float saw = synth_wrap_phase01(dt * inv_mod_env_period);
          float tri = 1.0f - 2.0f * synth_fabsf(saw - 0.5f);
          noise_env = lut_cosf(LUT_PI * tri) * decay * mod_env_amp_correction;
        } else {
          noise_env = decay;
        }
      }
    } else if (nenv_is_linear) {
      if (t < lin_atk)
        noise_env = t * inv_lin_atk;
      else if (t < lin_atk + lin_dcy)
        noise_env = 1.0f - (t - lin_atk) * inv_lin_dcy;
      else
        noise_env = 0.0f;
    } else {
      if (nenv_in_attack && t < n_atk) {
        noise_env = nenv_v;
        nenv_v *= nenv_atk_coeff;
      } else {
        if (!nenv_synced) {
          nenv_v = synth_exp_decay_env(t - n_atk, n_dcy);
          nenv_synced = 1;
        }
        noise_env = nenv_v;
        nenv_v *= nenv_dcy_coeff;
      }
    }

    noise_raw = synth_randf(&rand_state) * 2.0f - 1.0f;
    noise_sample = synth_biquad_process(&noise_filt, noise_raw) * noise_env;

    sample = osc_sample * osc_gain_mix * osc_vel_g + noise_sample * noise_gain_mix * noise_vel_g;

    if (dist_amt > SYNTH_DISTORT_MIN_AMOUNT) {
      sample = synth_distort(sample, dist_amt);
    }

    if (use_eq) {
      sample = synth_biquad_process(&eq_filt, sample);
    }

    sample *= level;
    if (sample > 1.0f)
      sample = 1.0f;
    if (sample < -1.0f)
      sample = -1.0f;
    out[i] = sample;
  }

  return PO32_OK;
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
  float sr, mix, eq_db, eq_freq_hz, mod_rate, mod_vel_g;
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

  v->rand_state = 123456789u;

  v->osc_freq = synth_param_to_hz(params->OscFreq);
  v->osc_atk = synth_attack_time(params->OscAtk);
  v->osc_dcy = synth_decay_time(params->OscDcy);
  v->osc_wave = params->OscWave;

  v->mod_sm = (params->ModAmt - 0.5f) * (2.0f * SYNTH_MOD_MAX_SEMITONES);
  v->mod_mode = params->ModMode;
  mod_rate = params->ModRate;

  v->nf_freq = synth_clamp_sr_freq(synth_param_to_hz(params->NFilFrq), sr);
  v->nf_q = synth_param_to_q(params->NFilQ);
  v->n_atk = synth_attack_time(params->NEnvAtk);
  v->n_dcy = synth_decay_time(params->NEnvDcy);
  v->n_env_mod = params->NEnvMod;

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
  v->mod_sm *= mod_vel_g;

  if (params->NFilMod < SYNTH_ONE_THIRD)
    synth_biquad_design_lp(&nf, v->nf_freq, v->nf_q, sr);
  else if (params->NFilMod < SYNTH_TWO_THIRDS)
    synth_biquad_design_bp(&nf, v->nf_freq, v->nf_q, sr);
  else
    synth_biquad_design_hp(&nf, v->nf_freq, v->nf_q, sr);
  synth_biquad_to_flat(&nf, v->noise_filt);

  v->use_eq = (synth_fabsf(eq_db) > SYNTH_EQ_ENABLE_THRESH_DB);
  if (v->use_eq) {
    synth_biquad_design_peak(&ef, eq_freq_hz, eq_db, sr);
    synth_biquad_to_flat(&ef, v->eq_filt);
  }

  v->mod_decay_time = synth_decay_time(mod_rate);
  v->mod_rate_hz = (v->mod_mode < SYNTH_TWO_THIRDS) ? (SYNTH_SINE_MOD_MAX_HZ * mod_rate)
                                                    : synth_param_to_hz(mod_rate);

  v->mod_alpha_precomp = 1.0f - lut_expf(-LUT_TWO_PI * v->mod_rate_hz / sr);
  v->mod_alpha_precomp = synth_clamp(v->mod_alpha_precomp, SYNTH_MOD_ALPHA_MIN, 1.0f);

  v->mod_env_amp_correction = 1.0f;
  if (v->n_env_mod > SYNTH_TWO_THIRDS) {
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
}

void po32_synth_voice_reset(po32_synth_voice_t *v) {
  uint32_t sr;
  if (v == NULL)
    return;
  sr = v->sample_rate;
  /* Re-init preserving precomputed constants by re-zeroing DSP state only. */
  v->samples_rendered = 0u;
  v->phase = 0.0f;
  v->mod_state = 0.0f;
  v->rand_state = 123456789u;
  /* Reset filter z1/z2 (indices 5, 6) */
  v->noise_filt[5] = 0.0f;
  v->noise_filt[6] = 0.0f;
  v->eq_filt[5] = 0.0f;
  v->eq_filt[6] = 0.0f;
  (void)sr;
}

size_t po32_synth_voice_samples_remaining(const po32_synth_voice_t *v) {
  if (v == NULL || v->samples_rendered >= v->total_samples)
    return 0u;
  return v->total_samples - v->samples_rendered;
}

int po32_synth_voice_done(const po32_synth_voice_t *v) {
  return v == NULL || v->samples_rendered >= v->total_samples;
}

po32_status_t po32_synth_voice_render_f32(po32_synth_voice_t *v, float *out, size_t out_capacity,
                                          size_t *out_len) {
  size_t n, i;
  float sr;
  synth_biquad_t nf, ef;

  if (v == NULL || out == NULL || out_len == NULL)
    return PO32_ERR_INVALID_ARG;

  sr = v->sr;
  n = v->total_samples - v->samples_rendered;
  if (n > out_capacity)
    n = out_capacity;
  *out_len = n;

  synth_biquad_from_flat(v->noise_filt, &nf);
  if (v->use_eq)
    synth_biquad_from_flat(v->eq_filt, &ef);

  for (i = 0; i < n; ++i) {
    float t = (float)(v->samples_rendered + i) / sr;
    float osc_env, noise_env;
    float mod_sig, freq_mult, inst_freq;
    float osc_sample, noise_raw, noise_sample;
    float sample;

    if (v->osc_atk > 0.0f && t < v->osc_atk)
      osc_env = synth_exp_attack_env(t, v->osc_atk);
    else
      osc_env = synth_exp_decay_env(t - v->osc_atk, v->osc_dcy);

    if (v->mod_mode < SYNTH_ONE_THIRD) {
      mod_sig = synth_exp_decay_env(t, v->mod_decay_time);
    } else if (v->mod_mode < SYNTH_TWO_THIRDS) {
      float sine_env = synth_exp_decay_env(t, v->mod_decay_time);
      mod_sig = lut_sinf(LUT_TWO_PI * v->mod_rate_hz * t) * sine_env;
    } else {
      float noise_in = synth_randf(&v->rand_state) * 2.0f - 1.0f;
      v->mod_state = v->mod_state * (1.0f - v->mod_alpha_precomp) + noise_in * v->mod_alpha_precomp;
      mod_sig = v->mod_state;
    }

    freq_mult = lut_exp2f(mod_sig * v->mod_sm / SYNTH_SEMITONES_PER_OCTAVE);
    inst_freq = v->osc_freq * freq_mult;
    v->phase += inst_freq * (LUT_TWO_PI / sr);

    if (v->osc_wave < SYNTH_ONE_THIRD) {
      osc_sample = lut_sinf(v->phase);
    } else if (v->osc_wave < SYNTH_TWO_THIRDS) {
      float ph = synth_wrap_phase01(v->phase * LUT_INV_TWO_PI);
      osc_sample = 2.0f * synth_fabsf(2.0f * ph - 1.0f) - 1.0f;
    } else {
      float ph = synth_wrap_phase01(v->phase * LUT_INV_TWO_PI);
      osc_sample = 2.0f * ph - 1.0f;
    }

    osc_sample *= osc_env;

    if (v->n_env_mod > SYNTH_TWO_THIRDS) {
      if (t < v->n_atk) {
        noise_env = 1.0f;
      } else if (v->mod_env_period > 0.0f) {
        float dt = t - v->n_atk;
        float saw = synth_wrap_phase01(dt / v->mod_env_period);
        float tri = 1.0f - 2.0f * synth_fabsf(saw - 0.5f);
        float ring = lut_cosf(LUT_PI * tri);
        float decay = synth_exp_decay_env(dt, v->n_dcy);
        noise_env = ring * decay * v->mod_env_amp_correction;
      } else {
        noise_env = synth_exp_decay_env(t - v->n_atk, v->n_dcy);
      }
    } else if (v->n_env_mod > SYNTH_ONE_THIRD) {
      float lin_atk = v->n_atk * SYNTH_TWO_THIRDS;
      float lin_dcy = v->n_dcy * SYNTH_TWO_THIRDS;

      if (t < lin_atk)
        noise_env = t / lin_atk;
      else if (t < lin_atk + lin_dcy)
        noise_env = 1.0f - (t - lin_atk) / lin_dcy;
      else
        noise_env = 0.0f;
    } else {
      if (v->n_atk > 0.0f && t < v->n_atk)
        noise_env = synth_exp_attack_env(t, v->n_atk);
      else
        noise_env = synth_exp_decay_env(t - v->n_atk, v->n_dcy);
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
  if (v->use_eq)
    synth_biquad_to_flat(&ef, v->eq_filt);

  v->samples_rendered += n;
  return PO32_OK;
}
