package po35

import (
	"bytes"
	"math"
	"reflect"
	"runtime"
	"testing"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

func TestStructuralCodecsPreserveUnknownBytes(t *testing.T) {
	for _, n := range []int{SpeechHeaderBytes, SpeechRecordBytes, PatternBytes, SettingsBaseBytes, 255} {
		raw := make([]byte, n)
		for i := range raw {
			raw[i] = byte(i*71 + 193)
		}
		var encoded []byte
		var err error
		switch n {
		case SpeechHeaderBytes:
			p, e := DecodeSpeechHeader(raw)
			if e != nil {
				t.Fatal(e)
			}
			encoded, err = EncodeSpeechHeader(p)
		case SpeechRecordBytes:
			p, e := DecodeSpeechRecord(raw)
			if e != nil {
				t.Fatal(e)
			}
			encoded, err = EncodeSpeechRecord(p)
		case PatternBytes:
			p, e := DecodePattern(raw)
			if e != nil {
				t.Fatal(e)
			}
			encoded, err = EncodePattern(p)
		default:
			raw[155] = byte(n - SettingsBaseBytes)
			p, e := DecodeSettings(raw)
			if e != nil {
				t.Fatal(e)
			}
			encoded, err = EncodeSettings(p)
		}
		if err != nil || !bytes.Equal(encoded, raw) {
			t.Fatalf("size %d: roundtrip = %x, %v", n, encoded, err)
		}
	}
	if _, e := EncodeSettings(&Settings{ChainCount: ChainMaxCount + 1}); e != libpo.ErrRange {
		t.Fatalf("chain: %v", e)
	}
	if _, e := DecodeSpeechHeader(make([]byte, 8)); e != libpo.ErrParse {
		t.Fatalf("length: %v", e)
	}
	if _, e := EncodeSpeechHeader(nil); e != libpo.ErrInvalidArg {
		t.Fatalf("nil: %v", e)
	}
}

func TestFramingOwnershipStopAndPartialFailure(t *testing.T) {
	b, e := NewBuilder(PreambleByteCount + 64)
	if e != nil {
		t.Fatal(e)
	}
	defer b.Close()
	offset, e := b.AppendRawPacket(0xaabb, []byte{1, 2, 3})
	if e != nil || offset != PreambleByteCount {
		t.Fatalf("append: %d %v", offset, e)
	}
	_, e = b.AppendRawPacket(TagSpeechEnd, nil)
	if e != nil {
		t.Fatal(e)
	}
	frame, e := b.Finish()
	if e != nil {
		t.Fatal(e)
	}
	var saved *libpo.Packet
	r, e := FrameParse(frame, func(p *libpo.Packet) bool { saved = p; return false })
	if e != nil || !r.Stopped || r.Done || r.PacketCount != 1 || r.FrameBytes != PreambleByteCount+8 {
		t.Fatalf("stop: %+v %v", r, e)
	}
	if saved.TagCode != 0xaabb || saved.Payload[2] != 3 || !saved.Trailer.MatchesState {
		t.Fatalf("saved packet: %+v", saved)
	}
	r, e = FrameParse(append(frame, 1, 2, 3), nil)
	if e != nil || !r.Done || r.Stopped || r.PacketCount != 2 || r.FrameBytes != len(frame) {
		t.Fatalf("full: %+v %v", r, e)
	}
	frame[len(frame)-1] ^= 1
	r, e = FrameParse(frame, nil)
	if e == nil || r.Done || r.PacketCount != 2 || r.FrameBytes != PreambleByteCount+13 || r.FinalState == 0 {
		t.Fatalf("partial: %+v %v", r, e)
	}
	if e = b.Reset(); e != nil {
		t.Fatal(e)
	}
	empty, e := b.Finish()
	if e != nil || len(empty) != PreambleByteCount+5 {
		t.Fatalf("reset: %d %v", len(empty), e)
	}
	b.Close()
	b.Close()
	if _, e = b.Finish(); e != libpo.ErrInvalidArg {
		t.Fatalf("closed: %v", e)
	}
	if _, e = NewBuilder(-1); e != libpo.ErrRange {
		t.Fatalf("negative capacity: %v", e)
	}
	if _, e = NewBuilder(PreambleByteCount - 1); e != libpo.ErrBufferTooSmall {
		t.Fatalf("small capacity: %v", e)
	}
}

func TestAnalysisCGoldenAndSilence(t *testing.T) {
	expected := []byte{40, 252, 51, 16, 199, 16, 246, 11, 249, 8, 252, 6, 254, 5, 181, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
	pcm := make([]float32, 24000)
	for i := range pcm {
		pcm[i] = float32(i%128-64) / 256
	}
	records, e := AnalysisEncodeF32(pcm, 24000, 40)
	if e != nil || len(records) != 1760 || !bytes.Equal(records[20*44:21*44], expected) {
		t.Fatalf("C golden mismatch: %v", e)
	}
	clear(pcm)
	records, e = AnalysisEncodeF32(pcm, 24000, 40)
	if e != nil {
		t.Fatal(e)
	}
	for i := 0; i < len(records); i += 44 {
		if records[i+2] != 0 {
			t.Fatal("silence gained energy")
		}
	}
	if n, e := AnalysisSizeForPCM(-1, 24000, 40); n != 0 || e != libpo.ErrRange {
		t.Fatalf("negative: %d %v", n, e)
	}
	if _, e := AnalysisEncodeF32([]float32{float32(math.NaN())}, 24000, 40); e != libpo.ErrRange {
		t.Fatalf("NaN: %v", e)
	}
	if out, e := AnalysisEncodeF32(nil, 24000, 40); len(out) != 0 || e != nil {
		t.Fatalf("empty: %v", e)
	}
	if _, e := AnalysisSizeForPCM(24000, 24000, 30); e != libpo.ErrRange {
		t.Fatalf("fps: %v", e)
	}
}

func TestSynthCGoldenStreamingOwnershipAndReset(t *testing.T) {
	expected := []float32{-0.37978512048721313, -0.041792649775743484, 0.6129953861236572, -0.05307433009147644, 0.07902200520038605, 0.6286685466766357, 0.49596133828163147, 0.12208618968725204}
	records := make([]byte, 2*SpeechRecordBytes)
	for i := 0; i < len(records); i += 44 {
		records[i+1] = 6
		records[i+2] = 128
		records[i+3] = 255
	}
	s, e := NewSynth(SynthBankLow, SynthDefaultSeed)
	if e != nil {
		t.Fatal(e)
	}
	full, e := s.Render(records, 80)
	if e != nil || len(full) != 200 || !reflect.DeepEqual(full[:8], expected) {
		t.Fatalf("noise golden: %v", e)
	}
	for _, bank := range []SynthBank{SynthBankLow, SynthBankHigh} {
		s, e = NewSynth(bank, 0)
		if e != nil {
			t.Fatal(e)
		}
		want, e := s.Render(records, 60)
		if e != nil {
			t.Fatal(e)
		}
		source := append([]byte(nil), records...)
		v, e := s.NewVoice(source, 60)
		if e != nil {
			t.Fatal(e)
		}
		clear(source)
		runtime.GC()
		var got []float32
		for !v.Done() {
			chunk, e := v.Render(17)
			if e != nil || len(chunk) == 0 {
				t.Fatalf("chunk: %v", e)
			}
			got = append(got, chunk...)
		}
		if !reflect.DeepEqual(got, want) || v.SamplesRemaining() != 0 {
			t.Fatal("chunk or owned storage mismatch")
		}
		if e = v.Reset(); e != nil {
			t.Fatal(e)
		}
		if v.SamplesRemaining() != len(want) {
			t.Fatal("remaining after reset")
		}
		again, e := v.Render(len(want))
		if e != nil || !reflect.DeepEqual(again, want) {
			t.Fatal("reset mismatch")
		}
		v.Close()
		v.Close()
		if _, e = v.Render(1); e != libpo.ErrInvalidArg {
			t.Fatalf("closed: %v", e)
		}
	}
	s, _ = NewSynth(SynthBankLow, 350)
	for i, want := range []int{0, 133, 267, 400} {
		n, e := s.SamplesForRecords(i, 60)
		if e != nil || n != want {
			t.Fatalf("timing: %d %v", n, e)
		}
	}
	if _, e = s.NewVoice(make([]byte, 43), 20); e != libpo.ErrRange {
		t.Fatalf("record length: %v", e)
	}
	if _, e = NewSynth(SynthBank(99), 0); e != libpo.ErrRange {
		t.Fatalf("bank: %v", e)
	}
}

func TestSynthReturnsFiniteErrorPrefix(t *testing.T) {
	record := make([]byte, 44)
	record[1] = 6
	record[2] = 128
	for i := 14; i < 44; i++ {
		record[i] = 127
	}
	s, _ := NewSynth(SynthBankHigh, 350)
	prefix, e := s.Render(record, 20)
	if e != libpo.ErrRange || len(prefix) == 0 || len(prefix) >= 1200 {
		t.Fatalf("prefix %d error %v", len(prefix), e)
	}
	for _, v := range prefix {
		if math.IsNaN(float64(v)) || math.IsInf(float64(v), 0) {
			t.Fatal("nonfinite committed output")
		}
	}
	v, e := s.NewVoice(record, 20)
	if e != nil {
		t.Fatal(e)
	}
	defer v.Close()
	got, e := v.Render(1200)
	if e != libpo.ErrRange || !reflect.DeepEqual(got, prefix) || v.Done() || v.SamplesRemaining() != 1200-len(prefix) {
		t.Fatalf("stream error: %d %v", len(got), e)
	}
	if got, e = v.Render(1200); e != libpo.ErrRange || len(got) != 0 {
		t.Fatalf("latched: %d %v", len(got), e)
	}
	if e = v.Reset(); e != nil {
		t.Fatal(e)
	}
	got, e = v.Render(1200)
	if e != libpo.ErrRange || !reflect.DeepEqual(got, prefix) {
		t.Fatal("error reset mismatch")
	}
}

func TestDrumPatchAndAtomicBuilderErrors(t *testing.T) {
	if _, e := EncodeDrumPatch(nil); e != libpo.ErrInvalidArg {
		t.Fatalf("nil patch: %v", e)
	}
	p, e := EncodeDrumPatch(&DrumPatch{Instrument: 1})
	if e != nil {
		t.Fatal(e)
	}
	decoded, e := DecodeDrumPatch(p.Payload[:p.PayloadLen])
	if e != nil || decoded.Instrument != 1 {
		t.Fatalf("patch: %+v %v", decoded, e)
	}
	b, e := NewBuilder(PreambleByteCount + 5)
	if e != nil {
		t.Fatal(e)
	}
	defer b.Close()
	if _, e = b.AppendRawPacket(0x71c3, nil); e != libpo.ErrInvalidArg {
		t.Fatalf("reserved tag: %v", e)
	}
	if _, e = b.AppendRawPacket(0xabcd, make([]byte, 256)); e != libpo.ErrRange {
		t.Fatalf("oversize payload: %v", e)
	}
	if _, e = b.AppendRawPacket(0xabcd, []byte{1}); e != libpo.ErrBufferTooSmall {
		t.Fatalf("capacity: %v", e)
	}
	if e = b.Append(&libpo.Packet{PayloadLen: -1}); e != libpo.ErrInvalidArg {
		t.Fatalf("negative packet: %v", e)
	}
	one, e := b.Finish()
	if e != nil {
		t.Fatal(e)
	}
	two, e := b.Finish()
	if e != nil || !bytes.Equal(one, two) {
		t.Fatal("finish not idempotent")
	}
	if _, e = b.AppendRawPacket(0xabcd, nil); e != libpo.ErrFrame {
		t.Fatalf("finished append: %v", e)
	}
	r, e := FrameParse(one, nil)
	if e != nil || !r.Done || r.PacketCount != 0 {
		t.Fatalf("atomic frame: %+v %v", r, e)
	}
	shared := *b
	b.Close()
	if _, e = shared.Finish(); e != libpo.ErrInvalidArg {
		t.Fatalf("copied owner: %v", e)
	}
}

func TestRenderCapacityAndTone(t *testing.T) {
	pcm := make([]float32, 4800)
	for i := range pcm {
		pcm[i] = float32(.2 * math.Sin(2*math.Pi*440*float64(i)/24000))
	}
	records, e := AnalysisEncodeF32(pcm, 24000, 40)
	if e != nil {
		t.Fatal(e)
	}
	record, e := DecodeSpeechRecord(records[3*44 : 4*44])
	if e != nil {
		t.Fatal(e)
	}
	hz := 261.6255653005986 * math.Exp2(float64(record.Pitch)/2048)
	if math.Abs(hz-440)/440 > .015 || record.Unvoiced >= 10 {
		t.Fatalf("tone pitch=%f noise=%d", hz, record.Unvoiced)
	}
	for _, bank := range []SynthBank{SynthBankLow, SynthBankHigh} {
		s, e := NewSynth(bank, 350)
		if e != nil {
			t.Fatal(e)
		}
		total, e := s.SamplesForRecords(len(records)/44, 40)
		if e != nil {
			t.Fatal(e)
		}
		out := make([]float32, total-1)
		for i := range out {
			out[i] = 17
		}
		n, e := s.RenderInto(records, 40, out)
		if e != libpo.ErrBufferTooSmall || n != 0 {
			t.Fatalf("capacity: %d %v", n, e)
		}
		for _, v := range out {
			if v != 17 {
				t.Fatal("capacity failure wrote output")
			}
		}
		out, e = s.Render(records, 40)
		if e != nil || len(out) != total {
			t.Fatalf("tone render: %d %v", len(out), e)
		}
		energy := 0.0
		for _, v := range out {
			if math.IsNaN(float64(v)) || math.IsInf(float64(v), 0) {
				t.Fatal("nonfinite tone")
			}
			energy += float64(v) * float64(v)
		}
		if energy == 0 {
			t.Fatal("silent tone")
		}
		v, e := s.NewVoice(nil, 40)
		if e != nil || !v.Done() || v.SamplesRemaining() != 0 {
			t.Fatalf("empty voice: %v", e)
		}
		if empty, e := v.Render(0); e != nil || len(empty) != 0 {
			t.Fatalf("zero capacity: %v", e)
		}
		v.Close()
	}
}

func TestStructuralFieldMappingAndBounds(t *testing.T) {
	h, e := DecodeSpeechHeader([]byte{3, 0x12, 0x34, 40, 1, 2, 3, 4, 5})
	if e != nil || h.Slot != 3 || h.RecordCount != 0x1234 || h.RecordsPerSecond != 40 || h.Metadata[4] != 5 {
		t.Fatalf("header: %+v %v", h, e)
	}
	raw := make([]byte, 44)
	raw[0] = 0xff
	raw[1] = 0xff
	raw[2] = 9
	raw[3] = 8
	raw[4] = 0x80
	raw[13] = 0x7f
	raw[14] = 0x81
	raw[43] = 0xfe
	r, e := DecodeSpeechRecord(raw)
	if e != nil || r.Pitch != -1 || r.Gain != 9 || r.Unvoiced != 8 || r.LowLARRaw[0] != -128 || r.LowLARRaw[9] != 127 || r.HighLARRaw[0] != -127 || r.HighLARRaw[29] != -2 {
		t.Fatalf("record: %+v %v", r, e)
	}
	for _, n := range []int{1, 155, 156, 157, 255, 256} {
		raw := make([]byte, n)
		if n > 155 {
			raw[155] = 100
		}
		if _, e := DecodeSettings(raw); e != libpo.ErrParse {
			t.Fatalf("settings length %d: %v", n, e)
		}
	}
	for _, fn := range []func() error{
		func() error { _, e := EncodeSpeechRecord(nil); return e }, func() error { _, e := EncodePattern(nil); return e }, func() error { _, e := EncodeSettings(nil); return e },
		func() error { _, e := DecodeSpeechHeader(nil); return e }, func() error { _, e := DecodeSpeechRecord(nil); return e }, func() error { _, e := DecodePattern(nil); return e }, func() error { _, e := DecodeSettings(nil); return e },
	} {
		if e := fn(); e != libpo.ErrInvalidArg {
			t.Fatalf("nil codec: %v", e)
		}
	}
	if r, e := FrameParse(nil, nil); e != libpo.ErrInvalidArg || r.FinalState != 0x1d0f || r.FrameBytes != 0 {
		t.Fatalf("nil frame: %+v %v", r, e)
	}
	if _, e := DecodePattern(make([]byte, 182)); e != libpo.ErrParse {
		t.Fatalf("pattern length: %v", e)
	}
	if _, e := DecodeSpeechRecord(make([]byte, 43)); e != libpo.ErrParse {
		t.Fatalf("record length: %v", e)
	}
}

func TestHighBankCGoldenAndFailureLeavesSuffixUntouched(t *testing.T) {
	// Reference values from core/tests/test_po35_synth.c, test_lpc_and_audio_reference.
	expected := []float32{1.4369262355405543, -.64910183187392889, 1.61664410845125, 1.3063412766921949, 1.7871792960581685, 1.8673684316570527, 1.6799826725971196, 1.0262417990351707, 3.7744309323001666, -.32040913623302464, .66598932365892227, .19765609660850481, -.0253752139981289, -.55853933476887263, -1.4994542015329981, -2.0296141604321454}
	record := make([]byte, 44)
	record[1] = 6
	record[2] = 128
	for i := 0; i < 30; i++ {
		record[14+i] = byte(7 * (i%7 - 3))
	}
	s, e := NewSynth(SynthBankHigh, 350)
	if e != nil {
		t.Fatal(e)
	}
	if s.Bank() != SynthBankHigh || s.Seed() != 350 || s.SampleRate() != 24000 {
		t.Fatal("synth configuration changed")
	}
	v, e := s.NewVoice(record, 20)
	if e != nil {
		t.Fatal(e)
	}
	defer v.Close()
	got, e := v.Render(16)
	if e != nil {
		t.Fatal(e)
	}
	for i := range expected {
		if math.Abs(float64(got[i]-expected[i])) > 3e-7 {
			t.Fatalf("high-bank sample %d: %g != %g", i, got[i], expected[i])
		}
	}
	for i := 14; i < 44; i++ {
		record[i] = 127
	}
	out := make([]float32, 1200)
	for i := range out {
		out[i] = 17
	}
	n, e := s.RenderInto(record, 20, out)
	if n <= 0 || n >= 1200 || e != libpo.ErrRange {
		t.Fatalf("numerical prefix: %d %v", n, e)
	}
	for i := n; i < len(out); i++ {
		if out[i] != 17 {
			t.Fatalf("uncommitted suffix changed at %d", i)
		}
	}
}

func TestIndependentCallbacksAndSharedLifetimes(t *testing.T) {
	for id := 0; id < 12; id++ {
		t.Run(string(rune('a'+id)), func(t *testing.T) {
			t.Parallel()
			b, e := NewBuilder(PreambleByteCount + 16)
			if e != nil {
				t.Fatal(e)
			}
			if e = b.Append(&libpo.Packet{TagCode: 0x1234, PayloadLen: 1, Payload: [255]byte{byte(id)}}); e != nil {
				t.Fatal(e)
			}
			shared := *b
			b = nil
			runtime.GC()
			frame, e := shared.Finish()
			if e != nil {
				t.Fatal(e)
			}
			shared.Close()
			count := 0
			r, e := FrameParse(frame, func(p *libpo.Packet) bool {
				count++
				if p.Payload[0] != byte(id) {
					t.Errorf("wrong callback payload: %d", p.Payload[0])
				}
				return true
			})
			if e != nil || !r.Done || count != 1 {
				t.Fatalf("callback result: %+v %v", r, e)
			}
			s, _ := NewSynth(SynthBankLow, 350)
			v, e := s.NewVoice(make([]byte, 44), 80)
			if e != nil {
				t.Fatal(e)
			}
			copyOfVoice := *v
			v = nil
			runtime.GC()
			output, e := copyOfVoice.Render(100)
			if e != nil || len(output) != 100 {
				t.Fatalf("copied voice after GC: %d %v", len(output), e)
			}
			copyOfVoice.Close()
		})
	}
}

func TestSizingRejectsOverflowAndInvalidRates(t *testing.T) {
	for _, args := range []struct {
		count     int
		rate, fps uint32
	}{{maxInt, 24000, 40}, {1, 7999, 40}, {1, 192001, 40}, {1, 24000, 0}} {
		n, e := AnalysisSizeForPCM(args.count, args.rate, args.fps)
		if n != 0 || e != libpo.ErrRange {
			t.Fatalf("analysis size %+v: %d %v", args, n, e)
		}
	}
	s, _ := NewSynth(SynthBankLow, 0)
	for _, count := range []int{-1, maxInt} {
		if n, e := s.SamplesForRecords(count, 20); n != 0 || e != libpo.ErrRange {
			t.Fatalf("synth overflow: %d %v", n, e)
		}
	}
	if _, e := NewSynth(SynthBank(maxInt), 0); e != libpo.ErrRange {
		t.Fatalf("enum narrowing: %v", e)
	}
	var b *Builder
	if e := b.Reset(); e != libpo.ErrInvalidArg {
		t.Fatalf("nil reset: %v", e)
	}
	b.Close()
	var v *Voice
	if _, e := v.Render(-1); e != libpo.ErrRange {
		t.Fatalf("negative capacity: %v", e)
	}
	v.Close()
}
