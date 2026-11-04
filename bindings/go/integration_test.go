package libpo_test

import (
	"bytes"
	"errors"
	"testing"

	"github.com/ericlewis/libpo32/bindings/go"
	"github.com/ericlewis/libpo32/bindings/go/po32"
	"github.com/ericlewis/libpo32/bindings/go/po33"
	"github.com/ericlewis/libpo32/bindings/go/po35"
	"github.com/ericlewis/libpo32/bindings/go/stereo"
)

func TestFamiliesLinkAndRoundTrip(t *testing.T) {
	// Import and exercise every package in one executable: isolated package
	// tests alone cannot catch duplicate native symbols or mixed status types.
	if !errors.Is(po32.ErrRange, libpo.ErrRange) || !errors.Is(po33.ErrRange, libpo.ErrRange) {
		t.Fatal("family errors must share their identity")
	}
	b32 := po32.NewBuilder(1024)
	defer b32.Close()
	p32, err := po32.EncodeResetPacket(&po32.ResetPacket{Instrument: 1})
	if err != nil {
		t.Fatal(err)
	}
	if err = b32.Append(p32); err != nil {
		t.Fatal(err)
	}
	f32, err := b32.Finish()
	if err != nil {
		t.Fatal(err)
	}
	a32, err := po32.RenderDPSKF32(f32, 44100)
	if err != nil {
		t.Fatal(err)
	}
	r32, decoded32, err := po32.DecodeF32(a32, 44100)
	if err != nil || !r32.Done || !bytes.Equal(f32, decoded32) {
		t.Fatalf("PO32: %v", err)
	}

	b33, err := po33.NewBuilder(8192)
	if err != nil {
		t.Fatal(err)
	}
	defer b33.Close()
	settings, err := po33.EncodeSettings(&po33.Settings{Tempo: 120})
	if err != nil {
		t.Fatal(err)
	}
	for len(settings) > 0 {
		n := min(len(settings), libpo.MaxPayload)
		if _, err = b33.AppendRawPacket(po33.TagSettings, settings[:n]); err != nil {
			t.Fatal(err)
		}
		settings = settings[n:]
	}
	f33, err := b33.Finish()
	if err != nil {
		t.Fatal(err)
	}
	a33, err := stereo.RenderF32(f33, 48000)
	if err != nil {
		t.Fatal(err)
	}
	w33, err := stereo.DecodeF32(a33, 48000, stereo.ChannelNormal)
	if err != nil {
		t.Fatal(err)
	}
	r33, err := po33.FrameParse(w33, nil)
	if err != nil || !r33.Done || !bytes.Equal(f33, w33[:r33.FrameBytes]) {
		t.Fatalf("PO33: %v", err)
	}

	pcm := make([]float32, 2400)
	speech, err := po35.AnalysisEncodeF32(pcm, 24000, 80)
	if err != nil {
		t.Fatal(err)
	}
	b35, err := po35.NewBuilder(8192)
	if err != nil {
		t.Fatal(err)
	}
	defer b35.Close()
	h, err := po35.EncodeSpeechHeader(&po35.SpeechHeader{Slot: 0, RecordCount: uint16(len(speech) / 44), RecordsPerSecond: 80})
	if err != nil {
		t.Fatal(err)
	}
	if _, err = b35.AppendRawPacket(po35.TagSpeechHeader, h); err != nil {
		t.Fatal(err)
	}
	for pos := 0; pos < len(speech); {
		n := min(len(speech)-pos, 220)
		if _, err = b35.AppendRawPacket(po35.TagSpeechData, speech[pos:pos+n]); err != nil {
			t.Fatal(err)
		}
		pos += n
	}
	if _, err = b35.AppendRawPacket(po35.TagSpeechEnd, nil); err != nil {
		t.Fatal(err)
	}
	f35, err := b35.Finish()
	if err != nil {
		t.Fatal(err)
	}
	a35, err := stereo.RenderF32(f35, 48000)
	if err != nil {
		t.Fatal(err)
	}
	w35, err := stereo.DecodeF32(a35, 48000, stereo.ChannelNormal)
	if err != nil {
		t.Fatal(err)
	}
	r35, err := po35.FrameParse(w35, nil)
	if err != nil || !r35.Done || !bytes.Equal(f35, w35[:r35.FrameBytes]) {
		t.Fatalf("PO35: %v", err)
	}
	synth, err := po35.NewSynth(po35.SynthBankHigh, 350)
	if err != nil {
		t.Fatal(err)
	}
	preview, err := synth.Render(speech, 80)
	if err != nil || len(preview) != 2400 {
		t.Fatalf("speech: %d %v", len(preview), err)
	}
	for _, sample := range preview {
		if sample != 0 {
			t.Fatal("silence encoded as nonzero speech")
		}
	}
}
