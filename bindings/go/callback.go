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
	if packet == nil || user == nil {
		return 1
	}
	id := uintptr(*(*C.uintptr_t)(user))
	fn, ok := lookupCallback(id)
	if !ok || fn == nil {
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
//
// When fn stops parsing early the C core never reaches the final tail and
// the returned FinalTail is the zero value; use FrameParseTail to tell
// that apart from a decoded tail.
func FrameParse(frame []byte, fn func(*Packet) bool) (FinalTail, error) {
	tail, _, err := FrameParseTail(frame, fn)
	return tail, err
}

// FrameParseTail is like FrameParse but additionally reports whether the
// final tail was decoded. The bool result is false when fn stopped
// parsing early: the C core then returns success without ever reaching
// the tail, so the FinalTail is the zero value rather than a decoded
// terminator.
func FrameParseTail(frame []byte, fn func(*Packet) bool) (FinalTail, bool, error) {
	if len(frame) == 0 || fn == nil {
		return FinalTail{}, false, ErrInvalidArg
	}
	stopped := false
	id := registerCallback(func(p *Packet) bool {
		if fn(p) {
			return true
		}
		stopped = true
		return false
	})
	defer unregisterCallback(id)

	// Store the handle in C-allocated memory to satisfy cgo pointer rules.
	handle := (*C.uintptr_t)(C.malloc(C.size_t(unsafe.Sizeof(id))))
	if handle == nil {
		return FinalTail{}, false, ErrInvalidArg
	}
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
		return FinalTail{}, false, err
	}
	if stopped {
		return FinalTail{}, false, nil
	}
	return finalTailFromC(&tail), true, nil
}
