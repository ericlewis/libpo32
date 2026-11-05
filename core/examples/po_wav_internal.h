#ifndef PO_WAV_INTERNAL_H
#define PO_WAV_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Resolve uncompressed WAV subtypes without accepting a partial GUID. */
static uint16_t po_wav_encoding(const uint8_t *fmt, size_t size) {
  static const uint8_t pcm_guid[16] = {1,    0, 0, 0,    0, 0,    0x10, 0,
                                       0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
  static const uint8_t float_guid[16] = {3,    0, 0, 0,    0, 0,    0x10, 0,
                                         0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
  uint16_t format, extension, valid_bits, bits;
  if (size < 16)
    return 0;
  format = (uint16_t)(fmt[0] | ((uint16_t)fmt[1] << 8));
  if (format != 0xfffe)
    return format == 1 || format == 3 ? format : 0;
  if (size < 40)
    return 0;
  bits = (uint16_t)(fmt[14] | ((uint16_t)fmt[15] << 8));
  extension = (uint16_t)(fmt[16] | ((uint16_t)fmt[17] << 8));
  valid_bits = (uint16_t)(fmt[18] | ((uint16_t)fmt[19] << 8));
  if (extension < 22 || extension > size - 18 || valid_bits == 0 || valid_bits > bits)
    return 0;
  if (!memcmp(fmt + 24, pcm_guid, sizeof(pcm_guid)))
    return 1;
  if (!memcmp(fmt + 24, float_guid, sizeof(float_guid)) && valid_bits == 32)
    return 3;
  return 0;
}
#endif
