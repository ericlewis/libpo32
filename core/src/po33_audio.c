#include "po33.h"
#include "po_resample_internal.h"

po_status_t po33_sample_count_for_pcm(size_t count, uint32_t rate, size_t *out_count) {
  size_t result;
  if (!out_count)
    return PO_ERR_INVALID_ARG;
  *out_count = 0;
  if (rate < 8000u || rate > 192000u || count > SIZE_MAX / sizeof(float))
    return PO_ERR_RANGE;
  if (rate == 23438u) {
    result = count;
  } else {
    uint32_t denominator = 2u * rate;
    size_t whole = count / denominator;
    uint64_t tail = ((uint64_t)(count % denominator) * 46875u + denominator - 1u) / denominator;
    if (whole > (SIZE_MAX - (size_t)tail) / 46875u)
      return PO_ERR_RANGE;
    result = whole * 46875u + (size_t)tail;
  }
  if ((uint64_t)result > INT64_MAX)
    return PO_ERR_RANGE;
  *out_count = result;
  return PO_OK;
}

po_status_t po33_samples_encode_f32(const float *pcm, size_t count, uint32_t rate, uint8_t *out,
                                    size_t capacity, size_t *written) {
  po_resample_kernel_t kernel;
  po_status_t status;
  size_t required;
  double input_rate = rate == 23438u ? 23437.5 : (double)rate;
  if (!written)
    return PO_ERR_INVALID_ARG;
  *written = 0;
  if (!pcm && count)
    return PO_ERR_INVALID_ARG;
  status = po33_sample_count_for_pcm(count, rate, &required);
  if (status != PO_OK)
    return status;
  if (!out && required)
    return PO_ERR_INVALID_ARG;
  if (capacity < required)
    return PO_ERR_BUFFER_TOO_SMALL;
  for (size_t i = 0; i < count; ++i)
    if (!po_math_isfinite(pcm[i]) || pcm[i] < -1.0f || pcm[i] > 1.0f)
      return PO_ERR_RANGE;
  if (!required)
    return PO_OK;
  po_resample_init(&kernel, input_rate, 23437.5);
  for (size_t i = 0; i < required; ++i) {
    double sample = po_resample_f32(&kernel, pcm, count, input_rate, 23437.5, (int64_t)i) * 32768.0;
    if (sample > 32124.0)
      sample = 32124.0;
    if (sample < -32124.0)
      sample = -32124.0;
    int16_t rounded = (int16_t)(sample < 0.0 ? sample - 0.5 : sample + 0.5);
    out[i] = po33_sample_encode(rounded);
  }
  *written = required;
  return PO_OK;
}
