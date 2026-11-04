package po33

import (
	"bytes"
	"errors"
	"math"
	"runtime"
	"testing"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

func TestSettingsPreserveEveryByte(t *testing.T) {
	wire := make([]byte, SettingsBytes)
	for i := range wire {
		wire[i] = byte(i*17 + 11)
	}
	wire[383], wire[384], wire[385] = 2, 140, 18
	value, err := DecodeSettings(wire)
	if err != nil {
		t.Fatal(err)
	}
	if value.ChainLength != 2 || value.Tempo != 140 || value.SwingRaw != 18 || value.Chain[127] != wire[382] || value.StateRaw[0] != wire[386] || value.SuffixRaw[107] != wire[509] {
		t.Fatalf("incorrect settings fields: %+v", value)
	}
	encoded, err := EncodeSettings(&value)
	if err != nil || !bytes.Equal(encoded, wire) {
		t.Fatalf("settings round trip: %v", err)
	}
	value.Tempo = 119
	wire[384] = 119
	encoded, err = EncodeSettings(&value)
	if err != nil || !bytes.Equal(encoded, wire) {
		t.Fatalf("tempo edit changed unrelated bytes: %v", err)
	}
	value.ChainLength = 129
	if out, err := EncodeSettings(&value); out != nil || !errors.Is(err, libpo.ErrRange) {
		t.Fatalf("invalid chain: %x, %v", out, err)
	}
	wire[383] = 129
	if _, err := DecodeSettings(wire); !errors.Is(err, libpo.ErrRange) {
		t.Fatalf("invalid chain decode: %v", err)
	}
}

func TestPatternAndSoundHeaderPreserveRawData(t *testing.T) {
	wire := make([]byte, PatternBytes)
	for i := range wire {
		wire[i] = byte(i*29 + i/256)
	}
	value, err := DecodePattern(wire)
	if err != nil {
		t.Fatal(err)
	}
	if value.Events[15][1].EventRaw[0] != wire[723] || value.Events[15][1].ControlRaw != wire[1009] || !bytes.Equal(value.FooterRaw[:], wire[1024:]) {
		t.Fatal("pattern layout differs from the C record")
	}
	encoded, err := EncodePattern(&value)
	if err != nil || !bytes.Equal(encoded, wire) {
		t.Fatalf("pattern round trip: %v", err)
	}
	value.Events[15][1].ControlRaw ^= 15
	wire[1009] ^= 15
	encoded, err = EncodePattern(&value)
	if err != nil || !bytes.Equal(encoded, wire) {
		t.Fatalf("event edit changed unrelated bytes: %v", err)
	}
	headerBytes := []byte{0x78, 0x56, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff, 120}
	header, err := DecodeSoundHeader(headerBytes)
	if err != nil || header.RangeStartRaw != 0x12345678 || header.RangeEndRaw != 0xffffffff || header.SourceSelector != 120 {
		t.Fatalf("header decode: %+v, %v", header, err)
	}
	encoded, err = EncodeSoundHeader(&header)
	if err != nil || !bytes.Equal(encoded, headerBytes) {
		t.Fatalf("header round trip: %x, %v", encoded, err)
	}
}

func TestRecordLengthAndNilValidation(t *testing.T) {
	for _, n := range []int{0, SettingsBytes - 1, SettingsBytes + 1} {
		if _, err := DecodeSettings(make([]byte, n)); !errors.Is(err, libpo.ErrFrame) {
			t.Errorf("settings length %d: %v", n, err)
		}
	}
	for _, n := range []int{0, PatternBytes - 1, PatternBytes + 1} {
		if _, err := DecodePattern(make([]byte, n)); !errors.Is(err, libpo.ErrFrame) {
			t.Errorf("pattern length %d: %v", n, err)
		}
	}
	for _, n := range []int{0, SoundHeaderBytes - 1, SoundHeaderBytes + 1} {
		if _, err := DecodeSoundHeader(make([]byte, n)); !errors.Is(err, libpo.ErrFrame) {
			t.Errorf("header length %d: %v", n, err)
		}
	}
	if _, err := EncodeSettings(nil); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("nil settings: %v", err)
	}
	if _, err := EncodePattern(nil); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("nil pattern: %v", err)
	}
	if _, err := EncodeSoundHeader(nil); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("nil header: %v", err)
	}
}

func TestSampleQuantizationAndResampling(t *testing.T) {
	for _, tc := range []struct {
		code   byte
		sample int16
	}{{0, 0}, {1, 8}, {15, 120}, {16, 132}, {31, 372}, {32, 396}, {63, 1884}, {64, 1980}, {127, 32124}, {128, -32124}, {254, -8}, {255, 0}} {
		if got := DecodeSample(tc.code); got != tc.sample {
			t.Errorf("decode %d: %d, want %d", tc.code, got, tc.sample)
		}
	}
	for _, tc := range []struct {
		sample int16
		code   byte
	}{{-32768, 128}, {32767, 127}, {4, 0}, {-4, 0}, {5, 1}, {-5, 254}} {
		if got := EncodeSample(tc.sample); got != tc.code {
			t.Errorf("encode %d: %d, want %d", tc.sample, got, tc.code)
		}
	}
	for code := 0; code < 256; code++ {
		want := byte(code)
		if code == 255 {
			want = 0
		}
		if got := EncodeSample(DecodeSample(byte(code))); got != want {
			t.Fatalf("code %d round trip: %d", code, got)
		}
	}
	pcm := []float32{0, 8.0 / 32768, -8.0 / 32768, 1, -1, 4.0 / 32768, -4.0 / 32768}
	want := []byte{0, 1, 254, 127, 128, 0, 0}
	got, err := EncodeSamples(pcm, 23438)
	if err != nil || !bytes.Equal(got, want) {
		t.Fatalf("native PCM: %v, %v", got, err)
	}
	for _, rate := range []uint32{8000, 23438, 44100, 48000, 192000} {
		count, err := SampleCountForPCM(int(rate), rate)
		if err != nil || count != 23438 {
			t.Errorf("one second %d: %d, %v", rate, count, err)
		}
		got, err := EncodeSamples(make([]float32, rate/100), rate)
		if err != nil || len(got) != 235 && rate != 23438 || rate == 23438 && len(got) != 234 {
			t.Fatalf("silence %d: %d, %v", rate, len(got), err)
		}
		for _, code := range got {
			if code != 0 {
				t.Fatalf("silence %d contains %d", rate, code)
			}
		}
	}
	for _, tc := range []struct {
		count int
		rate  uint32
		want  int
	}{{0, 48000, 0}, {333, 23438, 333}, {1, 192000, 1}, {96000, 48000, 46875}, {1073741823, 48000, 524288000}} {
		got, err := SampleCountForPCM(tc.count, tc.rate)
		if err != nil || got != tc.want {
			t.Errorf("count %+v: %d,%v", tc, got, err)
		}
	}
	for _, tc := range []struct {
		count int
		rate  uint32
	}{{-1, 48000}, {1, 7999}, {1, 192001}, {int(^uint(0) >> 1), 48000}} {
		if n, err := SampleCountForPCM(tc.count, tc.rate); n != 0 || !errors.Is(err, libpo.ErrRange) {
			t.Errorf("invalid count %+v: %d,%v", tc, n, err)
		}
	}
	for _, value := range []float32{float32(math.NaN()), float32(math.Inf(1)), float32(math.Inf(-1)), 1.01, -1.01} {
		if out, err := EncodeSamples([]float32{0, value}, 23438); out != nil || !errors.Is(err, libpo.ErrRange) {
			t.Errorf("invalid PCM %v: %x,%v", value, out, err)
		}
	}
	if out, err := EncodeSamples(nil, 48000); err != nil || len(out) != 0 {
		t.Fatalf("empty PCM: %x,%v", out, err)
	}
}

func TestPackSymbolsValidationAndParity(t *testing.T) {
	phases := []byte{0, 0, 0, 1, 0, 3, 0, 2, 1, 2, 2, 3, 3, 1}
	for parity, want := range [][]byte{{0x21, 0x43, 0x65}, {0x32, 0x54}} {
		got, err := PackSymbols(phases, uint(parity))
		if err != nil || !bytes.Equal(got, want) {
			t.Fatalf("parity %d: %x,%v", parity, got, err)
		}
	}
	for _, p := range [][]byte{nil, {0, 0}} {
		if got, err := PackSymbols(p, 0); err != nil || len(got) != 0 {
			t.Errorf("empty differential output: %x,%v", got, err)
		}
	}
	for _, parity := range []uint{2, ^uint(0)} {
		if _, err := PackSymbols(phases, parity); !errors.Is(err, libpo.ErrInvalidArg) {
			t.Errorf("invalid parity %d: %v", parity, err)
		}
	}
	if _, err := PackSymbols([]byte{0}, 0); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("unpaired phase: %v", err)
	}
	for _, phases := range [][]byte{{0, 4}, {0, 0, 0, 4, 0, 0}} {
		if _, err := PackSymbols(phases, 0); !errors.Is(err, libpo.ErrRange) {
			t.Errorf("phase range: %v", err)
		}
	}
}

func makeFrame(t *testing.T) []byte {
	t.Helper()
	b, err := NewBuilder(8192)
	if err != nil {
		t.Fatal(err)
	}
	defer b.Close()
	payload := make([]byte, 255)
	for i := range payload {
		payload[i] = byte(i * 17)
	}
	if off, err := b.AppendRawPacket(0x1234, payload); err != nil || off != 4101 {
		t.Fatalf("append: %d,%v", off, err)
	}
	packet := libpo.Packet{TagCode: TagSoundSelect, PayloadLen: 1, Payload: [255]byte{7}}
	if err := b.Append(&packet); err != nil {
		t.Fatal(err)
	}
	frame, err := b.Finish()
	if err != nil {
		t.Fatal(err)
	}
	return frame
}

func TestFrameParseOwnsPacketsAndReportsProgress(t *testing.T) {
	frame := makeFrame(t)
	if !bytes.Equal(frame[4096:4101], []byte{0x6a, 0x9a, 0x34, 0xe2, 0x48}) {
		t.Fatalf("C golden BEGIN mismatch: %x", frame[4096:4101])
	}
	var saved []*libpo.Packet
	result, err := FrameParse(append(frame, 0xaa), func(p *libpo.Packet) bool { saved = append(saved, p); return true })
	if err != nil || !result.Done || result.Stopped || result.PacketCount != 4 || result.FrameBytes != 4372 {
		t.Fatalf("complete: %+v,%v", result, err)
	}
	if len(saved) != 4 || saved[0] == saved[1] || saved[0].TagCode != TagBegin || saved[1].Offset != 4101 || saved[1].PayloadLen != 255 || saved[2].Offset != 4361 || saved[2].Payload[0] != 7 || saved[3].Offset != 4367 {
		t.Fatalf("packets: %+v", saved)
	}
	for _, p := range saved {
		if !p.Trailer.MatchesState {
			t.Fatal("missing trailer status")
		}
	}
	for i := range frame {
		frame[i] = 0
	}
	runtime.GC()
	for i, b := range saved[1].Payload {
		if b != byte(i*17) {
			t.Fatalf("retained packet byte %d changed: %d", i, b)
		}
	}
	frame = makeFrame(t)
	validated, err := FrameParse(frame, nil)
	if err != nil || validated != result {
		t.Fatalf("nil callback: %+v,%v", validated, err)
	}
	for _, stopAfter := range []int{1, 2, 4} {
		n := 0
		stopped, err := FrameParse(frame, func(*libpo.Packet) bool { n++; return n < stopAfter })
		if err != nil || !stopped.Stopped || stopped.Done || stopped.PacketCount != stopAfter {
			t.Fatalf("stop %d: %+v,%v", stopAfter, stopped, err)
		}
		if stopped.FrameBytes != saved[stopAfter-1].Offset+saved[stopAfter-1].PayloadLen+5 {
			t.Fatalf("stop offset: %+v", stopped)
		}
	}
	partial, err := FrameParse(frame[:len(frame)-1], nil)
	if !errors.Is(err, libpo.ErrFrame) || partial.Done || partial.Stopped || partial.PacketCount != 3 || partial.FrameBytes != 4367 || partial.FinalState == InitialState {
		t.Fatalf("partial: %+v,%v", partial, err)
	}
	frame[4365] ^= 1
	bad, err := FrameParse(frame, nil)
	if !errors.Is(err, libpo.ErrFrame) || bad.PacketCount != 2 || bad.FrameBytes != 4361 {
		t.Fatalf("bad trailer: %+v,%v", bad, err)
	}
	if empty, err := FrameParse(nil, nil); !errors.Is(err, libpo.ErrInvalidArg) || empty.FinalState != InitialState {
		t.Fatalf("empty frame: %+v,%v", empty, err)
	}
}

func TestBuilderValidationLifetimeAndReset(t *testing.T) {
	for _, capacity := range []int{-1, 0, 4100} {
		if b, err := NewBuilder(capacity); b != nil || err == nil {
			t.Fatalf("capacity %d: %v,%v", capacity, b, err)
		}
	}
	b, err := NewBuilder(4112)
	if err != nil {
		t.Fatal(err)
	}
	defer b.Close()
	for _, tag := range []uint16{TagBegin, TagEnd} {
		if _, err := b.AppendRawPacket(tag, nil); !errors.Is(err, libpo.ErrInvalidArg) {
			t.Errorf("reserved %x: %v", tag, err)
		}
	}
	if _, err := b.AppendRawPacket(0x1234, make([]byte, 256)); !errors.Is(err, libpo.ErrRange) {
		t.Errorf("oversize: %v", err)
	}
	for _, p := range []*libpo.Packet{nil, {PayloadLen: -1}, {PayloadLen: 256}} {
		if err := b.Append(p); !errors.Is(err, libpo.ErrInvalidArg) {
			t.Errorf("invalid packet: %v", err)
		}
	}
	if _, err := b.AppendRawPacket(0x1234, []byte{42}); err != nil {
		t.Fatal(err)
	}
	if _, err := b.AppendRawPacket(0x1234, []byte{43}); !errors.Is(err, libpo.ErrBufferTooSmall) {
		t.Fatalf("full buffer: %v", err)
	}
	runtime.GC()
	first, err := b.Finish()
	if err != nil || len(first) != 4112 {
		t.Fatalf("finish: %d,%v", len(first), err)
	}
	again, err := b.Finish()
	if err != nil || !bytes.Equal(first, again) {
		t.Fatalf("repeated finish: %v", err)
	}
	if _, err := b.AppendRawPacket(0x1234, nil); !errors.Is(err, libpo.ErrFrame) {
		t.Errorf("append after finish: %v", err)
	}
	if err := b.Reset(); err != nil {
		t.Fatal(err)
	}
	runtime.GC()
	second, err := b.Finish()
	if err != nil || len(second) != 4106 {
		t.Fatalf("reset finish: %d,%v", len(second), err)
	}
	if _, err := FrameParse(first, nil); err != nil {
		t.Fatalf("reset mutated returned bytes: %v", err)
	}
	b.Close()
	b.Close()
	if _, err := b.Finish(); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("closed finish: %v", err)
	}
	if _, err := b.AppendRawPacket(0x1234, nil); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("closed append: %v", err)
	}
	if err := b.Reset(); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("closed reset: %v", err)
	}
	var empty Builder
	if err := empty.Reset(); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("zero builder: %v", err)
	}
	var absent *Builder
	absent.Close()
	if _, err := absent.Finish(); !errors.Is(err, libpo.ErrInvalidArg) {
		t.Errorf("nil builder: %v", err)
	}
	tiny, err := NewBuilder(4101)
	if err != nil {
		t.Fatal(err)
	}
	defer tiny.Close()
	if _, err := tiny.Finish(); !errors.Is(err, libpo.ErrBufferTooSmall) {
		t.Errorf("no room for END: %v", err)
	}
}
