#ifndef PO_RESAMPLE_INTERNAL_H
#define PO_RESAMPLE_INTERNAL_H

#include "po_math_internal.h"
#include <stddef.h>

/* Private Lanczos-8 interpolation. Callers validate finite PCM and rates,
 * and bound sample indices so the kernel's signed tap indices fit int64_t. */
typedef struct po_resample_kernel {
  double cutoff, radius;
  double sin_step, cos_step;
  double window_sin_step, window_cos_step;
} po_resample_kernel_t;

static inline void po_resample_init(po_resample_kernel_t *kernel, double input_rate,
                                    double output_rate) {
  kernel->cutoff = input_rate > output_rate ? output_rate / input_rate : 1.0;
  kernel->radius = 8.0 / kernel->cutoff;
  kernel->sin_step = po_math_sin(-PO_MATH_PI * kernel->cutoff);
  kernel->cos_step = po_math_cos(-PO_MATH_PI * kernel->cutoff);
  kernel->window_sin_step = po_math_sin(-PO_MATH_PI * kernel->cutoff / 8.0);
  kernel->window_cos_step = po_math_cos(-PO_MATH_PI * kernel->cutoff / 8.0);
}

/* Zero-pad the source, including its contribution to the weight denominator.
 * Keep index * input_rate / output_rate in this order: changing the rounding
 * can change quantized codec output. Sinusoid recurrences avoid per-tap trig. */
static inline double po_resample_f32(const po_resample_kernel_t *kernel, const float *pcm,
                                     size_t count, double input_rate, double output_rate,
                                     int64_t index) {
  double position, z, sine, cosine, window_sine, window_cosine;
  double sum = 0.0, weight_sum = 0.0;
  int64_t first, last, i;
  if (input_rate == output_rate)
    return index >= 0 && (uint64_t)index < count ? pcm[(size_t)index] : 0.0;
  position = (double)index * input_rate / output_rate;
  if (position < -kernel->radius || position >= (double)count + kernel->radius)
    return 0.0;
  first = (int64_t)po_math_floor(position - kernel->radius) + 1;
  last = (int64_t)po_math_floor(position + kernel->radius);
  z = PO_MATH_PI * (position - (double)first) * kernel->cutoff;
  sine = po_math_sin(z);
  cosine = po_math_cos(z);
  window_sine = po_math_sin(z / 8.0);
  window_cosine = po_math_cos(z / 8.0);
  for (i = first; i <= last; ++i) {
    double distance = PO_MATH_PI * (position - (double)i) * kernel->cutoff;
    double magnitude = distance < 0.0 ? -distance : distance;
    double weight = magnitude < 1e-8 ? 1.0 : 8.0 * sine * window_sine / (distance * distance);
    double next;
    weight_sum += weight;
    if (i >= 0 && (uint64_t)i < count)
      sum += weight * pcm[(size_t)i];
    next = sine * kernel->cos_step + cosine * kernel->sin_step;
    cosine = cosine * kernel->cos_step - sine * kernel->sin_step;
    sine = next;
    next = window_sine * kernel->window_cos_step + window_cosine * kernel->window_sin_step;
    window_cosine = window_cosine * kernel->window_cos_step - window_sine * kernel->window_sin_step;
    window_sine = next;
  }
  return weight_sum > 1e-12 ? sum / weight_sum : 0.0;
}

#endif
