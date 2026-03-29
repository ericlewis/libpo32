package po32

/*
#include "po32.h"
#include <stdlib.h>
#include <string.h>
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// RenderSampleCount returns the number of mono float32 samples needed
// to render a frame of the given length at the given sample rate.
func RenderSampleCount(frameLen int, sampleRate uint32) int {
	return int(C.po32_render_sample_count(C.size_t(frameLen), C.uint32_t(sampleRate)))
}

// RenderDPSKF32 renders a transfer frame to mono float32 audio.
func RenderDPSKF32(frame []byte, sampleRate uint32) ([]float32, error) {
	n := RenderSampleCount(len(frame), sampleRate)
	if n == 0 {
		return nil, nil
	}
	out := make([]float32, n)
	s := C.po32_render_dpsk_f32(
		(*C.uint8_t)(&frame[0]),
		C.size_t(len(frame)),
		C.uint32_t(sampleRate),
		(*C.float)(unsafe.Pointer(&out[0])),
		C.size_t(n),
	)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	return out, nil
}

// Modulator renders transfer audio in streaming chunks.
//
// The modulator allocates C memory for the frame copy and internal state.
// Call Close when done, or rely on the garbage collector's finalizer.
type Modulator struct {
	cMod   *C.po32_modulator_t
	cFrame *C.uint8_t
}

// NewModulator creates a streaming modulator over the given frame.
func NewModulator(frame []byte, sampleRate uint32) *Modulator {
	cFrame := (*C.uint8_t)(C.malloc(C.size_t(len(frame))))
	C.memcpy(unsafe.Pointer(cFrame), unsafe.Pointer(&frame[0]), C.size_t(len(frame)))
	cMod := (*C.po32_modulator_t)(C.malloc(C.size_t(unsafe.Sizeof(C.po32_modulator_t{}))))
	C.po32_modulator_init(cMod, cFrame, C.size_t(len(frame)), C.uint32_t(sampleRate))
	m := &Modulator{cMod: cMod, cFrame: cFrame}
	runtime.SetFinalizer(m, (*Modulator).Close)
	return m
}

// Close releases C-allocated memory. Safe to call multiple times.
func (m *Modulator) Close() {
	if m.cMod != nil {
		C.free(unsafe.Pointer(m.cMod))
		m.cMod = nil
	}
	if m.cFrame != nil {
		C.free(unsafe.Pointer(m.cFrame))
		m.cFrame = nil
	}
}

// Reset restarts the modulator from the beginning.
func (m *Modulator) Reset() {
	C.po32_modulator_reset(m.cMod)
}

// SamplesRemaining returns how many samples are left to render.
func (m *Modulator) SamplesRemaining() int {
	return int(C.po32_modulator_samples_remaining(m.cMod))
}

// Done returns true when all samples have been rendered.
func (m *Modulator) Done() bool {
	return C.po32_modulator_done(m.cMod) != 0
}

// RenderF32 renders up to maxSamples of audio and returns the samples produced.
func (m *Modulator) RenderF32(maxSamples int) ([]float32, error) {
	if maxSamples == 0 {
		return nil, nil
	}
	out := make([]float32, maxSamples)
	var outLen C.size_t
	s := C.po32_modulator_render_f32(m.cMod,
		(*C.float)(unsafe.Pointer(&out[0])),
		C.size_t(maxSamples), &outLen)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	return out[:int(outLen)], nil
}
