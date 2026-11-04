package po32

/*
#include "po32.h"
*/
import "C"
import "unsafe"

// decodeInitialFrameCapacity is the starting size of DecodeF32's
// auto-growing reconstructed-frame buffer (64 KiB).
const decodeInitialFrameCapacity = 1 << 16

// DecodeF32 decodes mono float32 audio back into a transfer frame.
//
// The reconstructed-frame buffer starts at 64 KiB and grows automatically
// until the frame fits, so frames of any size the audio can carry decode
// without configuration. Use DecodeF32WithCapacity to bound the buffer
// yourself.
func DecodeF32(samples []float32, sampleRate float32) (DecodeResult, []byte, error) {
	capacity := decodeInitialFrameCapacity
	// Every frame byte spans many audio samples, so the sample count is a
	// generous upper bound on the size of any frame the audio can encode.
	maxCapacity := max(capacity, len(samples))
	for {
		result, frame, err := DecodeF32WithCapacity(samples, sampleRate, capacity)
		if err != ErrBufferTooSmall || capacity >= maxCapacity {
			return result, frame, err
		}
		if capacity <= maxCapacity/2 {
			capacity *= 2
		} else {
			capacity = maxCapacity
		}
	}
}

// DecodeF32WithCapacity decodes like DecodeF32 with a caller-chosen
// capacity for the reconstructed-frame buffer, mirroring the
// out_frame/out_capacity pair of the C po32_decode_f32. A frame larger
// than frameCapacity bytes fails with ErrBufferTooSmall instead of
// growing.
func DecodeF32WithCapacity(samples []float32, sampleRate float32, frameCapacity int) (DecodeResult, []byte, error) {
	if len(samples) == 0 || frameCapacity <= 0 {
		return DecodeResult{}, nil, ErrInvalidArg
	}
	frameBuf := make([]byte, frameCapacity)
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
