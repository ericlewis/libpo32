package po32

import (
	"math"
	"testing"
)

func TestSynthInvalidDurationsAreInert(t *testing.T) {
	s := NewSynth(44100)
	params := PatchParams{Level: 0.8}
	for _, duration := range []float32{-1, 0, float32(math.NaN()), float32(math.Inf(1)), float32(math.Inf(-1)), math.MaxFloat32} {
		if n := s.SamplesForDuration(duration); n != 0 {
			t.Errorf("SamplesForDuration(%v) = %d, want 0", duration, n)
		}
		out, err := s.Render(&params, 100, duration)
		if err != nil || len(out) != 0 {
			t.Errorf("Render(%v) = %d samples, %v; want no samples", duration, len(out), err)
		}
	}
	if out, err := s.Render(nil, 100, 1); err != ErrInvalidArg || out != nil {
		t.Fatalf("Render(nil) = %v, %v", out, err)
	}
	var nilSynth *Synth
	if n := nilSynth.SamplesForDuration(1); n != 0 {
		t.Errorf("nil SamplesForDuration = %d", n)
	}
	if _, err := nilSynth.Render(&params, 100, 1); err != ErrInvalidArg {
		t.Errorf("nil Render = %v", err)
	}
}

func TestCodecRejectsNilPackets(t *testing.T) {
	tests := []struct {
		name string
		call func() error
	}{
		{"patch", func() error { _, err := EncodePatchPacket(nil); return err }},
		{"knob", func() error { _, err := EncodeKnobPacket(nil); return err }},
		{"reset", func() error { _, err := EncodeResetPacket(nil); return err }},
		{"state", func() error { _, err := EncodeStatePacket(nil); return err }},
		{"pattern", func() error { _, err := EncodePatternPacket(nil); return err }},
		{"raw patch", func() error { _, err := EncodePatchRaw(nil); return err }},
		{"decode nil", func() error { _, err := DecodePacketByTag(nil); return err }},
		{"decode negative length", func() error { _, err := DecodePacketByTag(&Packet{PayloadLen: -1}); return err }},
		{"decode oversized length", func() error { _, err := DecodePacketByTag(&Packet{PayloadLen: MaxPayload + 1}); return err }},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if err := tt.call(); err != ErrInvalidArg {
				t.Fatalf("error = %v, want ErrInvalidArg", err)
			}
		})
	}
}

func TestNegativeSizesDoNotAllocate(t *testing.T) {
	if n := RenderSampleCount(-1, 44100); n != 0 {
		t.Errorf("RenderSampleCount(-1) = %d, want 0", n)
	}
	b := NewBuilder(-1)
	defer b.Close()
	if _, err := b.Finish(); err != ErrInvalidArg {
		t.Errorf("negative builder Finish = %v", err)
	}
	if _, _, err := DecodeF32WithCapacity([]float32{0}, 44100, -1); err != ErrInvalidArg {
		t.Errorf("negative decode capacity = %v", err)
	}
}

func TestSynthRejectsOutOfRangeVelocity(t *testing.T) {
	params := PatchParamsZero()
	for _, velocity := range []int{-1, 128, int(^uint(0) >> 1)} {
		if _, err := NewSynth(44100).Render(&params, velocity, 0.001); err != ErrRange {
			t.Fatalf("Render velocity %d: %v", velocity, err)
		}
		voice, err := NewSynthVoice(44100, &params, velocity, 0.001)
		if voice != nil {
			voice.Close()
		}
		if err != ErrRange {
			t.Fatalf("NewSynthVoice velocity %d: %v", velocity, err)
		}
	}
}
