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
func (s *Synth) SamplesForDuration(seconds float32) int {
	return int(float32(s.sampleRate) * seconds)
}

// Render synthesizes a drum hit and returns mono float32 samples in [-1, 1].
// Velocity is MIDI velocity (0–127). Duration is in seconds.
func (s *Synth) Render(params *PatchParams, velocity int, duration float32) ([]float32, error) {
	var cs C.po32_synth_t
	C.po32_synth_init(&cs, C.uint32_t(s.sampleRate))

	n := s.SamplesForDuration(duration)
	if n == 0 {
		return nil, nil
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
