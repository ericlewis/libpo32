package po32

/*
#include "po32_synth.h"
*/
import "C"
import "unsafe"

// NewSynth creates a drum synthesizer at the given sample rate.
func NewSynth(sampleRate uint32) *Synth {
	return &Synth{sampleRate: sampleRate}
}

// SamplesForDuration returns how many samples are needed for the given duration.
// Non-positive, non-finite, and unrepresentable durations return zero.
func (s *Synth) SamplesForDuration(seconds float32) int {
	if s == nil {
		return 0
	}
	var cs C.po32_synth_t
	C.po32_synth_init(&cs, C.uint32_t(s.sampleRate))
	n := C.po32_synth_samples_for_duration(&cs, C.float(seconds))
	if uint64(n) > uint64(^uint(0)>>1) {
		return 0
	}
	return int(n)
}

// Render synthesizes a drum hit and returns mono float32 samples in [-1, 1].
// Velocity is MIDI velocity (0–127); values outside that range return ErrRange.
// Duration is in seconds.
func (s *Synth) Render(params *PatchParams, velocity int, duration float32) ([]float32, error) {
	if s == nil || params == nil {
		return nil, ErrInvalidArg
	}
	if velocity < 0 || velocity > 127 {
		return nil, ErrRange
	}
	var cs C.po32_synth_t
	C.po32_synth_init(&cs, C.uint32_t(s.sampleRate))

	n := s.SamplesForDuration(duration)
	if n == 0 {
		return nil, nil
	}
	if n > int(^uint(0)>>1)/4 {
		return nil, ErrRange
	}

	out := make([]float32, n)
	cp := patchParamsToC(params)
	var outLen C.size_t
	st := C.po32_synth_render(&cs, &cp, C.int(velocity), C.float(duration),
		(*C.float)(unsafe.Pointer(&out[0])), C.size_t(n), &outLen)
	if err := statusToError(int(st)); err != nil {
		return nil, err
	}
	return out[:int(outLen)], nil
}
