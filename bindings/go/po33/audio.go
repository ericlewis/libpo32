package po33

/*
#include "po33.h"
*/
import "C"
import "unsafe"

// DecodeSample expands a signed-order mu-law byte into linear PCM16. Sound
// headers and padding must be excluded by the caller.
func DecodeSample(encoded byte) int16 { return int16(C.po33_sample_decode(C.uint8_t(encoded))) }

// EncodeSample returns the nearest signed-order mu-law level, saturating at
// +/-32124. Ties select the smaller magnitude; zero has canonical encoding 0.
func EncodeSample(sample int16) byte { return byte(C.po33_sample_encode(C.int16_t(sample))) }

// SampleCountForPCM sizes conversion to the nominal 23437.5 Hz sample clock.
// Rates must be 8000..192000 Hz; 23438 is an exact native-rate alias. Other
// rates round duration up to a whole output sample. No device limit is imposed.
func SampleCountForPCM(count int, rate uint32) (int, error) {
	if count < 0 {
		return 0, ErrRange
	}
	var required C.size_t
	if err := Status(C.po33_sample_count_for_pcm(C.size_t(count), C.uint32_t(rate), &required)).Err(); err != nil {
		return 0, err
	}
	if uint64(required) > uint64(^uint(0)>>1) {
		return 0, ErrRange
	}
	return int(required), nil
}

// EncodeSamples converts finite normalized mono PCM in [-1,1] into sample
// bytes, resampling nonnative rates with the C core's bandlimited filter.
// Empty input is permitted. The returned bytes contain no headers or padding.
func EncodeSamples(pcm []float32, rate uint32) ([]byte, error) {
	required, err := SampleCountForPCM(len(pcm), rate)
	if err != nil {
		return nil, err
	}
	out := make([]byte, required)
	var input *C.float
	if len(pcm) != 0 {
		input = (*C.float)(unsafe.Pointer(&pcm[0]))
	}
	var written C.size_t
	if err := Status(C.po33_samples_encode_f32(input, C.size_t(len(pcm)), C.uint32_t(rate), bytePointer(out), C.size_t(len(out)), &written)).Err(); err != nil {
		return nil, err
	}
	return out[:int(written)], nil
}

// PackSymbols converts interleaved left/right absolute phase quadrants (0..3)
// into wire bytes. The first pair is a reference; parity (0 or 1) skips that
// many differential symbols. An odd final nibble is omitted. An unpaired
// channel value or invalid parity returns ErrInvalidArg.
func PackSymbols(interleaved []byte, parity uint) ([]byte, error) {
	if len(interleaved)%2 != 0 || parity > 1 {
		return nil, ErrInvalidArg
	}
	symbols := len(interleaved) / 2
	required := 0
	if symbols > 1+int(parity) {
		required = (symbols - 1 - int(parity)) / 2
	}
	out := make([]byte, required)
	var written C.size_t
	if err := Status(C.po33_pack_symbols(bytePointer(interleaved), C.size_t(symbols), C.uint(parity), bytePointer(out), C.size_t(len(out)), &written)).Err(); err != nil {
		return nil, err
	}
	return out[:int(written)], nil
}

func bytePointer(data []byte) *C.uint8_t {
	if len(data) == 0 {
		return nil
	}
	return (*C.uint8_t)(&data[0])
}
