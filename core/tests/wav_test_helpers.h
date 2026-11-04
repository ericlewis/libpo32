#ifndef WAV_TEST_HELPERS_H
#define WAV_TEST_HELPERS_H
#include <stdint.h>
static inline uint16_t test_wav_u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
static inline uint32_t test_wav_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
#endif
