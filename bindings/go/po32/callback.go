package po32

/*
#include "po32.h"
#include <stdlib.h>

// Implemented in trampoline.c (links against _cgo_export.h).
po32_status_t go_po32_frame_parse(const uint8_t *frame, size_t frame_len, void *user,
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

//export goPO32CallbackBridge
func goPO32CallbackBridge(packet *C.po32_packet_t, user unsafe.Pointer) C.int {
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

// newCallbackHandle stores only an integer ID in C memory. The Go callback is
// retained by the registry for the duration of one synchronous C call.
func newCallbackHandle(fn func(*Packet) bool) (unsafe.Pointer, func(), error) {
	if fn == nil {
		return nil, func() {}, nil
	}
	id := registerCallback(fn)
	handle := (*C.uintptr_t)(C.malloc(C.size_t(unsafe.Sizeof(id))))
	if handle == nil {
		unregisterCallback(id)
		return nil, nil, ErrInvalidArg
	}
	*handle = C.uintptr_t(id)
	return unsafe.Pointer(handle), func() {
		C.free(unsafe.Pointer(handle))
		unregisterCallback(id)
	}, nil
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
	handle, cleanup, err := newCallbackHandle(func(p *Packet) bool {
		if fn(p) {
			return true
		}
		stopped = true
		return false
	})
	if err != nil {
		return FinalTail{}, false, err
	}
	defer cleanup()

	var tail C.po32_final_tail_t
	s := C.go_po32_frame_parse(
		(*C.uint8_t)(&frame[0]),
		C.size_t(len(frame)),
		handle,
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
