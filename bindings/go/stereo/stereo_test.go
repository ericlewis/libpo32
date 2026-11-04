package stereo_test

import (
	"bytes"
	"math"
	"runtime"
	"testing"

	"github.com/ericlewis/libpo32/bindings/go"
	"github.com/ericlewis/libpo32/bindings/go/po32"
	"github.com/ericlewis/libpo32/bindings/go/stereo"
)

func TestRequirementsAndInput(t *testing.T) {
	if _, err := stereo.DecodeRequirements(-1, 48000); err != libpo.ErrInvalidArg {
		t.Fatalf("negative count: %v", err)
	}
	if _, err := stereo.DecodeF32([]float32{0}, 48000, stereo.ChannelNormal); err != libpo.ErrInvalidArg {
		t.Fatalf("odd stereo input: %v", err)
	}
	if _, err := stereo.DecodeF32(nil, 48000, stereo.ChannelNormal); err != libpo.ErrFrame {
		t.Fatalf("empty input: %v", err)
	}
	if _, err := stereo.NewModulator(nil, 48000); err != libpo.ErrInvalidArg {
		t.Fatalf("empty transmitter: %v", err)
	}
}

func TestChunkedTransmitOwnsInput(t *testing.T) {
	wire := make([]byte, 128)
	for i := range wire {
		wire[i] = byte(i)
	}
	want, err := stereo.RenderF32(wire, 48000)
	if err != nil {
		t.Fatal(err)
	}
	m, err := stereo.NewModulator(wire, 48000)
	if err != nil {
		t.Fatal(err)
	}
	defer m.Close()
	for i := range wire {
		wire[i] ^= 255
	}
	runtime.GC()
	if m.FramesRemaining() != len(want)/2 {
		t.Fatal("frame count uses stereo frames")
	}
	var got []float32
	for !m.Done() {
		part, err := m.RenderF32(73)
		if err != nil {
			t.Fatal(err)
		}
		got = append(got, part...)
	}
	if len(got) != len(want) {
		t.Fatal("short render")
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("chunk mismatch at %d", i)
		}
	}
	if err := m.Reset(); err != nil {
		t.Fatal(err)
	}
	got, err = m.RenderF32(len(want) / 2)
	if err != nil || len(got) != len(want) {
		t.Fatalf("reset: %v", err)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatal("reset changed output")
		}
	}
	m.Close()
	m.Close()
	if _, err := m.RenderF32(1); err != libpo.ErrInvalidArg {
		t.Fatalf("closed render: %v", err)
	}
}

func TestRawWireRecovery(t *testing.T) {
	// Independent framing uses the established shared checksum primitives.
	wire := make([]byte, 4096)
	for i := 0; i < 4092; i++ {
		wire[i] = byte(i)
	}
	copy(wire[4092:], []byte{0x4d, 0xb8, 0x16, 0x5d})
	state := uint16(0x1d0f)
	appendByte := func(b byte) { wire = append(wire, po32.WhitenByte(state, b)); state = po32.CRCUpdate(state, b) }
	packet := func(tag uint16) {
		appendByte(byte(tag))
		appendByte(byte(tag >> 8))
		appendByte(0)
		trailer := state
		appendByte(byte(trailer))
		appendByte(byte(trailer >> 8))
	}
	packet(0x985b)
	packet(0x826f)
	audio, err := stereo.RenderF32(wire, 48000)
	if err != nil {
		t.Fatal(err)
	}
	recovered, err := stereo.DecodeF32(audio, 48000, stereo.ChannelNormal)
	if err != nil || len(recovered) < len(wire) || !bytes.Equal(recovered[:len(wire)], wire) {
		t.Fatalf("recovery: %v", err)
	}
	for i := 0; i < len(audio); i += 2 {
		audio[i], audio[i+1] = audio[i+1], audio[i]
	}
	recovered, err = stereo.DecodeF32(audio, 48000, stereo.ChannelSwapped)
	if err != nil || len(recovered) < len(wire) || !bytes.Equal(recovered[:len(wire)], wire) {
		t.Fatalf("swap: %v", err)
	}
	audio[3] = float32(math.NaN())
	if _, err := stereo.DecodeF32(audio, 48000, stereo.ChannelSwapped); err != libpo.ErrRange {
		t.Fatalf("NaN: %v", err)
	}
}
