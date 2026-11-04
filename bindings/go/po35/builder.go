package po35

/*
#include "po35.h"
#include <stdlib.h>
*/
import "C"
import (
	libpo "github.com/ericlewis/libpo32/bindings/go"
	"runtime"
	"sync"
	"unsafe"
)

// Builder builds a counter-preamble PO-35/PO-137 transfer frame. Close releases
// its C-owned storage; a finalizer is only a fallback. Copies share ownership.
type Builder struct{ state *builderState }
type builderState struct {
	mu     sync.Mutex
	c      C.po35_builder_t
	buffer *C.uint8_t
}

// NewBuilder allocates capacity bytes. At least PreambleByteCount are required;
// packet and final-tail storage must also fit within this fixed capacity.
func NewBuilder(capacity int) (*Builder, error) {
	n, err := sizeArg(capacity)
	if err != nil {
		return nil, err
	}
	if capacity < PreambleByteCount {
		return nil, libpo.ErrBufferTooSmall
	}
	buffer := (*C.uint8_t)(C.malloc(n))
	if buffer == nil {
		return nil, libpo.ErrBufferTooSmall
	}
	state := &builderState{buffer: buffer}
	if err = libpo.Status(C.po35_builder_init(&state.c, buffer, n)).Err(); err != nil {
		C.free(unsafe.Pointer(buffer))
		return nil, err
	}
	runtime.SetFinalizer(state, (*builderState).close)
	return &Builder{state: state}, nil
}
func (s *builderState) close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.buffer != nil {
		C.free(unsafe.Pointer(s.buffer))
		s.buffer = nil
		s.c = C.po35_builder_t{}
	}
}

// Close releases retained memory. It is safe to call repeatedly or on nil.
func (b *Builder) Close() {
	if b != nil && b.state != nil {
		b.state.close()
		runtime.SetFinalizer(b.state, nil)
	}
}

// Reset discards the frame and rewrites the counter preamble.
func (b *Builder) Reset() error {
	if b == nil || b.state == nil {
		return libpo.ErrInvalidArg
	}
	s := b.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.buffer == nil {
		return libpo.ErrInvalidArg
	}
	return libpo.Status(C.po35_builder_init(&s.c, s.buffer, s.c.capacity)).Err()
}

// AppendRawPacket appends up to 255 payload bytes and returns its frame offset.
// An unsuccessful append leaves the frame unchanged. Tag 0x71c3 is reserved.
func (b *Builder) AppendRawPacket(tag uint16, payload []byte) (int, error) {
	if b == nil || b.state == nil {
		return 0, libpo.ErrInvalidArg
	}
	s := b.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.buffer == nil {
		return 0, libpo.ErrInvalidArg
	}
	var offset C.size_t
	status := C.po35_builder_append_packet(&s.c, C.uint16_t(tag), bytePtr(payload), C.size_t(len(payload)), &offset)
	if err := libpo.Status(status).Err(); err != nil {
		return 0, err
	}
	return int(offset), nil
}

// Append appends a packet's tag and payload, recomputing its wire trailer.
func (b *Builder) Append(p *libpo.Packet) error {
	if p == nil || p.PayloadLen < 0 || p.PayloadLen > libpo.MaxPayload {
		return libpo.ErrInvalidArg
	}
	_, err := b.AppendRawPacket(p.TagCode, p.Payload[:p.PayloadLen])
	return err
}

// Finish appends the final tail idempotently and returns an owned frame copy.
func (b *Builder) Finish() ([]byte, error) {
	if b == nil || b.state == nil {
		return nil, libpo.ErrInvalidArg
	}
	s := b.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.buffer == nil {
		return nil, libpo.ErrInvalidArg
	}
	var n C.size_t
	if err := libpo.Status(C.po35_builder_finish(&s.c, &n)).Err(); err != nil {
		return nil, err
	}
	length, err := sizeResult(n)
	if err != nil {
		return nil, err
	}
	return append([]byte(nil), unsafe.Slice((*byte)(unsafe.Pointer(s.buffer)), length)...), nil
}
