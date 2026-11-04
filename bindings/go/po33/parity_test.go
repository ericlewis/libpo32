package po33

import (
	"bytes"
	"encoding/binary"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"

	libpo "github.com/ericlewis/libpo32/bindings/go"
)

func TestStandaloneCParity(t *testing.T) {
	compiler := strings.Fields(os.Getenv("CC"))
	if len(compiler) == 0 {
		compiler = []string{"cc"}
	}
	program := filepath.Join(t.TempDir(), "po33-parity")
	args := append(compiler[1:], "-std=c99", "-O2", "-I../internal/ccore/include", "-I../internal/ccore/src", "testdata/parity.c", "../internal/ccore/src/po33.c", "../internal/ccore/src/po33_data.c", "../internal/ccore/src/po33_audio.c", "-o", program)
	if runtime.GOOS == "linux" {
		args = append(args, "-lm")
	}
	if output, err := exec.Command(compiler[0], args...).CombinedOutput(); err != nil {
		t.Fatalf("standalone C build: %v\n%s", err, output)
	}
	want, err := exec.Command(program).Output()
	if err != nil {
		t.Fatalf("standalone C execution: %v", err)
	}
	var got []byte
	appendEncoded := func(data []byte, err error) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
		got = append(got, data...)
	}
	settingsData := make([]byte, SettingsBytes)
	for i := range settingsData {
		settingsData[i] = byte(i*17 + 11)
	}
	settingsData[383] = 2
	settings, err := DecodeSettings(settingsData)
	if err != nil {
		t.Fatal(err)
	}
	settings.Tempo, settings.Chain[127] = 119, 42
	appendEncoded(EncodeSettings(&settings))
	patternData := make([]byte, PatternBytes)
	for i := range patternData {
		patternData[i] = byte(i*29 + i/256)
	}
	pattern, err := DecodePattern(patternData)
	if err != nil {
		t.Fatal(err)
	}
	pattern.Events[15][1].ControlRaw ^= 15
	pattern.Events[7][5].EventRaw[2] = 0xab
	appendEncoded(EncodePattern(&pattern))
	appendEncoded(EncodeSoundHeader(&SoundHeader{RangeStartRaw: 0x12345678, RangeEndRaw: 0xffffffff, SourceSelector: 120}))
	pcm := make([]float32, 257)
	for i := range pcm {
		pcm[i] = float32(i%29-14) / 16
	}
	for _, rate := range []uint32{23438, 44100, 8000} {
		appendEncoded(EncodeSamples(pcm, rate))
	}
	phases := []byte{0, 0, 0, 1, 0, 3, 0, 2, 1, 2, 2, 3, 3, 1}
	for _, parity := range []uint{0, 1} {
		appendEncoded(PackSymbols(phases, parity))
	}
	frame := makeFrame(t)
	got = append(got, frame...)
	appendWords := func(values ...uint64) {
		for _, value := range values {
			got = binary.LittleEndian.AppendUint64(got, value)
		}
	}
	wordBool := func(value bool) uint64 {
		if value {
			return 1
		}
		return 0
	}
	appendResult := func(value DecodeResult) {
		appendWords(uint64(value.PacketCount), uint64(value.FrameBytes), wordBool(value.Done), uint64(value.FinalState), wordBool(value.Stopped))
	}
	result, err := FrameParse(frame, func(packet *libpo.Packet) bool {
		appendWords(uint64(packet.TagCode), uint64(packet.Offset), uint64(packet.PayloadLen))
		got = append(got, packet.Payload[:packet.PayloadLen]...)
		appendWords(uint64(packet.Trailer.Lo), uint64(packet.Trailer.Hi), uint64(packet.Trailer.State), wordBool(packet.Trailer.MatchesState))
		return true
	})
	if err != nil {
		t.Fatal(err)
	}
	appendResult(result)
	result, err = FrameParse(frame[:len(frame)-1], nil)
	if !errors.Is(err, libpo.ErrFrame) {
		t.Fatalf("truncated C fixture: %v", err)
	}
	appendResult(result)
	if !bytes.Equal(got, want) {
		if len(got) != len(want) {
			t.Fatalf("C parity size: Go %d, C %d", len(got), len(want))
		}
		for i := range got {
			if got[i] != want[i] {
				t.Fatalf("C parity byte %d: Go %02x, C %02x", i, got[i], want[i])
			}
		}
	}
}

func TestCallbacksPermitReentryPanicAndConcurrentParses(t *testing.T) {
	frame := makeFrame(t)
	result, err := FrameParse(frame, func(p *libpo.Packet) bool {
		runtime.GC()
		inner, err := FrameParse(frame, nil)
		if err != nil || !inner.Done {
			t.Fatalf("nested parse: %+v, %v", inner, err)
		}
		p.Payload[0] = 99
		return true
	})
	if err != nil || !result.Done {
		t.Fatalf("outer parse: %+v, %v", result, err)
	}
	func() {
		defer func() {
			if recover() != "callback panic" {
				t.Error("callback panic did not propagate")
			}
		}()
		_, _ = FrameParse(frame, func(*libpo.Packet) bool { panic("callback panic") })
	}()
	var workers sync.WaitGroup
	for i := 0; i < 16; i++ {
		workers.Add(1)
		go func() {
			defer workers.Done()
			count := 0
			parsed, err := FrameParse(frame, func(p *libpo.Packet) bool { count++; return true })
			if err != nil || !parsed.Done || count != 4 {
				t.Errorf("concurrent parse: %+v, count %d, %v", parsed, count, err)
			}
		}()
	}
	workers.Wait()
}
