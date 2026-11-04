package po32

/*
#include "po32_synth.h"
#include <stdlib.h>
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// SynthVoice renders one drum hit in chunks while preserving its DSP state.
// Its state is C-owned and does not retain the supplied patch parameters.
// Call Close when finished; a finalizer is a fallback. A SynthVoice must not
// be copied or used concurrently without external synchronization.
type SynthVoice struct {
	cVoice *C.po32_synth_voice_t
}

// NewSynthVoice prepares a drum hit. Velocity is MIDI velocity (0-127) and
// duration is seconds. A zero sample rate or a non-positive, non-finite, or
// duration beyond the C core's range produces an inert voice. ErrRange is
// returned for velocity outside 0–127 or a C sample count that cannot be
// represented by Go's int type.
func NewSynthVoice(sampleRate uint32, params *PatchParams, velocity int, duration float32) (*SynthVoice, error) {
	if params == nil {
		return nil, ErrInvalidArg
	}
	if velocity < 0 || velocity > 127 {
		return nil, ErrRange
	}
	p := (*C.po32_synth_voice_t)(C.malloc(C.size_t(C.sizeof_po32_synth_voice_t)))
	if p == nil {
		return nil, ErrInvalidArg
	}
	cp := patchParamsToC(params)
	C.po32_synth_voice_init(p, C.uint32_t(sampleRate), &cp, C.int(velocity), C.float(duration))
	if uint64(C.po32_synth_voice_samples_remaining(p)) > uint64(^uint(0)>>1) {
		C.free(unsafe.Pointer(p))
		return nil, ErrRange
	}
	v := &SynthVoice{cVoice: p}
	runtime.SetFinalizer(v, (*SynthVoice).Close)
	return v, nil
}

// NewVoice prepares a streaming drum hit at this synthesizer's sample rate.
func (s *Synth) NewVoice(params *PatchParams, velocity int, duration float32) (*SynthVoice, error) {
	if s == nil {
		return nil, ErrInvalidArg
	}
	return NewSynthVoice(s.sampleRate, params, velocity, duration)
}

// Close releases the C-owned voice state. Safe to call multiple times.
func (v *SynthVoice) Close() {
	if v == nil {
		return
	}
	runtime.SetFinalizer(v, nil)
	if v.cVoice != nil {
		C.free(unsafe.Pointer(v.cVoice))
		v.cVoice = nil
	}
}

// Reset restarts the original hit, including its envelopes, filters, and PRNG.
func (v *SynthVoice) Reset() {
	if v == nil || v.cVoice == nil {
		return
	}
	defer runtime.KeepAlive(v)
	C.po32_synth_voice_reset(v.cVoice)
}

// Done reports whether the hit is exhausted, inert, or closed.
func (v *SynthVoice) Done() bool {
	if v == nil || v.cVoice == nil {
		return true
	}
	defer runtime.KeepAlive(v)
	return C.po32_synth_voice_done(v.cVoice) != 0
}

// SamplesRemaining returns the number of samples left in the hit.
func (v *SynthVoice) SamplesRemaining() int {
	if v == nil || v.cVoice == nil {
		return 0
	}
	defer runtime.KeepAlive(v)
	return int(C.po32_synth_voice_samples_remaining(v.cVoice))
}

// RenderF32 renders up to maxSamples mono float32 samples in [-1, 1].
// A zero request or an exhausted hit returns no samples without changing
// state. Negative capacities and closed voices return ErrInvalidArg.
func (v *SynthVoice) RenderF32(maxSamples int) ([]float32, error) {
	if maxSamples < 0 || v == nil || v.cVoice == nil {
		return nil, ErrInvalidArg
	}
	defer runtime.KeepAlive(v)
	n := min(maxSamples, v.SamplesRemaining())
	if n == 0 {
		return nil, nil
	}
	if n > int(^uint(0)>>1)/4 {
		return nil, ErrRange
	}
	out := make([]float32, n)
	var outLen C.size_t
	st := C.po32_synth_voice_render_f32(v.cVoice,
		(*C.float)(unsafe.Pointer(&out[0])), C.size_t(n), &outLen)
	if err := statusToError(int(st)); err != nil {
		return nil, err
	}
	return out[:int(outLen)], nil
}

// Render is an alias for RenderF32.
func (v *SynthVoice) Render(maxSamples int) ([]float32, error) {
	return v.RenderF32(maxSamples)
}
