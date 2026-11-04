package stereo

/*
#include "po_stereo.h"
#include <stdlib.h>
*/
import "C"
import (
	"runtime"
	"unsafe"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

// Modulator renders raw wire bytes using C-owned state and an immutable copy
// of the input. Copies share ownership and Close invalidates every copy.
// Calls, including Close, must be externally synchronized.
type Modulator struct{ state *modulatorState }

type modulatorState struct{ native *C.po_stereo_modulator_t }

// NewModulator copies wire and initializes a transmitter. Wire must not be
// empty; packet grammar is not checked. TX supports 44100, 48000 and 96000 Hz.
// Close releases retained memory promptly; a finalizer is a fallback.
func NewModulator(wire []byte, rate uint32) (*Modulator, error) {
	if len(wire) == 0 {
		return nil, libpo.ErrInvalidArg
	}
	if rate != 44100 && rate != 48000 && rate != 96000 {
		return nil, libpo.ErrRange
	}
	native := (*C.po_stereo_modulator_t)(C.calloc(1, C.size_t(C.sizeof_po_stereo_modulator_t)))
	if native == nil {
		return nil, libpo.ErrInvalidArg
	}
	data := (*C.uint8_t)(C.CBytes(wire))
	if data == nil {
		C.free(unsafe.Pointer(native))
		return nil, libpo.ErrInvalidArg
	}
	if err := libpo.Status(C.po_stereo_modulator_init(native, data, C.size_t(len(wire)), C.uint32_t(rate))).Err(); err != nil {
		C.free(unsafe.Pointer(data))
		C.free(unsafe.Pointer(native))
		return nil, err
	}
	state := &modulatorState{native: native}
	runtime.SetFinalizer(state, (*modulatorState).close)
	return &Modulator{state: state}, nil
}

func (s *modulatorState) close() {
	if s.native != nil {
		C.free(unsafe.Pointer(s.native.wire))
		C.free(unsafe.Pointer(s.native))
		s.native = nil
	}
	runtime.KeepAlive(s)
}

// Close releases retained state and wire memory. It is idempotent and nil-safe.
func (m *Modulator) Close() {
	if m == nil || m.state == nil {
		return
	}
	runtime.SetFinalizer(m.state, nil)
	m.state.close()
}

// FramesRemaining reports stereo frames, not individual float values. Nil,
// uninitialized and closed modulators report zero.
func (m *Modulator) FramesRemaining() int {
	if m == nil || m.state == nil || m.state.native == nil {
		return 0
	}
	state := m.state
	defer runtime.KeepAlive(state)
	return int(C.po_stereo_modulator_remaining(state.native))
}

// Done reports whether all frames have been rendered. It is false for nil,
// uninitialized and closed modulators.
func (m *Modulator) Done() bool {
	if m == nil || m.state == nil || m.state.native == nil {
		return false
	}
	state := m.state
	defer runtime.KeepAlive(state)
	return C.po_stereo_modulator_done(state.native) != 0
}

// Reset rewinds the transmitter while retaining its original wire copy.
func (m *Modulator) Reset() error {
	if m == nil || m.state == nil || m.state.native == nil {
		return libpo.ErrInvalidArg
	}
	state := m.state
	defer runtime.KeepAlive(state)
	return libpo.Status(C.po_stereo_modulator_reset(state.native)).Err()
}

// RenderF32 returns up to frameCapacity stereo frames as interleaved L/R
// float32 values. Capacity zero is a successful no-op. Chunk boundaries do not
// change the waveform, and returned slices remain valid after reset or close.
func (m *Modulator) RenderF32(frameCapacity int) ([]float32, error) {
	if m == nil || m.state == nil || m.state.native == nil || frameCapacity < 0 {
		return nil, libpo.ErrInvalidArg
	}
	if uint64(frameCapacity) > uint64(^uint(0)>>1)/8 {
		return nil, libpo.ErrRange
	}
	state := m.state
	defer runtime.KeepAlive(state)
	frames := min(frameCapacity, int(C.po_stereo_modulator_remaining(state.native)))
	out := make([]float32, frames*2)
	var written C.size_t
	status := C.po_stereo_modulator_render_f32(state.native, (*C.float)(unsafe.Pointer(unsafe.SliceData(out))), C.size_t(frames), &written)
	if err := libpo.Status(status).Err(); err != nil {
		return nil, err
	}
	return out[:int(written)*2], nil
}

// RenderF32 renders the complete raw wire transfer at rate Hz. The output
// includes 0.25 seconds of silence on each side and a final phase hold.
func RenderF32(wire []byte, rate uint32) ([]float32, error) {
	m, err := NewModulator(wire, rate)
	if err != nil {
		return nil, err
	}
	defer m.Close()
	return m.RenderF32(m.FramesRemaining())
}
