package po32

/*
#include "po32.h"
#include <stdlib.h>

po32_status_t go_po32_demodulator_push(po32_demodulator_t *d, const float *samples,
                                       size_t count, void *user);
void go_po32_demodulator_flush(po32_demodulator_t *d, void *user);
*/
import "C"
import (
	"math"
	"runtime"
	"unsafe"
)

// Demodulator decodes successive chunks of mono float32 transfer audio.
// Its state is C-owned. Call Close when finished; a finalizer is a fallback.
// A Demodulator must not be copied or used concurrently without external
// synchronization. Callbacks may query progress or Close the demodulator,
// but must not recursively Push, Flush, or Desync it.
type Demodulator struct {
	cDemod       *C.po32_demodulator_t
	active       bool
	closePending bool
}

// NewDemodulator creates a decoder at a positive, finite sample rate.
func NewDemodulator(sampleRate float32) (*Demodulator, error) {
	if !(sampleRate > 0) || math.IsInf(float64(sampleRate), 0) {
		return nil, ErrInvalidArg
	}
	p := (*C.po32_demodulator_t)(C.malloc(C.size_t(C.sizeof_po32_demodulator_t)))
	if p == nil {
		return nil, ErrInvalidArg
	}
	C.po32_demodulator_init(p, C.float(sampleRate))
	d := &Demodulator{cDemod: p}
	runtime.SetFinalizer(d, (*Demodulator).Close)
	return d, nil
}

// Close releases the decoder. It is safe to call multiple times or from its
// callback; callback-initiated Close stops decoding and defers the release
// until the current C call has returned.
func (d *Demodulator) Close() {
	if d == nil {
		return
	}
	runtime.SetFinalizer(d, nil)
	if d.active {
		d.closePending = true
		return
	}
	if d.cDemod != nil {
		C.free(unsafe.Pointer(d.cDemod))
		d.cDemod = nil
	}
}

func (d *Demodulator) endCall() {
	d.active = false
	if d.closePending {
		d.Close()
	}
}

func (d *Demodulator) callbackHandle(fn func(*Packet) bool) (unsafe.Pointer, func(), error) {
	if fn == nil {
		return newCallbackHandle(nil)
	}
	return newCallbackHandle(func(p *Packet) bool {
		keepGoing := fn(p)
		return keepGoing && !d.closePending
	})
}

// Push decodes one chunk. Packets passed to fn are Go-owned copies and remain
// valid after the callback. A nil fn discards packets while tracking progress.
// Return false to stop permanently; the stopping packet is included in
// PacketCount, the rest of the chunk is discarded, and Done remains false.
// Empty chunks and pushes after completion or a callback stop are no-ops.
func (d *Demodulator) Push(samples []float32, fn func(*Packet) bool) error {
	if d == nil || d.cDemod == nil || d.active || d.closePending {
		return ErrInvalidArg
	}
	defer runtime.KeepAlive(d)
	if len(samples) == 0 || d.Done() || d.Stopped() {
		return nil
	}
	handle, cleanup, err := d.callbackHandle(fn)
	if err != nil {
		return err
	}
	defer cleanup()
	d.active = true
	defer d.endCall()
	return statusToError(int(C.go_po32_demodulator_push(d.cDemod,
		(*C.float)(unsafe.Pointer(&samples[0])), C.size_t(len(samples)), handle)))
}

// Flush resolves a pending final symbol after the last audio chunk. It is
// needed for captures ending at the exact rendered sample count. Flush does
// not guarantee rejection of every truncated capture. fn follows Push's
// callback contract; a stopped or completed decoder is unchanged.
func (d *Demodulator) Flush(fn func(*Packet) bool) error {
	if d == nil || d.cDemod == nil || d.active || d.closePending {
		return ErrInvalidArg
	}
	defer runtime.KeepAlive(d)
	if d.Done() || d.Stopped() {
		return nil
	}
	handle, cleanup, err := d.callbackHandle(fn)
	if err != nil {
		return err
	}
	defer cleanup()
	d.active = true
	defer d.endCall()
	C.go_po32_demodulator_flush(d.cDemod, handle)
	return nil
}

// Desync discards current sync and any partial packet. It preserves Done,
// Stopped, and PacketCount; the count resets on the next sync acquisition.
// Create a new decoder to restart after completion or a callback stop.
func (d *Demodulator) Desync() {
	if d == nil || d.cDemod == nil || d.active || d.closePending {
		return
	}
	defer runtime.KeepAlive(d)
	C.po32_demodulator_desync(d.cDemod)
}

// Done reports whether a valid final tail has been decoded.
func (d *Demodulator) Done() bool {
	if d == nil || d.cDemod == nil || d.closePending {
		return false
	}
	defer runtime.KeepAlive(d)
	return C.po32_demodulator_done(d.cDemod) != 0
}

// Stopped reports whether a callback permanently stopped decoding.
func (d *Demodulator) Stopped() bool {
	if d == nil || d.cDemod == nil || d.closePending {
		return false
	}
	defer runtime.KeepAlive(d)
	return C.po32_demodulator_stopped(d.cDemod) != 0
}

// PacketCount counts committed packets since the most recent sync
// acquisition, including the packet whose callback returned false.
func (d *Demodulator) PacketCount() int {
	if d == nil || d.cDemod == nil || d.closePending {
		return 0
	}
	defer runtime.KeepAlive(d)
	return int(C.po32_demodulator_packet_count(d.cDemod))
}

// Tail returns a copy of the final tail and true only after successful
// completion. An unfinished, stopped, or closed decoder has no final tail.
func (d *Demodulator) Tail() (FinalTail, bool) {
	if !d.Done() {
		return FinalTail{}, false
	}
	defer runtime.KeepAlive(d)
	return finalTailFromC(C.po32_demodulator_tail(d.cDemod)), true
}
