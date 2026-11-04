#ifndef PO35_PO35_ANALYSIS_H
#define PO35_PO35_ANALYSIS_H

#include "po_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental LPC analysis compatible with reconstruction synth model 1.
 * This is an approximation, not the original PO-35/PO-137 speech encoder. */
#define PO35_ANALYSIS_MODEL_VERSION 1
#define PO35_ANALYSIS_RECORD_BYTES  44u
#define PO35_ANALYSIS_MAX_RECORDS   65535u

/* Caller-owned scratch storage; members are private implementation details.
 * Both coefficient banks and frame energy use the same bandlimited 24 kHz
 * analysis clock, independent of input rate. No allocation or global state.
 * Input, output, output length and workspace must be disjoint. */
typedef struct po35_analysis_workspace {
  double native[1672];
  double high[1024];
  double low[512];
  double filtered[1024];
  double pitch_correlation[800];
  double window[1024];
  double autocorrelation[31];
  double lpc[31];
  double temporary[31];
  double decimator[49];
  double resample_cutoff;
  double resample_radius;
  double resample_sin_step;
  double resample_cos_step;
  double resample_window_sin_step;
  double resample_window_cos_step;
} po35_analysis_workspace_t;

/* Input rate is 8000..192000 Hz; fps is 20, 40, 60 or 80. The duration is
 * rounded up to whole records (zero-padded at the end), limited to 65535.
 * Returns RANGE for invalid rates/fps or excessive duration. *record_bytes
 * is set to zero on every error. Empty input requires zero output bytes. */
po_status_t po35_analysis_size_for_pcm(size_t count, uint32_t rate, uint32_t fps,
                                       size_t *record_bytes);

/* Analyze normalized, finite mono PCM in [-1,1] into packed 44-byte records.
 * Both low (10 coefficients) and high (30 coefficients) banks are emitted.
 * Shared gain is calibrated to the louder bank under reconstruction model 1;
 * it does not normalize input, and synthesized transient peaks can exceed 1.
 * All argument and sample checks precede output writes. On error the output
 * records are untouched and *record_bytes is zero. Workspace is required even
 * for empty input; pcm/records may be NULL when their required sizes are zero.
 * IEEE-754 binary32 float and binary64 double are required. */
po_status_t po35_analysis_encode_f32(po35_analysis_workspace_t *workspace, const float *pcm,
                                     size_t count, uint32_t rate, uint32_t fps, uint8_t *records,
                                     size_t capacity, size_t *record_bytes);

#ifdef __cplusplus
}
#endif
#endif
