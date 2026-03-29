package po32

/*
#include "po32.h"
*/
import "C"
import "unsafe"

// DecodeF32 decodes mono float32 audio back into a transfer frame.
func DecodeF32(samples []float32, sampleRate float32) (DecodeResult, []byte, error) {
	frameBuf := make([]byte, 65536)
	var result C.po32_decode_result_t
	var frameLen C.size_t

	s := C.po32_decode_f32(
		(*C.float)(unsafe.Pointer(&samples[0])),
		C.size_t(len(samples)),
		C.float(sampleRate),
		&result,
		(*C.uint8_t)(&frameBuf[0]),
		C.size_t(len(frameBuf)),
		&frameLen,
	)
	if err := statusToError(int(s)); err != nil {
		return DecodeResult{}, nil, err
	}
	return decodeResultFromC(&result), frameBuf[:int(frameLen)], nil
}
