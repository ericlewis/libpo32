package po32

import (
	"math"
	"testing"
)

func approxEqual(a, b, tol float32) bool {
	return float32(math.Abs(float64(a-b))) < tol
}

func TestTagName(t *testing.T) {
	cases := []struct {
		tag  uint16
		name string
	}{
		{TagPatch, "patch"},
		{TagReset, "reset"},
		{TagPattern, "pattern"},
		{TagErase, "erase"},
		{TagState, "state"},
		{TagKnob, "knob"},
		{0xFFFF, "unknown"},
	}
	for _, tc := range cases {
		if got := TagName(tc.tag); got != tc.name {
			t.Errorf("TagName(0x%04x) = %q, want %q", tc.tag, got, tc.name)
		}
	}
}

func TestPreambleData(t *testing.T) {
	data := PreambleData()
	if len(data) != PreambleByteCount {
		t.Fatalf("PreambleData len = %d, want %d", len(data), PreambleByteCount)
	}
	// First 124 bytes are 0x55, last 4 are the sync word.
	for i := 0; i < 124; i++ {
		if data[i] != 0x55 {
			t.Fatalf("PreambleData[%d] = 0x%02x, want 0x55", i, data[i])
		}
	}
}

func TestWhitenUnwhitenRoundtrip(t *testing.T) {
	state := InitialState
	for raw := uint8(0); ; raw++ {
		coded := WhitenByte(state, raw)
		decoded := UnwhitenByte(state, coded)
		if decoded != raw {
			t.Fatalf("Whiten/Unwhiten roundtrip failed for raw=0x%02x", raw)
		}
		if raw == 255 {
			break
		}
	}
}

func TestPatchEncodeDecodeRoundtrip(t *testing.T) {
	in := PatchPacket{
		Instrument: 1,
		Side:       PatchLeft,
		Params: PatchParams{
			OscWave: 0.5, OscFreq: 0.25, OscDcy: 0.42,
			Level: 0.88,
		},
	}
	pkt, err := EncodePatchPacket(&in)
	if err != nil {
		t.Fatal(err)
	}
	if pkt.TagCode != TagPatch {
		t.Fatalf("tag = 0x%04x, want 0x%04x", pkt.TagCode, TagPatch)
	}

	out, err := DecodePatchPacket(pkt.Payload[:pkt.PayloadLen])
	if err != nil {
		t.Fatal(err)
	}
	if out.Instrument != 1 {
		t.Fatalf("instrument = %d, want 1", out.Instrument)
	}
	if out.Side != PatchLeft {
		t.Fatalf("side = %d, want PatchLeft", out.Side)
	}
	if !approxEqual(out.Params.OscWave, 0.5, 0.001) {
		t.Fatalf("OscWave = %f, want ~0.5", out.Params.OscWave)
	}
	if !approxEqual(out.Params.Level, 0.88, 0.001) {
		t.Fatalf("Level = %f, want ~0.88", out.Params.Level)
	}
}

func TestKnobEncodeDecodeRoundtrip(t *testing.T) {
	in := KnobPacket{Instrument: 3, Kind: KnobMorph, Value: 200}
	pkt, err := EncodeKnobPacket(&in)
	if err != nil {
		t.Fatal(err)
	}
	out, err := DecodeKnobPacket(pkt.Payload[:pkt.PayloadLen])
	if err != nil {
		t.Fatal(err)
	}
	if out.Instrument != 3 || out.Kind != KnobMorph || out.Value != 200 {
		t.Fatalf("knob roundtrip mismatch: %+v", out)
	}
}

func TestResetEncodeDecodeRoundtrip(t *testing.T) {
	in := ResetPacket{Instrument: 7}
	pkt, err := EncodeResetPacket(&in)
	if err != nil {
		t.Fatal(err)
	}
	out, err := DecodeResetPacket(pkt.Payload[:pkt.PayloadLen])
	if err != nil {
		t.Fatal(err)
	}
	if out.Instrument != 7 {
		t.Fatalf("instrument = %d, want 7", out.Instrument)
	}
}

func TestStateEncodeDecodeRoundtrip(t *testing.T) {
	in := StatePacket{
		MorphPairs:   MorphPairsDefault(),
		Tempo:        120,
		SwingTimes12: 5,
		PatternCount: 2,
	}
	in.PatternNumbers[0] = 0
	in.PatternNumbers[1] = 3
	pkt, err := EncodeStatePacket(&in)
	if err != nil {
		t.Fatal(err)
	}
	out, err := DecodeStatePacket(pkt.Payload[:pkt.PayloadLen])
	if err != nil {
		t.Fatal(err)
	}
	if out.Tempo != 120 || out.SwingTimes12 != 5 || out.PatternCount != 2 {
		t.Fatalf("state mismatch: %+v", out)
	}
	if out.PatternNumbers[1] != 3 {
		t.Fatalf("pattern_numbers[1] = %d, want 3", out.PatternNumbers[1])
	}
}

func TestPatternSetAndClear(t *testing.T) {
	p := NewPattern(5)
	if p.PatternNumber != 5 {
		t.Fatalf("pattern_number = %d, want 5", p.PatternNumber)
	}
	if err := p.SetTrigger(0, 1, 1); err != nil {
		t.Fatal(err)
	}
	if p.Steps[0].Instrument != 1 {
		t.Fatal("trigger not set")
	}
	if err := p.SetAccent(0, true); err != nil {
		t.Fatal(err)
	}
	if p.AccentBits&1 == 0 {
		t.Fatal("accent not set")
	}
	if err := p.ClearStep(0); err != nil {
		t.Fatal(err)
	}
	if p.Steps[0].Instrument != 0 {
		t.Fatal("step not cleared")
	}
}

func TestPatternEncodeDecodeRoundtrip(t *testing.T) {
	p := NewPattern(2)
	if err := p.SetTrigger(0, 1, 1); err != nil {
		t.Fatal(err)
	}
	if err := p.SetTrigger(4, 2, 3); err != nil {
		t.Fatal(err)
	}
	pkt, err := EncodePatternPacket(p)
	if err != nil {
		t.Fatal(err)
	}
	out, err := DecodePatternPacket(pkt.Payload[:pkt.PayloadLen])
	if err != nil {
		t.Fatal(err)
	}
	if out.PatternNumber != 2 {
		t.Fatalf("pattern_number = %d, want 2", out.PatternNumber)
	}
	if out.Steps[0].Instrument != 1 {
		t.Fatal("step 0 instrument mismatch")
	}
}

func TestPatternTriggerLane(t *testing.T) {
	for inst := uint8(1); inst <= 16; inst++ {
		lane, err := PatternTriggerLane(inst)
		if err != nil {
			t.Fatalf("instrument %d: %v", inst, err)
		}
		if lane >= PatternLaneCount {
			t.Fatalf("instrument %d: lane %d out of range", inst, lane)
		}
	}
	_, err := PatternTriggerLane(0)
	if err == nil {
		t.Fatal("expected error for instrument 0")
	}
}

func TestDecodePacketByTag(t *testing.T) {
	pkt, _ := EncodePatchPacket(&PatchPacket{Instrument: 1, Side: PatchLeft})
	decoded, err := DecodePacketByTag(pkt)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := decoded.(*PatchPacket); !ok {
		t.Fatalf("expected *PatchPacket, got %T", decoded)
	}
}

func TestEncodePatchRaw(t *testing.T) {
	params := PatchParams{OscFreq: 0.25, Level: 0.88}
	data, err := EncodePatchRaw(&params)
	if err != nil {
		t.Fatal(err)
	}
	if len(data) != ParamCount*2 {
		t.Fatalf("len = %d, want %d", len(data), ParamCount*2)
	}
	decoded, err := DecodePatchRaw(data)
	if err != nil {
		t.Fatal(err)
	}
	if !approxEqual(decoded.OscFreq, 0.25, 0.001) {
		t.Fatalf("OscFreq = %f, want ~0.25", decoded.OscFreq)
	}
}

func TestBuilderAndFrameParse(t *testing.T) {
	pkt, err := EncodePatchPacket(&PatchPacket{
		Instrument: 1, Side: PatchLeft,
		Params: PatchParams{OscFreq: 0.18, OscDcy: 0.42, Level: 0.88},
	})
	if err != nil {
		t.Fatal(err)
	}

	b := NewBuilder(2048)
	defer b.Close()
	if err := b.Append(pkt); err != nil {
		t.Fatal(err)
	}
	frame, err := b.Finish()
	if err != nil {
		t.Fatal(err)
	}
	if len(frame) <= PreambleByteCount {
		t.Fatalf("frame too short: %d bytes", len(frame))
	}

	var count int
	tail, err := FrameParse(frame, func(p *Packet) bool {
		count++
		if p.TagCode != TagPatch {
			t.Errorf("packet tag = 0x%04x, want 0x%04x", p.TagCode, TagPatch)
		}
		return true
	})
	if err != nil {
		t.Fatal(err)
	}
	if count != 1 {
		t.Fatalf("parsed %d packets, want 1", count)
	}
	if tail.MarkerC3 != 0xC3 || tail.Marker71 != 0x71 {
		t.Fatal("bad tail markers")
	}
}

func TestFrameParseEarlyStop(t *testing.T) {
	pkt, _ := EncodePatchPacket(&PatchPacket{Instrument: 1, Side: PatchLeft})
	b := NewBuilder(2048)
	defer b.Close()
	b.Append(pkt)
	frame, _ := b.Finish()

	var count int
	FrameParse(frame, func(p *Packet) bool {
		count++
		return false // stop
	})
	if count != 1 {
		t.Fatalf("count = %d, want 1", count)
	}
}

func TestFullRoundtrip(t *testing.T) {
	params := PatchParams{OscFreq: 0.28, OscDcy: 0.42, Level: 0.88}
	pkt, err := EncodePatchPacket(&PatchPacket{
		Instrument: 1, Side: PatchLeft, Params: params,
	})
	if err != nil {
		t.Fatal(err)
	}

	b := NewBuilder(2048)
	defer b.Close()
	b.Append(pkt)
	frame, err := b.Finish()
	if err != nil {
		t.Fatal(err)
	}

	samples, err := RenderDPSKF32(frame, 44100)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) == 0 {
		t.Fatal("no samples rendered")
	}

	result, decodedFrame, err := DecodeF32(samples, 44100.0)
	if err != nil {
		t.Fatal(err)
	}
	if !result.Done {
		t.Fatal("decode not done")
	}
	if result.PacketCount != 1 {
		t.Fatalf("decoded %d packets, want 1", result.PacketCount)
	}

	var decodedTag uint16
	FrameParse(decodedFrame, func(p *Packet) bool {
		decodedTag = p.TagCode
		return true
	})
	if decodedTag != TagPatch {
		t.Fatalf("decoded tag = 0x%04x, want 0x%04x", decodedTag, TagPatch)
	}
}

func TestModulatorStreaming(t *testing.T) {
	pkt, _ := EncodePatchPacket(&PatchPacket{
		Instrument: 1, Side: PatchLeft,
		Params: PatchParams{OscFreq: 0.18, Level: 0.88},
	})
	b := NewBuilder(2048)
	defer b.Close()
	b.Append(pkt)
	frame, _ := b.Finish()

	expected := RenderSampleCount(len(frame), 44100)
	m := NewModulator(frame, 44100)
	defer m.Close()

	var total int
	for !m.Done() {
		chunk, err := m.RenderF32(1024)
		if err != nil {
			t.Fatal(err)
		}
		total += len(chunk)
	}
	if total != expected {
		t.Fatalf("streamed %d samples, want %d", total, expected)
	}
	if m.SamplesRemaining() != 0 {
		t.Fatal("samples remaining should be 0")
	}
}

func TestSynthRender(t *testing.T) {
	synth := NewSynth(44100)
	n := synth.SamplesForDuration(0.5)
	if n != 22050 {
		t.Fatalf("samples for 0.5s = %d, want 22050", n)
	}

	params := PatchParams{
		OscWave: 0.0, OscFreq: 0.25, OscDcy: 0.55,
		ModAmt: 0.55, Level: 0.88,
	}
	samples, err := synth.Render(&params, 100, 0.5)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) == 0 {
		t.Fatal("no samples")
	}

	var peak float32
	for _, s := range samples {
		a := float32(math.Abs(float64(s)))
		if a > peak {
			peak = a
		}
	}
	if peak < 0.01 {
		t.Fatalf("peak = %f, expected audible output", peak)
	}
	if peak > 1.5 {
		t.Fatalf("peak = %f, unexpectedly loud", peak)
	}
}

func TestPatchParseMtdrumText(t *testing.T) {
	text := "OscWave: Triangle\n" +
		"OscFreq: 536.918 Hz\n" +
		"OscAtk: 0 ms\n" +
		"OscDcy: 160.811 ms\n" +
		"ModMode: Noise FM\n" +
		"ModRate: 244.983 Hz\n" +
		"ModAmt: 5.896 st\n" +
		"NFilMod: HP (hi-pass)\n" +
		"NFilFrq: 1840.23 Hz\n" +
		"NFilQ: 11.407\n" +
		"NEnvMod: Modulate\n" +
		"NEnvAtk: 0 ms\n" +
		"NEnvDcy: 31.522 ms\n" +
		"Mix: 40.626% osc, 59.374% noise\n" +
		"DistAmt: 27.083%\n" +
		"EQFreq: 1497.21 Hz\n" +
		"EQGain: 15.417 dB\n" +
		"Level: -2.645 dB\n" +
		"OscVel: 0.000%\n" +
		"NVel: 0.000%\n" +
		"ModVel: 0.000%\n"

	params, err := PatchParseMtdrumText(text)
	if err != nil {
		t.Fatal(err)
	}
	if !approxEqual(params.OscWave, 0.5, 0.001) {
		t.Fatalf("OscWave = %f, want ~0.5", params.OscWave)
	}
	if !approxEqual(params.NFilMod, 1.0, 0.001) {
		t.Fatalf("NFilMod = %f, want ~1.0 (HP)", params.NFilMod)
	}
}

func TestMorphPairsDefault(t *testing.T) {
	pairs := MorphPairsDefault()
	if pairs[0].Flag != 0x80 || pairs[0].Morph != 0x01 {
		t.Fatalf("pair[0] = {%02x, %02x}, want {80, 01}", pairs[0].Flag, pairs[0].Morph)
	}
}

func TestErrorSentinels(t *testing.T) {
	if ErrInvalidArg.Error() != "po32: invalid argument" {
		t.Fatal("wrong error message")
	}
	if ErrFrame.Error() != "po32: frame error" {
		t.Fatal("wrong error message")
	}
	if statusToError(0) != nil {
		t.Fatal("expected nil for OK status")
	}
}

func TestDecodeEmptyData(t *testing.T) {
	_, err := DecodePatchPacket(nil)
	if err == nil {
		t.Fatal("expected error for nil data")
	}
	_, err = DecodePatchPacket([]byte{})
	if err == nil {
		t.Fatal("expected error for empty data")
	}
	_, err = DecodePatchRaw(nil)
	if err == nil {
		t.Fatal("expected error for nil data")
	}
}

func TestPatternErrorPaths(t *testing.T) {
	p := NewPattern(0)
	if err := p.SetTrigger(16, 1, 1); err == nil {
		t.Fatal("expected error for step 16")
	}
	if err := p.SetTrigger(0, 0, 1); err == nil {
		t.Fatal("expected error for instrument 0")
	}
	if err := p.SetTrigger(0, 17, 1); err == nil {
		t.Fatal("expected error for instrument 17")
	}
	if err := p.ClearStep(16); err == nil {
		t.Fatal("expected error for step 16")
	}
	if err := p.SetAccent(16, true); err == nil {
		t.Fatal("expected error for step 16")
	}
}

func TestBuilderOverflow(t *testing.T) {
	b := NewBuilder(PreambleByteCount + 1)
	defer b.Close()
	_, err := b.AppendRawPacket(TagPatch, make([]byte, 200))
	if err == nil {
		t.Fatal("expected error for payload overflow")
	}
}

func TestModulatorRenderZero(t *testing.T) {
	pkt, _ := EncodePatchPacket(&PatchPacket{
		Instrument: 1, Side: PatchLeft,
		Params: PatchParams{OscFreq: 0.18, Level: 0.88},
	})
	b := NewBuilder(2048)
	defer b.Close()
	b.Append(pkt)
	frame, _ := b.Finish()

	m := NewModulator(frame, 44100)
	defer m.Close()
	samples, err := m.RenderF32(0)
	if err != nil {
		t.Fatal(err)
	}
	if samples != nil {
		t.Fatal("expected nil for zero maxSamples")
	}
}
