package po33

/*
#include "po33.h"
po_status_t po33_go_frame_parse(const uint8_t *frame, size_t frame_len,
                               uintptr_t handle, po33_decode_result_t *result);
*/
import "C"
import (
	"runtime/cgo"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

//export po33GoPacketCallback
func po33GoPacketCallback(packet *C.po_packet_t, handle C.uintptr_t) C.int {
	fn := cgo.Handle(handle).Value().(func(*libpo.Packet) bool)
	value := libpo.Packet{
		TagCode: uint16(packet.tag_code), Offset: int(packet.offset), PayloadLen: int(packet.payload_len),
		Trailer: libpo.PacketTrailer{Lo: uint8(packet.trailer.lo), Hi: uint8(packet.trailer.hi), State: uint16(packet.trailer.state), MatchesState: packet.trailer.matches_state != 0},
	}
	for i := range value.Payload {
		value.Payload[i] = byte(packet.payload[i])
	}
	if fn(&value) {
		return 0
	}
	return 1
}

// FrameParse validates a frame and calls fn for each validated packet,
// including BEGIN and END. Return true to continue or false to stop. A nil fn
// validates without callbacks. Packets are independent Go-owned copies that
// may be retained or changed. Progress is returned even on a later error;
// only Done establishes completion. Bytes after END are ignored.
func FrameParse(frame []byte, fn func(*libpo.Packet) bool) (DecodeResult, error) {
	var handle cgo.Handle
	if fn != nil {
		handle = cgo.NewHandle(fn)
		defer handle.Delete()
	}
	var result C.po33_decode_result_t
	status := C.po33_go_frame_parse(bytePointer(frame), C.size_t(len(frame)), C.uintptr_t(handle), &result)
	return DecodeResult{
		PacketCount: int(result.packet_count), FrameBytes: int(result.frame_bytes),
		Done: result.done != 0, Stopped: result.stopped != 0, FinalState: uint16(result.final_state),
	}, Status(status).Err()
}
