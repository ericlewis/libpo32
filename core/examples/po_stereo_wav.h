#ifndef PO_STEREO_WAV_H
#define PO_STEREO_WAV_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Hosted file wrappers around the freestanding stereo modulator. Validate one
 * complete model-family frame before exclusive output creation. On I/O error,
 * remove only the newly created partial file. Return 1 on success, 0 on error.
 * Hardware reception is not implied by successful generation or roundtrip. */
int po33_write_transfer_wav(const char *path, const uint8_t *frame, size_t frame_len,
                            uint32_t rate);
int po35_write_transfer_wav(const char *path, const uint8_t *frame, size_t frame_len,
                            uint32_t rate);
#ifdef __cplusplus
}
#endif
#endif
