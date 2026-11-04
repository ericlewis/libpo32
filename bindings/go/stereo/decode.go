package stereo

/*
#include "po_stereo.h"

// Construct the pointer-bearing descriptor on the C stack. Each Go array is
// passed separately and pinned for this synchronous call by cgo.
static po_status_t po_stereo_go_decode_f32(
    const float *input, size_t frames, uint32_t rate,
    po_stereo_channel_order_t order, double *scalars, size_t scalar_count,
    uint8_t *scratch, size_t byte_count, uint8_t *wire, size_t capacity,
    size_t *written) {
  po_stereo_workspace_t workspace = {scalars, scalar_count, scratch, byte_count};
  return po_stereo_decode_f32(input, frames, rate, order, &workspace,
                              wire, capacity, written);
}
*/
import "C"
import (
	"unsafe"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

// ChannelOrder selects the received channel assignment.
type ChannelOrder int

const (
	ChannelNormal ChannelOrder = iota
	ChannelSwapped
)

// Requirements gives element counts for one-shot decoding: float64 scratch
// scalars, byte scratch, and the maximum output wire bytes.
type Requirements struct {
	ScalarCount  int
	ByteCount    int
	WireCapacity int
}

// DecodeRequirements sizes scratch and output for frames stereo frames. RX
// accepts sample rates from 32000 through 192000 Hz. Invalid counts or counts
// that cannot fit Go allocations return an error; zero frames needs no storage.
func DecodeRequirements(frames int, rate uint32) (Requirements, error) {
	if frames < 0 {
		return Requirements{}, libpo.ErrInvalidArg
	}
	var scalars, scratch, wire C.size_t
	status := C.po_stereo_decode_requirements(C.size_t(frames), C.uint32_t(rate), &scalars, &scratch, &wire)
	if err := libpo.Status(status).Err(); err != nil {
		return Requirements{}, err
	}
	maxInt := uint64(^uint(0) >> 1)
	if uint64(scalars) > maxInt/8 || uint64(scratch) > maxInt || uint64(wire) > maxInt {
		return Requirements{}, libpo.ErrRange
	}
	return Requirements{ScalarCount: int(scalars), ByteCount: int(scratch), WireCapacity: int(wire)}, nil
}

// DecodeF32 recovers raw wire bytes from finite interleaved left/right PCM.
// Input is unchanged. Odd input lengths or invalid channel orders return
// ErrInvalidArg. Empty or short input, or a missing counter preamble, returns
// ErrFrame. Returned bytes may extend past the final packet: use the model's
// parser and its FrameBytes result to determine validated frame boundaries.
func DecodeF32(interleaved []float32, rate uint32, order ChannelOrder) ([]byte, error) {
	if len(interleaved)%2 != 0 || (order != ChannelNormal && order != ChannelSwapped) {
		return nil, libpo.ErrInvalidArg
	}
	requirements, err := DecodeRequirements(len(interleaved)/2, rate)
	if err != nil {
		return nil, err
	}
	if len(interleaved) == 0 {
		return nil, libpo.ErrFrame
	}
	scalars := make([]float64, requirements.ScalarCount)
	scratch := make([]byte, requirements.ByteCount)
	wire := make([]byte, requirements.WireCapacity)
	var written C.size_t
	status := C.po_stereo_go_decode_f32(
		(*C.float)(unsafe.Pointer(unsafe.SliceData(interleaved))), C.size_t(len(interleaved)/2), C.uint32_t(rate), C.po_stereo_channel_order_t(order),
		(*C.double)(unsafe.Pointer(unsafe.SliceData(scalars))), C.size_t(len(scalars)),
		(*C.uint8_t)(unsafe.Pointer(unsafe.SliceData(scratch))), C.size_t(len(scratch)),
		(*C.uint8_t)(unsafe.Pointer(unsafe.SliceData(wire))), C.size_t(len(wire)), &written)
	if err := libpo.Status(status).Err(); err != nil {
		return nil, err
	}
	return wire[:int(written)], nil
}
