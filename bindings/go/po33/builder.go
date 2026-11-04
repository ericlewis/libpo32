package po33

/*
#include "po33.h"
#include <stdlib.h>
*/
import "C"
import (
	"runtime"
	"unsafe"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

// Builder constructs a frame using C-owned state and storage. It must not be
// copied after first use. Calls, including Close, must be externally
// synchronized. Close releases memory promptly; a finalizer is a fallback.
type Builder struct{ native *C.po33_builder_t }

// NewBuilder allocates capacity bytes and writes the preamble and BEGIN.
// Capacity must be at least PreambleBytes+5; END requires another 5 bytes.
func NewBuilder(capacity int) (*Builder, error) {
	if capacity < 0 {
		return nil, ErrInvalidArg
	}
	if capacity < PreambleBytes+5 {
		return nil, ErrBufferTooSmall
	}
	native := (*C.po33_builder_t)(C.calloc(1, C.size_t(C.sizeof_po33_builder_t)))
	if native == nil {
		return nil, ErrInvalidArg
	}
	buffer := (*C.uint8_t)(C.malloc(C.size_t(capacity)))
	if buffer == nil {
		C.free(unsafe.Pointer(native))
		return nil, ErrInvalidArg
	}
	if err := Status(C.po33_builder_init(native, buffer, C.size_t(capacity))).Err(); err != nil {
		C.free(unsafe.Pointer(buffer))
		C.free(unsafe.Pointer(native))
		return nil, err
	}
	b := &Builder{native: native}
	runtime.SetFinalizer(b, (*Builder).Close)
	return b, nil
}

// Close releases C-owned memory. It is safe for nil or already-closed builders.
func (b *Builder) Close() {
	if b == nil {
		return
	}
	runtime.SetFinalizer(b, nil)
	if b.native != nil {
		C.free(unsafe.Pointer(b.native.buffer))
		C.free(unsafe.Pointer(b.native))
		b.native = nil
	}
	runtime.KeepAlive(b)
}

// Reset rewrites the preamble and BEGIN in the existing allocation.
func (b *Builder) Reset() error {
	if b == nil || b.native == nil {
		return ErrInvalidArg
	}
	defer runtime.KeepAlive(b)
	return Status(C.po33_builder_init(b.native, b.native.buffer, b.native.capacity)).Err()
}

// AppendRawPacket appends a raw payload, preserving unknown tags. BEGIN and END
// are reserved. The returned offset includes the preamble. Errors leave the
// builder unchanged.
func (b *Builder) AppendRawPacket(tag uint16, payload []byte) (int, error) {
	if b == nil || b.native == nil {
		return 0, ErrInvalidArg
	}
	defer runtime.KeepAlive(b)
	var offset C.size_t
	if err := Status(C.po33_builder_append_packet(b.native, C.uint16_t(tag), bytePointer(payload), C.size_t(len(payload)), &offset)).Err(); err != nil {
		return 0, err
	}
	return int(offset), nil
}

// Append appends a packet's tag and declared payload. Offset and trailer are
// recomputed for this frame; the caller's packet is unchanged.
func (b *Builder) Append(packet *libpo.Packet) error {
	if packet == nil || packet.PayloadLen < 0 || packet.PayloadLen > MaxPayload {
		return ErrInvalidArg
	}
	_, err := b.AppendRawPacket(packet.TagCode, packet.Payload[:packet.PayloadLen])
	return err
}

// Finish appends END and returns a Go-owned copy of the complete frame.
// Repeated calls succeed without appending another END. Reset and Close do not
// affect previously returned bytes.
func (b *Builder) Finish() ([]byte, error) {
	if b == nil || b.native == nil {
		return nil, ErrInvalidArg
	}
	defer runtime.KeepAlive(b)
	var length C.size_t
	if err := Status(C.po33_builder_finish(b.native, &length)).Err(); err != nil {
		return nil, err
	}
	out := make([]byte, int(length))
	copy(out, unsafe.Slice((*byte)(unsafe.Pointer(b.native.buffer)), len(out)))
	return out, nil
}
