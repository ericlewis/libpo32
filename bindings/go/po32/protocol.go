package po32

/*
#include "po32.h"
*/
import "C"
import "unsafe"

// PreambleData returns a copy of the 128-byte frame preamble.
func PreambleData() []byte {
	p := C.po32_preamble_bytes()
	out := make([]byte, PreambleByteCount)
	copy(out, (*[PreambleByteCount]byte)(unsafe.Pointer(p))[:])
	return out
}

// TagName returns a human-readable name for a packet tag code.
func TagName(tagCode uint16) string {
	return C.GoString(C.po32_tag_name(C.uint16_t(tagCode)))
}

// CRCMixState updates CRC state with a mix byte.
func CRCMixState(state uint16, x uint8) uint16 {
	return uint16(C.po32_crc_mix_state(C.uint16_t(state), C.uint8_t(x)))
}

// CRCUpdate updates CRC state with a raw byte.
func CRCUpdate(state uint16, rawByte uint8) uint16 {
	return uint16(C.po32_crc_update(C.uint16_t(state), C.uint8_t(rawByte)))
}

// WhitenByte encodes a raw byte using the current CRC state.
func WhitenByte(state uint16, rawByte uint8) uint8 {
	return uint8(C.po32_whiten_byte(C.uint16_t(state), C.uint8_t(rawByte)))
}

// UnwhitenByte decodes a whitened byte using the current CRC state.
func UnwhitenByte(state uint16, codedByte uint8) uint8 {
	return uint8(C.po32_unwhiten_byte(C.uint16_t(state), C.uint8_t(codedByte)))
}
