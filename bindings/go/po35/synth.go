package po35

/*
#include "po35_synth.h"
#include <stdlib.h>
*/
import "C"
import (
	libpo "github.com/ericlewis/libpo32/bindings/go"
	"runtime"
	"sync"
	"unsafe"
)

// Synth selects a reconstruction bank and deterministic noise seed. Its zero
// value selects the low bank and seed zero. It has no retained C resources.
type Synth struct {
	bank SynthBank
	seed uint32
}

// NewSynth selects the low (8000 Hz) or high (24000 Hz) coefficient bank. A zero
// seed is valid and maps to the C model's fixed nonzero random state.
func NewSynth(bank SynthBank, seed uint32) (*Synth, error) {
	if bank != SynthBankLow && bank != SynthBankHigh {
		return nil, libpo.ErrRange
	}
	var c C.po35_synth_t
	if err := libpo.Status(C.po35_synth_init(&c, C.po35_synth_bank_t(bank), C.uint32_t(seed))).Err(); err != nil {
		return nil, err
	}
	return &Synth{bank: bank, seed: seed}, nil
}
func (s *Synth) toC() (C.po35_synth_t, error) {
	var c C.po35_synth_t
	if s == nil {
		return c, libpo.ErrInvalidArg
	}
	err := libpo.Status(C.po35_synth_init(&c, C.po35_synth_bank_t(s.bank), C.uint32_t(s.seed))).Err()
	return c, err
}

// Bank returns the selected coefficient bank.
func (s *Synth) Bank() SynthBank { return s.bank }

// Seed returns the caller's seed, including zero before C's zero-seed mapping.
func (s *Synth) Seed() uint32 { return s.seed }

// SampleRate returns the native output rate in Hz.
func (s *Synth) SampleRate() uint32 {
	if s.bank == SynthBankHigh {
		return 24000
	}
	return 8000
}

// SamplesForRecords returns the cumulative rounded duration at the bank's
// native sample rate. Fps must be 20, 40, 60, or 80; errors return zero.
func (s *Synth) SamplesForRecords(count int, fps uint32) (int, error) {
	c, err := s.toC()
	if err != nil {
		return 0, err
	}
	n, err := sizeArg(count)
	if err != nil {
		return 0, err
	}
	var out C.size_t
	if err = libpo.Status(C.po35_synth_samples_for_records(&c, n, C.uint32_t(fps), &out)).Err(); err != nil {
		return 0, err
	}
	return sizeResult(out)
}
func floatPtr(out []float32) *C.float {
	if len(out) == 0 {
		return nil
	}
	return (*C.float)(unsafe.Pointer(&out[0]))
}

// Render returns finite, unnormalized samples at the bank's native rate. A
// numerical failure returns the committed finite prefix together with ErrRange.
// Records must contain a whole number of packed 44-byte records.
func (s *Synth) Render(records []byte, fps uint32) ([]float32, error) {
	if len(records)%SpeechRecordBytes != 0 {
		return nil, libpo.ErrRange
	}
	n, err := s.SamplesForRecords(len(records)/SpeechRecordBytes, fps)
	if err != nil {
		return nil, err
	}
	if n > maxInt/4 {
		return nil, libpo.ErrRange
	}
	out := make([]float32, n)
	used, err := s.RenderInto(records, fps, out)
	return out[:used], err
}

// RenderInto renders into caller storage and returns its finite sample count,
// including on numerical failure. Insufficient capacity leaves out untouched.
func (s *Synth) RenderInto(records []byte, fps uint32, out []float32) (int, error) {
	c, err := s.toC()
	if err != nil {
		return 0, err
	}
	var used C.size_t
	status := C.po35_synth_render(&c, bytePtr(records), C.size_t(len(records)), C.uint32_t(fps), floatPtr(out), C.size_t(len(out)), &used)
	return int(used), libpo.Status(status).Err()
}

// Voice streams model-1 reconstruction from an immutable C-owned record copy.
// Close releases that storage; a finalizer is a fallback. Copies share state,
// and calls on shared state are serialized.
type Voice struct{ state *voiceState }
type voiceState struct {
	mu      sync.Mutex
	c       *C.po35_synth_voice_t
	records *C.uint8_t
}

// NewVoice copies records into C-owned memory before retaining them. Empty
// records produce an inert, completed voice. Reset repeats the same samples.
func (s *Synth) NewVoice(records []byte, fps uint32) (*Voice, error) {
	synth, err := s.toC()
	if err != nil {
		return nil, err
	}
	if len(records)%SpeechRecordBytes != 0 {
		return nil, libpo.ErrRange
	}
	if _, err = s.SamplesForRecords(len(records)/SpeechRecordBytes, fps); err != nil {
		return nil, err
	}
	state := &voiceState{c: (*C.po35_synth_voice_t)(C.malloc(C.sizeof_po35_synth_voice_t))}
	if state.c == nil {
		return nil, libpo.ErrBufferTooSmall
	}
	if len(records) > 0 {
		state.records = (*C.uint8_t)(C.malloc(C.size_t(len(records))))
		if state.records == nil {
			state.close()
			return nil, libpo.ErrBufferTooSmall
		}
		copy(unsafe.Slice((*byte)(unsafe.Pointer(state.records)), len(records)), records)
	}
	if err = libpo.Status(C.po35_synth_voice_init(state.c, &synth, state.records, C.size_t(len(records)), C.uint32_t(fps))).Err(); err != nil {
		state.close()
		return nil, err
	}
	runtime.SetFinalizer(state, (*voiceState).close)
	return &Voice{state: state}, nil
}
func (s *voiceState) close() {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.c != nil {
		C.free(unsafe.Pointer(s.c))
		s.c = nil
	}
	if s.records != nil {
		C.free(unsafe.Pointer(s.records))
		s.records = nil
	}
}

// Close releases C-owned voice state and records. It is nil-safe and idempotent.
func (v *Voice) Close() {
	if v != nil && v.state != nil {
		v.state.close()
		runtime.SetFinalizer(v.state, nil)
	}
}

// Reset restarts playback and clears a latched numerical error.
func (v *Voice) Reset() error {
	if v == nil || v.state == nil {
		return libpo.ErrInvalidArg
	}
	s := v.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.c == nil {
		return libpo.ErrInvalidArg
	}
	C.po35_synth_voice_reset(s.c)
	return nil
}

// SamplesRemaining returns uncommitted samples, including after a numerical
// failure. Closed, zero-value, and nil voices have no remaining samples.
func (v *Voice) SamplesRemaining() int {
	if v == nil || v.state == nil {
		return 0
	}
	s := v.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.c == nil {
		return 0
	}
	return int(C.po35_synth_voice_samples_remaining(s.c))
}

// Done reports whether playback has completed. A numerical error does not mark
// an unfinished voice done. Closed, zero-value, and nil voices are done.
func (v *Voice) Done() bool {
	if v == nil || v.state == nil {
		return true
	}
	s := v.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.c == nil {
		return true
	}
	return C.po35_synth_voice_done(s.c) != 0
}

// Render returns at most capacity samples, retaining a finite prefix on error.
// Zero capacity commits no samples; a latched error is still reported.
func (v *Voice) Render(capacity int) ([]float32, error) {
	if capacity < 0 || capacity > maxInt/4 {
		return nil, libpo.ErrRange
	}
	out := make([]float32, capacity)
	n, err := v.RenderInto(out)
	return out[:n], err
}

// RenderInto commits finite samples to out, reporting the count even on error.
// Samples after that prefix remain untouched. Numerical errors latch until Reset.
func (v *Voice) RenderInto(out []float32) (int, error) {
	if v == nil || v.state == nil {
		return 0, libpo.ErrInvalidArg
	}
	s := v.state
	s.mu.Lock()
	defer s.mu.Unlock()
	defer runtime.KeepAlive(s)
	if s.c == nil {
		return 0, libpo.ErrInvalidArg
	}
	var n C.size_t
	status := C.po35_synth_voice_render_f32(s.c, floatPtr(out), C.size_t(len(out)), &n)
	return int(n), libpo.Status(status).Err()
}
