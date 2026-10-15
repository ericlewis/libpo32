#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <string.h>

#include "../src/po32_synth.c"

static float abs_diff(float a, float b) {
  float d = a - b;
  return d < 0.0f ? -d : d;
}

static void test_lut_functions(void) {
  union {
    float f;
    uint32_t u;
  } near_two;
  float value;

  near_two.u = 0x3FFFFFFFu;

  assert(abs_diff(po32_lut_sinf(0.0f), 0.0f) < 0.001f);
  assert(abs_diff(po32_lut_sinf(-PO32_LUT_HALF_PI), -1.0f) < 0.01f);
  assert(abs_diff(po32_lut_cosf(0.0f), 1.0f) < 0.01f);
  value = po32_lut_exp2f(-200.0f);
  assert(value == 0.0f);
  value = po32_lut_exp2f(200.0f);
  assert(value > 1.0e30f);
  value = po32_lut_exp2f(0.0f);
  assert(abs_diff(value, 1.0f) < 0.001f);
  value = po32_lut_log2f(0.0f);
  assert(value == -126.0f);
  value = po32_lut_log2f(1.0f);
  assert(abs_diff(value, 0.0f) < 0.001f);
  value = po32_lut_log2f(near_two.f);
  assert(value > 0.9f);
  assert(po32_lut_powf(0.0f, 2.0f) == 0.0f);
  assert(abs_diff(po32_lut_expf(0.0f), 1.0f) < 0.001f);
  assert(abs_diff(po32_lut_pow10f(0.0f), 1.0f) < 0.001f);
  assert(abs_diff(po32_lut_logf(1.0f), 0.0f) < 0.001f);
  assert(abs_diff(po32_lut_log10f(10.0f), 1.0f) < 0.01f);
}

static void test_synth_helper_functions(void) {
  synth_biquad_t bq;
  float value;

  value = lut_sqrtf(-1.0f);
  assert(value == 0.0f);
  value = lut_sqrtf(4.0f);
  assert(abs_diff(value, 2.0f) < 0.05f);
  assert(synth_clamp(-1.0f, 0.0f, 1.0f) == 0.0f);
  assert(synth_clamp(2.0f, 0.0f, 1.0f) == 1.0f);
  assert(abs_diff(synth_clamp(0.25f, 0.0f, 1.0f), 0.25f) < 0.0001f);
  assert(abs_diff(synth_clamp_sr_freq(1000.0f, 44100.0f), 1000.0f) < 0.0001f);
  assert(abs_diff(synth_clamp_sr_freq(50000.0f, 44100.0f), 19845.0f) < 0.5f);
  assert(abs_diff(synth_wrap_phase01(-0.25f), 0.75f) < 0.0001f);
  assert(abs_diff(synth_wrap_phase01(1.25f), 0.25f) < 0.0001f);
  assert(abs_diff(synth_fabsf(-2.0f), 2.0f) < 0.0001f);
  assert(abs_diff(synth_sym_limit(0.0f), 0.0f) < 0.0001f);
  assert(abs_diff(synth_sym_limit(5.0f), 1.0f) < 0.0001f);

  assert(synth_attack_time(0.0f) == 0.0f);
  assert(synth_attack_time(0.5f) > 0.0f);
  assert(synth_exp_decay_env(0.1f, 0.0f) == 0.0f);
  assert(synth_level_gain(0.0f) == 0.0f);
  assert(synth_level_gain(0.00011f) == 0.0f);
  assert(synth_velocity_gain(127, 0.0f) > 0.9f);
  assert(synth_mix_osc_gain(0.0f) > 0.9f);
  assert(synth_mix_noise_gain(1.0f) > 0.9f);
  assert(abs_diff(synth_param_to_hz(0.0f), 20.0f) < 0.01f);
  assert(abs_diff(synth_param_to_q(0.0f), 0.1f) < 0.001f);

  assert(abs_diff(synth_distort(0.25f, 0.0f), 0.25f) < 0.0001f);
  assert(synth_distort(0.25f, 0.1f) != 0.25f);
  assert(synth_distort(0.25f, 1.0f) != 0.25f);

  synth_biquad_design_lp(&bq, 1000.0f, 0.7f, 44100.0f);
  assert(bq.z1 == 0.0f && bq.z2 == 0.0f);
  synth_biquad_design_bp(&bq, 1000.0f, 0.7f, 44100.0f);
  assert(bq.z1 == 0.0f && bq.z2 == 0.0f);
  synth_biquad_design_hp(&bq, 1000.0f, 0.7f, 44100.0f);
  assert(bq.z1 == 0.0f && bq.z2 == 0.0f);
  synth_biquad_design_peak(&bq, 1000.0f, 6.0f, 44100.0f);
  assert(bq.z1 == 0.0f && bq.z2 == 0.0f);
  value = synth_biquad_process(&bq, 0.5f);
  assert(value != 0.0f);
}

static void test_public_helper_guards(void) {
  po32_synth_t synth;
  po32_patch_params_t params;
  float out[8];
  size_t out_len = 0u;

  po32_synth_init(NULL, 44100u);
  po32_synth_init(&synth, 44100u);
  assert(po32_synth_samples_for_duration(NULL, 1.0f) == 0u);
  assert(po32_synth_samples_for_duration(&synth, 0.0f) == 0u);

  memset(&params, 0, sizeof(params));
  params.Level = 0.836f;
  {
    po32_status_t status = po32_synth_render(&synth, &params, 100, 1.0f, out, 8u, &out_len);
    assert(status == PO32_OK);
  }
  assert(out_len == 8u);

  /* synth_distort: low drive path (drive < 1.0) */
  {
    float d = synth_distort(0.5f, 0.05f);
    assert(d != 0.5f); /* should apply soft clipping */
  }

  /* synth_distort: strong drive path (drive >= 1.0) */
  {
    float d = synth_distort(0.5f, 0.5f);
    assert(d != 0.5f);
  }
}

static void test_normalised_control_reference_points(void) {
  po32_patch_params_t p;
  po32_synth_voice_t v;
  float output[256];
  size_t made;
  /* Independent physical reference points, not a forward/inverse round trip. */
  assert(abs_diff(synth_attack_time(0.25f), 0.0396223f) < 0.00001f);
  assert(abs_diff(synth_attack_time(0.5f), 0.315479f) < 0.0001f);
  assert(abs_diff(synth_attack_time(1.0f), 10.0f) < 0.001f);
  assert(abs_diff(po32_param_mod_semitones(0.25f, 0.0f), -24.0f) < 0.001f);
  assert(abs_diff(po32_param_mod_semitones(0.25f, 0.5f), -12.0f) < 0.001f);
  assert(abs_diff(po32_param_mod_semitones(0.75f, 1.0f), 12.0f) < 0.001f);
  assert(abs_diff(po32_param_mod_hz(0.25f, 1), 1.185687f) < 0.001f);
  assert(abs_diff(po32_param_mod_hz(0.5f, 1), 17.782794f) < 0.002f);
  assert(abs_diff(po32_param_mod_hz(0.25f, 0), 5.0f) < 0.001f);
  assert(abs_diff(po32_param_mod_hz(0.5f, 0), 100.0f) < 0.01f);
  assert(po32_param_mod_hz(0.0f, 0) == 0.0f);
  assert(po32_param_mod_decay_coefficient(0.0f, 1.0f / 44100.0f) == 1.0f);
  assert(abs_diff(po32_param_mod_from_ms(353.333333f), 0.5f) < 0.00001f);
  assert(abs_diff(po32_param_mod_from_hz(17.782794f, 1), 0.5f) < 0.0001f);
  assert(abs_diff(po32_param_mod_from_hz(100.0f, 0), 0.5f) < 0.0001f);
  memset(&p, 0, sizeof(p));
  p.Level = 0.5f;
  p.ModAmt = 1.0f;
  p.ModMode = 0.5f;
  p.ModRate = 0.0f;
  po32_synth_voice_init(&v, 44100, &p, 127, 1.0f);
  assert(v.mod_pitch_scale == 4.0f && v.mod_osc_s == 1.0f);
  assert(v.mod_dcy_coeff == 1.0f);
  assert(po32_synth_voice_render_f32(&v, output, 256, &made) == PO32_OK);
  assert(v.mod_env_v == 1.0f && v.mod_osc_s == 1.0f);
  p.ModMode = 1.0f;
  po32_synth_voice_init(&v, 44100, &p, 127, 1.0f);
  {
    float held = v.mod_state;
    assert(held != 0.0f && v.mod_alpha_precomp == 0.0f);
    assert(po32_synth_voice_render_f32(&v, output, 256, &made) == PO32_OK);
    assert(v.mod_state == held);
  }
}

int main(void) {
  test_normalised_control_reference_points();
  test_lut_functions();
  test_synth_helper_functions();
  test_public_helper_guards();
  return 0;
}
