package po32

/*
#include "po32.h"
#include <stdlib.h>

// Implemented in trampoline.c (links against _cgo_export.h).
po32_status_t frame_parse_with_trampoline(const uint8_t *frame, size_t frame_len, void *user,
                                          po32_final_tail_t *out_tail);
*/
import "C"
import (
	"sync"
	"unsafe"
)

var (
	cbMu     sync.Mutex
	cbMap    = make(map[uintptr]func(*Packet) bool)
	cbNextID uintptr
)

func registerCallback(fn func(*Packet) bool) uintptr {
	cbMu.Lock()
	cbNextID++
	id := cbNextID
	cbMap[id] = fn
	cbMu.Unlock()
	return id
}

func unregisterCallback(id uintptr) {
	cbMu.Lock()
	delete(cbMap, id)
	cbMu.Unlock()
}

func lookupCallback(id uintptr) (func(*Packet) bool, bool) {
	cbMu.Lock()
	fn, ok := cbMap[id]
	cbMu.Unlock()
	return fn, ok
}

//export goCallbackBridge
func goCallbackBridge(packet *C.po32_packet_t, user unsafe.Pointer) C.int {
	id := uintptr(*(*C.uintptr_t)(user))
	fn, ok := lookupCallback(id)
	if !ok {
		return 1
	}
	p := packetFromC(packet)
	if fn(&p) {
		return 0 // continue
	}
	return 1 // stop
}

// FrameParse walks the packets in a frame, calling fn for each.
// Return true from fn to continue, false to stop early.
func FrameParse(frame []byte, fn func(*Packet) bool) (FinalTail, error) {
	id := registerCallback(fn)
	defer unregisterCallback(id)

	// Store the handle in C-allocated memory to satisfy cgo pointer rules.
	handle := (*C.uintptr_t)(C.malloc(C.size_t(unsafe.Sizeof(id))))
	*handle = C.uintptr_t(id)
	defer C.free(unsafe.Pointer(handle))

	var tail C.po32_final_tail_t
	s := C.frame_parse_with_trampoline(
		(*C.uint8_t)(&frame[0]),
		C.size_t(len(frame)),
		unsafe.Pointer(handle),
		&tail,
	)
	if err := statusToError(int(s)); err != nil {
		return FinalTail{}, err
	}
	return finalTailFromC(&tail), nil
}
