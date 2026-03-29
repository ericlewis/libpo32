package po32

/*
#include "po32.h"
#include <stdlib.h>
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// Builder constructs a PO-32 transfer frame.
//
// The builder allocates C memory for the frame buffer. Call Close when
// done, or rely on the garbage collector's finalizer as a safety net.
type Builder struct {
	cBuf *C.uint8_t
	cap  int
	cBld C.po32_builder_t
}

// NewBuilder creates a frame builder with the given buffer capacity.
func NewBuilder(capacity int) *Builder {
	cBuf := (*C.uint8_t)(C.malloc(C.size_t(capacity)))
	b := &Builder{cBuf: cBuf, cap: capacity}
	C.po32_builder_init(&b.cBld, cBuf, C.size_t(capacity))
	runtime.SetFinalizer(b, (*Builder).Close)
	return b
}

// Close releases the C-allocated buffer. Safe to call multiple times.
func (b *Builder) Close() {
	if b.cBuf != nil {
		C.free(unsafe.Pointer(b.cBuf))
		b.cBuf = nil
	}
}

// Reset clears the builder and rewrites the preamble.
func (b *Builder) Reset() {
	C.po32_builder_reset(&b.cBld)
}

// AppendRawPacket appends a raw payload with the given tag code.
// Returns the packet's byte offset within the frame.
func (b *Builder) AppendRawPacket(tagCode uint16, payload []byte) (int, error) {
	var offset C.size_t
	if len(payload) == 0 {
		s := C.po32_builder_append_packet(&b.cBld, C.uint16_t(tagCode),
			nil, 0, &offset)
		return int(offset), statusToError(int(s))
	}
	s := C.po32_builder_append_packet(&b.cBld, C.uint16_t(tagCode),
		(*C.uint8_t)(&payload[0]), C.size_t(len(payload)), &offset)
	if err := statusToError(int(s)); err != nil {
		return 0, err
	}
	return int(offset), nil
}

// Append appends an already-encoded Packet to the frame.
func (b *Builder) Append(pkt *Packet) error {
	payload := make([]byte, pkt.PayloadLen)
	copy(payload, pkt.Payload[:pkt.PayloadLen])
	_, err := b.AppendRawPacket(pkt.TagCode, payload)
	return err
}

// Finish emits the final tail and returns the completed frame bytes.
// The returned slice is a Go-owned copy; the builder can be reused or closed.
func (b *Builder) Finish() ([]byte, error) {
	var frameLen C.size_t
	s := C.po32_builder_finish(&b.cBld, &frameLen)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	n := int(frameLen)
	out := make([]byte, n)
	copy(out, unsafe.Slice((*byte)(unsafe.Pointer(b.cBuf)), n))
	return out, nil
}
