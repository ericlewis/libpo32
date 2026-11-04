package stereo_test

import (
	"math"
	"runtime"
	"testing"

	libpo "github.com/ericlewis/libpo32/bindings/go"
	"github.com/ericlewis/libpo32/bindings/go/stereo"
)

func TestRequirementsUnitsAndLimits(t *testing.T) {
	for _, rate := range []uint32{32000, 44100, 48000, 96000, 192000} {
		got, err := stereo.DecodeRequirements(1001, rate)
		if err != nil || got != (stereo.Requirements{ScalarCount: 4004, ByteCount: 1001, WireCapacity: 500}) {
			t.Fatalf("requirements at %d: %+v, %v", rate, got, err)
		}
		got, err = stereo.DecodeRequirements(0, rate)
		if err != nil || got != (stereo.Requirements{}) {
			t.Fatalf("empty requirements: %+v, %v", got, err)
		}
	}
	for _, rate := range []uint32{0, 31999, 192001, ^uint32(0)} {
		if got, err := stereo.DecodeRequirements(100, rate); got != (stereo.Requirements{}) || err != libpo.ErrRange {
			t.Fatalf("invalid rate %d: %+v, %v", rate, got, err)
		}
	}
	if _, err := stereo.DecodeRequirements(int(^uint(0)>>1)/32+1, 48000); err != libpo.ErrRange {
		t.Fatalf("unrepresentable scratch size: %v", err)
	}
	if _, err := stereo.DecodeF32([]float32{0, 0}, 48000, stereo.ChannelOrder(2)); err != libpo.ErrInvalidArg {
		t.Fatalf("invalid order: %v", err)
	}
	if _, err := stereo.DecodeF32([]float32{0, 0}, 48000, stereo.ChannelNormal); err != libpo.ErrFrame {
		t.Fatalf("short input: %v", err)
	}
	if _, err := stereo.DecodeF32([]float32{0, float32(math.Inf(1))}, 48000, stereo.ChannelNormal); err != libpo.ErrRange {
		t.Fatalf("nonfinite short input: %v", err)
	}
}

func TestModulatorCapacityAndSharedOwnership(t *testing.T) {
	for _, rate := range []uint32{0, 32000, 47999, 192000} {
		if value, err := stereo.NewModulator([]byte{1}, rate); value != nil || err != libpo.ErrRange {
			t.Fatalf("invalid rate %d: %v, %v", rate, value, err)
		}
	}
	for _, rate := range []uint32{44100, 48000, 96000} {
		m, err := stereo.NewModulator([]byte{0x21}, rate)
		if err != nil {
			t.Fatal(err)
		}
		remaining := m.FramesRemaining()
		if remaining <= int(rate)/2 || m.Done() {
			t.Fatalf("new state: remaining %d, done %v", remaining, m.Done())
		}
		if out, err := m.RenderF32(0); err != nil || len(out) != 0 || m.FramesRemaining() != remaining {
			t.Fatalf("zero capacity: %d, %v", len(out), err)
		}
		if _, err := m.RenderF32(-1); err != libpo.ErrInvalidArg {
			t.Fatalf("negative capacity: %v", err)
		}
		if _, err := m.RenderF32(int(^uint(0) >> 1)); err != libpo.ErrRange {
			t.Fatalf("overflow capacity: %v", err)
		}
		copyOfModulator := *m
		m = nil
		runtime.GC()
		out, err := copyOfModulator.RenderF32(remaining + 1)
		if err != nil || len(out) != 2*remaining || !copyOfModulator.Done() || copyOfModulator.FramesRemaining() != 0 {
			t.Fatalf("render copy at %d: len %d, %v", rate, len(out), err)
		}
		for i, value := range out {
			if math.IsNaN(float64(value)) || math.Abs(float64(value)) > 0.600001 {
				t.Fatalf("invalid audio at %d: %v", i, value)
			}
		}
		if part, err := copyOfModulator.RenderF32(1); err != nil || len(part) != 0 {
			t.Fatalf("completed render: %d, %v", len(part), err)
		}
		alias := copyOfModulator
		copyOfModulator.Close()
		if _, err := alias.RenderF32(1); err != libpo.ErrInvalidArg {
			t.Fatalf("closed shared owner: %v", err)
		}
		alias.Close()
	}
}

func TestNilAndClosedModulators(t *testing.T) {
	for _, m := range []*stereo.Modulator{nil, new(stereo.Modulator)} {
		m.Close()
		if m.Done() || m.FramesRemaining() != 0 {
			t.Fatal("uninitialized modulator reports completion")
		}
		if err := m.Reset(); err != libpo.ErrInvalidArg {
			t.Fatalf("reset uninitialized: %v", err)
		}
		if _, err := m.RenderF32(0); err != libpo.ErrInvalidArg {
			t.Fatalf("render uninitialized: %v", err)
		}
	}
}
