#include "po_stereo.h"
#include "po_math_internal.h"

#define PO_STEREO_PI           3.14159265358979323846
#define PO_STEREO_SYMBOL_RATE  7812.5
#define PO_STEREO_HOLD_SYMBOLS 128u

static unsigned wire_nibble(const po_stereo_modulator_t *state, uint64_t symbol) {
  if (symbol >= (uint64_t)state->wire_bytes * 2u)
    return 0;
  return (state->wire[(size_t)(symbol / 2u)] >> ((symbol & 1u) * 4u)) & 15u;
}

po_status_t po_stereo_modulator_reset(po_stereo_modulator_t *state) {
  unsigned nibble;
  if (!state || !state->initialized)
    return PO_ERR_INVALID_ARG;
  state->position = 0;
  state->symbol = 0;
  state->phase[0] = 0;
  state->phase[1] = 0;
  nibble = wire_nibble(state, 0);
  state->next_phase[0] = nibble >> 2;
  state->next_phase[1] = nibble & 3u;
  return PO_OK;
}

po_status_t po_stereo_modulator_init(po_stereo_modulator_t *state, const uint8_t *wire,
                                     size_t wire_bytes, uint32_t sample_rate) {
  uint64_t denominator, symbols, active, total;
  if (!state)
    return PO_ERR_INVALID_ARG;
  state->initialized = 0;
  state->wire = NULL;
  state->wire_bytes = 0;
  state->position = 0;
  state->total_frames = 0;
  state->active_frames = 0;
  state->lead_frames = 0;
  state->symbol = 0;
  state->sample_rate = 0;
  state->phase[0] = 0;
  state->phase[1] = 0;
  state->next_phase[0] = 0;
  state->next_phase[1] = 0;
  if (!wire || !wire_bytes)
    return PO_ERR_INVALID_ARG;
  if (sample_rate != 44100 && sample_rate != 48000 && sample_rate != 96000)
    return PO_ERR_RANGE;
  denominator = (uint64_t)sample_rate * 2u;
  if (wire_bytes > ((UINT64_MAX - 15624u) / denominator - 1u - PO_STEREO_HOLD_SYMBOLS) / 2u)
    return PO_ERR_RANGE;
  symbols = (uint64_t)wire_bytes * 2u + 1u + PO_STEREO_HOLD_SYMBOLS;
  active = (symbols * denominator + 15624u) / 15625u;
  /* The wire-byte bound also ensures active <= UINT64_MAX / 15625,
   * so converting active sample positions back to ticks remains safe. */
  total = active + 2u * (sample_rate / 4u);
  if (total > SIZE_MAX / (2u * sizeof(float)))
    return PO_ERR_RANGE;
  state->wire = wire;
  state->wire_bytes = wire_bytes;
  state->total_frames = (size_t)total;
  state->active_frames = (size_t)active;
  state->lead_frames = sample_rate / 4u;
  state->sample_rate = sample_rate;
  state->initialized = 1;
  return po_stereo_modulator_reset(state);
}

size_t po_stereo_modulator_remaining(const po_stereo_modulator_t *state) {
  return state && state->initialized ? state->total_frames - state->position : 0;
}

int po_stereo_modulator_done(const po_stereo_modulator_t *state) {
  return state && state->initialized && state->position == state->total_frames;
}

static double quadrature_wave(unsigned phase, double sine, double cosine) {
  switch (phase & 3u) {
  case 0:
    return sine;
  case 1:
    return cosine;
  case 2:
    return -sine;
  default:
    return -cosine;
  }
}

po_status_t po_stereo_modulator_render_f32(po_stereo_modulator_t *state, float *interleaved,
                                           size_t frame_capacity, size_t *frames_written) {
  size_t count, i;
  if (!frames_written)
    return PO_ERR_INVALID_ARG;
  *frames_written = 0;
  if (!state || !state->initialized || (!interleaved && frame_capacity))
    return PO_ERR_INVALID_ARG;
  if (frame_capacity > SIZE_MAX / (2u * sizeof(float)))
    return PO_ERR_RANGE;
  count = po_stereo_modulator_remaining(state);
  if (count > frame_capacity)
    count = frame_capacity;
  for (i = 0; i < count; ++i, ++state->position) {
    double left = 0, right = 0;
    if (state->position >= state->lead_frames &&
        state->position - state->lead_frames < state->active_frames) {
      uint64_t ticks = (uint64_t)(state->position - state->lead_frames) * 15625u;
      uint64_t denominator = (uint64_t)state->sample_rate * 2u;
      uint64_t symbol = ticks / denominator;
      double fraction = (double)(ticks % denominator) / (double)denominator;
      double sine = po_math_sin(2 * PO_STEREO_PI * fraction);
      double cosine = po_math_cos(2 * PO_STEREO_PI * fraction);
      double mix = fraction < 0.60 ? 0 : fraction > 0.90 ? 1 : (fraction - 0.60) / 0.30;
      while (state->symbol < symbol) {
        unsigned nibble;
        state->phase[0] = state->next_phase[0];
        state->phase[1] = state->next_phase[1];
        nibble = wire_nibble(state, ++state->symbol);
        state->next_phase[0] = (state->phase[0] + (nibble >> 2)) & 3u;
        state->next_phase[1] = (state->phase[1] + (nibble & 3u)) & 3u;
      }
      left = 0.6 * ((1 - mix) * quadrature_wave(state->phase[0], sine, cosine) +
                    mix * quadrature_wave(state->next_phase[0], sine, cosine));
      right = 0.6 * ((1 - mix) * quadrature_wave(state->phase[1], sine, cosine) +
                     mix * quadrature_wave(state->next_phase[1], sine, cosine));
    }
    interleaved[2 * i] = (float)left;
    interleaved[2 * i + 1] = (float)right;
  }
  *frames_written = count;
  return PO_OK;
}

po_status_t po_stereo_decode_requirements(size_t frame_count, uint32_t sample_rate,
                                          size_t *scalar_count, size_t *byte_count,
                                          size_t *wire_capacity) {
  if (scalar_count)
    *scalar_count = 0;
  if (byte_count)
    *byte_count = 0;
  if (wire_capacity)
    *wire_capacity = 0;
  if (!scalar_count || !byte_count || !wire_capacity)
    return PO_ERR_INVALID_ARG;
  /* Bound storage arithmetic and retain exact integer sample coordinates in
   * double. Four scalars/frame hold separate stereo audio and pilot copies. */
  if (sample_rate < 32000 || sample_rate > 192000 ||
      frame_count > SIZE_MAX / (4u * sizeof(double)) ||
      (uint64_t)frame_count > UINT64_C(9007199254740991))
    return PO_ERR_RANGE;
  *scalar_count = frame_count * 4u;
  *byte_count = frame_count;
  *wire_capacity = frame_count / 2u;
  return PO_OK;
}

typedef struct stereo_filter {
  double b0, b1, b2, a1, a2;
} stereo_filter_t;

static stereo_filter_t biquad(double cutoff, double rate, double q, int highpass) {
  double k = po_math_tan(PO_STEREO_PI * cutoff / rate);
  double norm = 1 / (1 + k / q + k * k);
  stereo_filter_t f;
  f.b0 = (highpass ? 1 : k * k) * norm;
  f.b1 = (highpass ? -2 : 2) * f.b0;
  f.b2 = f.b0;
  f.a1 = 2 * (k * k - 1) * norm;
  f.a2 = (1 - k / q + k * k) * norm;
  return f;
}

static stereo_filter_t first_order(double cutoff, double rate) {
  double k = po_math_tan(PO_STEREO_PI * cutoff / rate);
  stereo_filter_t f;
  f.b0 = k / (1 + k);
  f.b1 = f.b0;
  f.b2 = 0;
  f.a1 = (k - 1) / (k + 1);
  f.a2 = 0;
  return f;
}

static void filter_direction(double *data, size_t n, stereo_filter_t f, int reverse) {
  double z1 = 0, z2 = 0;
  size_t j;
  for (j = 0; j < n; ++j) {
    size_t i = (reverse ? n - 1 - j : j) * 2u;
    double input = data[i], output = f.b0 * input + z1;
    z1 = f.b1 * input - f.a1 * output + z2;
    z2 = f.b2 * input - f.a2 * output;
    data[i] = output;
  }
}

static void zero_phase(double *data, size_t n, stereo_filter_t f) {
  filter_direction(data, n, f, 0);
  filter_direction(data, n, f, 1);
}

static double interpolate(const double *data, size_t channel, double t) {
  size_t i = (size_t)t;
  double u = t - (double)i;
  double a = data[2 * (i - 1) + channel], b = data[2 * i + channel];
  double c = data[2 * (i + 1) + channel], d = data[2 * (i + 2) + channel];
  return b + 0.5 * u * (c - a + u * (2 * a - 5 * b + 4 * c - d + u * (3 * (b - c) + d - a)));
}

static size_t demodulate(double *x, double *pilot, uint8_t *nibbles, size_t n, uint32_t rate) {
  size_t count = 0, ch, i;
  double step = 2 * PO_STEREO_PI * PO_STEREO_SYMBOL_RATE / rate;
  double cs = po_math_cos(step), sn = po_math_sin(step), osc_r = 1, osc_i = 0;
  double last_angle = 0, unwrapped = 0, last_clock = 0, previous[2];
  double eighth_period = rate / (PO_STEREO_SYMBOL_RATE * 8);
  int have_previous = 0;
  stereo_filter_t hp = biquad(200, rate, 1 / po_math_sqrt(2.0), 1);
  stereo_filter_t lp1 = first_order(3, rate), lp2 = biquad(3, rate, 1, 0);
  previous[0] = 0;
  previous[1] = 0;
  for (ch = 0; ch < 2; ++ch)
    zero_phase(x + ch, n, hp);
  for (i = 0; i < n; ++i) {
    double power = (x[2 * i] * x[2 * i] + x[2 * i + 1] * x[2 * i + 1]) * 0.5;
    double next_r = osc_r * cs - osc_i * sn;
    pilot[2 * i] = power * osc_r;
    pilot[2 * i + 1] = -power * osc_i;
    osc_i = osc_i * cs + osc_r * sn;
    osc_r = next_r;
    if ((i & 4095u) == 4095u) {
      double magnitude = po_math_sqrt(osc_r * osc_r + osc_i * osc_i);
      osc_r /= magnitude;
      osc_i /= magnitude;
    }
  }
  for (ch = 0; ch < 2; ++ch) {
    zero_phase(pilot + ch, n, lp1);
    zero_phase(pilot + ch, n, lp2);
  }
  for (i = 0; i < n; ++i) {
    double angle = po_math_atan2(pilot[2 * i + 1], pilot[2 * i]);
    double delta = angle - last_angle, clock;
    if (delta > PO_STEREO_PI)
      delta -= 2 * PO_STEREO_PI;
    else if (delta < -PO_STEREO_PI)
      delta += 2 * PO_STEREO_PI;
    unwrapped += delta;
    last_angle = angle;
    clock = (double)i * PO_STEREO_SYMBOL_RATE / rate + unwrapped / (2 * PO_STEREO_PI);
    if (i && po_math_floor(clock) > po_math_floor(last_clock) && clock - last_clock < 1) {
      double t = (double)(i - 1) + (po_math_floor(clock) - last_clock) / (clock - last_clock);
      if (t - eighth_period >= 1 && t + eighth_period < (double)n - 3) {
        unsigned dibits[2];
        for (ch = 0; ch < 2; ++ch) {
          double phase = po_math_atan2(interpolate(x, ch, t + eighth_period),
                                       interpolate(x, ch, t - eighth_period));
          double difference = (previous[ch] - phase) * 2 / PO_STEREO_PI;
          /* Difference is bounded by +/-4; round halfway away from zero. */
          int d = (int)(difference < 0 ? difference - 0.5 : difference + 0.5);
          dibits[ch] = (unsigned)d & 3u;
          previous[ch] = phase;
        }
        if (have_previous)
          nibbles[count++] = (uint8_t)(dibits[1] | (dibits[0] << 2));
        have_previous = 1;
      }
    }
    last_clock = clock;
  }
  return count;
}

static uint8_t packed_byte(const uint8_t *nibbles, size_t index, size_t parity) {
  return (uint8_t)(nibbles[2 * index + parity] | (nibbles[2 * index + parity + 1] << 4));
}

/* Caller has checked that a complete 4096-byte preamble fits at start. */
static int counter_matches(const uint8_t *nibbles, size_t start) {
  if (packed_byte(nibbles, 4092, start) != 0x4d || packed_byte(nibbles, 4093, start) != 0xb8 ||
      packed_byte(nibbles, 4094, start) != 0x16 || packed_byte(nibbles, 4095, start) != 0x5d)
    return 0;
  for (size_t j = 0; j < 4092; ++j)
    if (packed_byte(nibbles, j, start) != (uint8_t)j)
      return 0;
  return 1;
}

static po_status_t find_frame(const uint8_t *nibbles, size_t count, uint8_t *out, size_t capacity,
                              size_t *out_len) {
  size_t parity;
  if (count < 8192)
    return PO_ERR_FRAME;
  for (parity = 0; parity < 2; ++parity) {
    size_t length = (count - parity) / 2u, i;
    for (i = 4092; i + 4 <= length; ++i) {
      size_t start = i - 4092;
      if (counter_matches(nibbles, 2 * start + parity)) {
        *out_len = length - start;
        if (capacity < *out_len)
          return PO_ERR_BUFFER_TOO_SMALL;
        for (size_t j = 0; j < *out_len; ++j)
          out[j] = packed_byte(nibbles, start + j, parity);
        return PO_OK;
      }
    }
  }
  return PO_ERR_FRAME;
}

po_status_t po_stereo_candidates_init_f32(po_stereo_candidates_t *candidates,
                                          const float *interleaved, size_t frame_count,
                                          uint32_t sample_rate, po_stereo_channel_order_t order,
                                          po_stereo_workspace_t *workspace) {
  size_t scalar_count, byte_count, maximum_wire, i, count;
  double *audio, *pilot;
  po_status_t status;
  if (!candidates)
    return PO_ERR_INVALID_ARG;
  *candidates = (po_stereo_candidates_t){0};
  if (!workspace || (!interleaved && frame_count) ||
      (order != PO_STEREO_CHANNEL_NORMAL && order != PO_STEREO_CHANNEL_SWAPPED))
    return PO_ERR_INVALID_ARG;
  status = po_stereo_decode_requirements(frame_count, sample_rate, &scalar_count, &byte_count,
                                         &maximum_wire);
  if (status != PO_OK)
    return status;
  if (!frame_count)
    return PO_ERR_FRAME;
  if (workspace->scalar_count < scalar_count || workspace->byte_count < byte_count)
    return PO_ERR_BUFFER_TOO_SMALL;
  if (!workspace->scalars || !workspace->bytes)
    return PO_ERR_INVALID_ARG;
  audio = workspace->scalars;
  pilot = audio + 2 * frame_count;
  for (i = 0; i < frame_count; ++i) {
    double left = interleaved[2 * i], right = interleaved[2 * i + 1];
    if (!po_math_isfinite(left) || !po_math_isfinite(right))
      return PO_ERR_RANGE;
    audio[2 * i] = order == PO_STEREO_CHANNEL_NORMAL ? left : right;
    audio[2 * i + 1] = order == PO_STEREO_CHANNEL_NORMAL ? right : left;
  }
  if (frame_count < 8192)
    return PO_ERR_FRAME;
  count = demodulate(audio, pilot, workspace->bytes, frame_count, sample_rate);
  *candidates = (po_stereo_candidates_t){workspace->bytes, count, 8184u};
  return PO_OK;
}

po_status_t po_stereo_candidates_next(po_stereo_candidates_t *candidates, uint8_t *out_wire,
                                      size_t wire_capacity, size_t *out_len) {
  size_t sync;
  if (!out_len)
    return PO_ERR_INVALID_ARG;
  *out_len = 0;
  if (!candidates || !candidates->nibbles || (!out_wire && wire_capacity))
    return PO_ERR_INVALID_ARG;
  if (candidates->nibble_count < 8192u)
    return PO_ERR_FRAME;
  for (sync = candidates->next_sync; sync <= candidates->nibble_count - 8u; ++sync) {
    const uint8_t *nibbles = candidates->nibbles;
    size_t start, length;
    if (sync < 8184u)
      continue;
    start = sync - 8184u;
    if (!counter_matches(nibbles, start))
      continue;
    length = (candidates->nibble_count - start) / 2u;
    *out_len = length;
    if (wire_capacity < length)
      return PO_ERR_BUFFER_TOO_SMALL;
    for (size_t j = 0; j < length; ++j)
      out_wire[j] = packed_byte(nibbles, j, start);
    candidates->next_sync = sync + 1u;
    return PO_OK;
  }
  candidates->next_sync = candidates->nibble_count;
  return PO_ERR_FRAME;
}

po_status_t po_stereo_decode_f32(const float *interleaved, size_t frame_count, uint32_t sample_rate,
                                 po_stereo_channel_order_t order, po_stereo_workspace_t *workspace,
                                 uint8_t *out_wire, size_t wire_capacity, size_t *out_len) {
  po_stereo_candidates_t candidates;
  po_status_t status;
  if (!out_len)
    return PO_ERR_INVALID_ARG;
  *out_len = 0;
  if (!out_wire && wire_capacity)
    return PO_ERR_INVALID_ARG;
  status = po_stereo_candidates_init_f32(&candidates, interleaved, frame_count, sample_rate, order,
                                         workspace);
  if (status != PO_OK)
    return status;
  return find_frame(candidates.nibbles, candidates.nibble_count, out_wire, wire_capacity, out_len);
}
