package po35

/*
#include "po35.h"
po_status_t po35_go_frame_parse(const uint8_t *, size_t, uintptr_t, po35_decode_result_t *);
*/
import "C"
import (
	libpo "github.com/ericlewis/libpo32/bindings/go"
	"runtime/cgo"
)

//export po35GoPacketCallback
func po35GoPacketCallback(c *C.po_packet_t, handle C.uintptr_t) C.int {
	fn := cgo.Handle(handle).Value().(func(*libpo.Packet) bool)
	p := &libpo.Packet{TagCode: uint16(c.tag_code), Offset: int(c.offset), PayloadLen: int(c.payload_len), Trailer: libpo.PacketTrailer{Lo: uint8(c.trailer.lo), Hi: uint8(c.trailer.hi), State: uint16(c.trailer.state), MatchesState: c.trailer.matches_state != 0}}
	for i := 0; i < p.PayloadLen; i++ {
		p.Payload[i] = byte(c.payload[i])
	}
	if fn(p) {
		return 0
	}
	return 1
}

// FrameParse validates the counter preamble, packets, and final tail. A nil
// callback performs validation only. A callback returns true to continue or
// false to stop successfully with Stopped set. Every callback receives an owned
// packet copy. The result describes the validated prefix even on failure.
// Trailing bytes after the tail are ignored and excluded from FrameBytes.
func FrameParse(frame []byte, fn func(*libpo.Packet) bool) (DecodeResult, error) {
	var handle cgo.Handle
	if fn != nil {
		handle = cgo.NewHandle(fn)
		defer handle.Delete()
	}
	var c C.po35_decode_result_t
	status := C.po35_go_frame_parse(bytePtr(frame), C.size_t(len(frame)), C.uintptr_t(handle), &c)
	// Both counts are bounded by the input slice length, which fits in int.
	result := DecodeResult{PacketCount: int(c.packet_count), FrameBytes: int(c.frame_bytes), Done: c.done != 0, FinalState: uint16(c.final_state), Stopped: c.stopped != 0}
	return result, libpo.Status(status).Err()
}
