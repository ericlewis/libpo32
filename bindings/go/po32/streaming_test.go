package po32

import (
	"math"
	"reflect"
	"runtime"
	"testing"
)

func streamingFrame(t *testing.T, instruments ...uint8) []byte {
	t.Helper()
	b := NewBuilder(2048)
	defer b.Close()
	for _, instrument := range instruments {
		p, err := EncodeResetPacket(&ResetPacket{Instrument: instrument})
		if err != nil {
			t.Fatal(err)
		}
		if err := b.Append(p); err != nil {
			t.Fatal(err)
		}
	}
	frame, err := b.Finish()
	if err != nil {
		t.Fatal(err)
	}
	return frame
}

func streamingAudio(t *testing.T, frame []byte) []float32 {
	t.Helper()
	audio, err := RenderDPSKF32(frame, 44100)
	if err != nil {
		t.Fatal(err)
	}
	return audio
}

func TestDemodulatorChunkParity(t *testing.T) {
	frame := streamingFrame(t, 1, 3, 5)
	audio := streamingAudio(t, frame)
	var want []Packet
	wantTail, err := FrameParse(frame, func(p *Packet) bool {
		want = append(want, *p)
		return true
	})
	if err != nil {
		t.Fatal(err)
	}
	for _, chunkSize := range []int{1, 257, 1024, len(audio)} {
		d, err := NewDemodulator(44100)
		if err != nil {
			t.Fatal(err)
		}
		var got []*Packet
		callback := func(p *Packet) bool {
			got = append(got, p)
			if d.PacketCount() != len(got) {
				t.Fatalf("callback count = %d, want %d", d.PacketCount(), len(got))
			}
			runtime.GC()
			return true
		}
		for start := 0; start < len(audio); start += chunkSize {
			if err := d.Push(audio[start:min(start+chunkSize, len(audio))], callback); err != nil {
				t.Fatal(err)
			}
		}
		if err := d.Flush(callback); err != nil {
			t.Fatal(err)
		}
		if !d.Done() || d.Stopped() || d.PacketCount() != 3 {
			t.Fatalf("chunk size %d: done=%v stopped=%v packets=%d", chunkSize, d.Done(), d.Stopped(), d.PacketCount())
		}
		if tail, ok := d.Tail(); !ok || tail != wantTail {
			t.Fatalf("tail = %+v, %v; want %+v, true", tail, ok, wantTail)
		}
		if err := d.Flush(callback); err != nil {
			t.Fatal(err)
		}
		d.Desync()
		if !d.Done() || d.PacketCount() != 3 {
			t.Fatal("desync changed completed state")
		}
		d.Close()
		d.Close()
		runtime.GC()
		if len(got) != len(want) {
			t.Fatalf("got %d packets, want %d", len(got), len(want))
		}
		for i, p := range got {
			if *p != want[i] {
				t.Fatalf("packet %d changed or differs from frame parse", i)
			}
		}
	}
}

func TestDemodulatorExactLengthNeedsFlush(t *testing.T) {
	// This longer fixture leaves the final symbol pending at exactly the
	// rendered length; short frames can finish within their rounded length.
	b := NewBuilder(1 << 14)
	defer b.Close()
	for i := 0; i < 32; i++ {
		pattern := NewPattern(uint8(i))
		if err := pattern.SetTrigger(uint8(i%16), uint8(1+i%16), 1); err != nil {
			t.Fatal(err)
		}
		packet, err := EncodePatternPacket(pattern)
		if err != nil {
			t.Fatal(err)
		}
		if err := b.Append(packet); err != nil {
			t.Fatal(err)
		}
	}
	frame, err := b.Finish()
	if err != nil {
		t.Fatal(err)
	}
	d, err := NewDemodulator(44100)
	if err != nil {
		t.Fatal(err)
	}
	defer d.Close()
	if err := d.Push(streamingAudio(t, frame), nil); err != nil {
		t.Fatal(err)
	}
	if d.Done() {
		t.Fatal("fixture should leave the last symbol pending")
	}
	if tail, ok := d.Tail(); ok || tail != (FinalTail{}) {
		t.Fatal("unfinished stream exposed a decoded tail")
	}
	if err := d.Flush(nil); err != nil {
		t.Fatal(err)
	}
	if !d.Done() || d.Stopped() || d.PacketCount() != 32 {
		t.Fatalf("flush: done=%v stopped=%v packets=%d", d.Done(), d.Stopped(), d.PacketCount())
	}
	if tail, ok := d.Tail(); !ok || tail.MarkerC3 != 0xc3 || tail.Marker71 != 0x71 {
		t.Fatal("flush failed to decode the final tail")
	}
}

func pendingPacketAudio(t *testing.T) []float32 {
	t.Helper()
	b := NewBuilder(1 << 15)
	defer b.Close()
	for i := 0; i < 64; i++ {
		pattern := NewPattern(uint8(i % 32))
		if err := pattern.SetTrigger(uint8(i%16), uint8(1+i%16), 1); err != nil {
			t.Fatal(err)
		}
		packet, err := EncodePatternPacket(pattern)
		if err != nil {
			t.Fatal(err)
		}
		if err := b.Append(packet); err != nil {
			t.Fatal(err)
		}
	}
	frame, err := b.Finish()
	if err != nil {
		t.Fatal(err)
	}
	// Remove the tail so the pending symbol completes a packet in Flush.
	return streamingAudio(t, frame[:len(frame)-FinalTailBytes])
}

func TestDemodulatorFlushDeliversAndStopsOnFinalPacket(t *testing.T) {
	audio := pendingPacketAudio(t)
	d, err := NewDemodulator(44100)
	if err != nil {
		t.Fatal(err)
	}
	defer d.Close()
	if err := d.Push(audio, nil); err != nil {
		t.Fatal(err)
	}
	if d.PacketCount() != 63 {
		t.Fatalf("expected one pending packet, have %d", d.PacketCount())
	}
	var last *Packet
	if err := d.Flush(func(p *Packet) bool {
		last = p
		if d.PacketCount() != 64 {
			t.Fatal("flush callback saw an uncommitted packet")
		}
		runtime.GC()
		return false
	}); err != nil {
		t.Fatal(err)
	}
	if last == nil || last.TagCode != TagPattern || !d.Stopped() || d.Done() || d.PacketCount() != 64 {
		t.Fatal("flush did not deliver and commit the stopping packet")
	}
	if err := d.Flush(func(*Packet) bool { t.Fatal("flush resumed a stopped decoder"); return true }); err != nil {
		t.Fatal(err)
	}
	d.Close()
	if packet, err := DecodePatternPacket(last.Payload[:last.PayloadLen]); err != nil || packet.PatternNumber != 31 {
		t.Fatal("flush packet did not outlive the decoder")
	}
}

func TestDemodulatorStopIsTerminal(t *testing.T) {
	audio := streamingAudio(t, streamingFrame(t, 1, 2, 3))
	d, err := NewDemodulator(44100)
	if err != nil {
		t.Fatal(err)
	}
	defer d.Close()
	calls := 0
	callback := func(*Packet) bool {
		calls++
		return false
	}
	if err := d.Push(audio, callback); err != nil {
		t.Fatal(err)
	}
	if !d.Stopped() || d.Done() || calls != 1 || d.PacketCount() != 1 {
		t.Fatalf("stopped=%v done=%v calls=%d packets=%d", d.Stopped(), d.Done(), calls, d.PacketCount())
	}
	d.Desync()
	if err := d.Push(audio, callback); err != nil {
		t.Fatal(err)
	}
	if err := d.Flush(callback); err != nil {
		t.Fatal(err)
	}
	if !d.Stopped() || d.Done() || calls != 1 || d.PacketCount() != 1 {
		t.Fatal("desync, push, or flush resumed a terminal stop")
	}
	if _, ok := d.Tail(); ok {
		t.Fatal("stopped stream reported a tail")
	}
}

func TestDemodulatorDesyncResetsCountOnReacquisition(t *testing.T) {
	frame := streamingFrame(t, 1, 2, 3)
	d, err := NewDemodulator(44100)
	if err != nil {
		t.Fatal(err)
	}
	defer d.Close()
	if err := d.Push(streamingAudio(t, frame[:len(frame)-FinalTailBytes]), nil); err != nil {
		t.Fatal(err)
	}
	if err := d.Flush(nil); err != nil {
		t.Fatal(err)
	}
	if d.PacketCount() != 3 || d.Done() {
		t.Fatal("partial frame should have three packets and no tail")
	}
	d.Desync()
	if d.PacketCount() != 3 {
		t.Fatal("desync must preserve count until reacquisition")
	}
	if err := d.Push(streamingAudio(t, streamingFrame(t, 7)), nil); err != nil {
		t.Fatal(err)
	}
	if err := d.Flush(nil); err != nil {
		t.Fatal(err)
	}
	if !d.Done() || d.PacketCount() != 1 {
		t.Fatalf("reacquisition: done=%v packets=%d", d.Done(), d.PacketCount())
	}
}

func TestDemodulatorCloseFromCallback(t *testing.T) {
	d, err := NewDemodulator(44100)
	if err != nil {
		t.Fatal(err)
	}
	calls := 0
	err = d.Push(streamingAudio(t, streamingFrame(t, 1, 2)), func(*Packet) bool {
		calls++
		if err := d.Push([]float32{0}, nil); err != ErrInvalidArg {
			t.Errorf("recursive Push = %v", err)
		}
		if err := d.Flush(nil); err != ErrInvalidArg {
			t.Errorf("recursive Flush = %v", err)
		}
		d.Close()
		d.Close()
		runtime.GC()
		return true
	})
	if err != nil || calls != 1 {
		t.Fatalf("close from callback: %d calls, %v", calls, err)
	}
	if err := d.Push([]float32{0}, nil); err != ErrInvalidArg {
		t.Fatalf("push after close = %v", err)
	}
}

func TestDemodulatorCallbackUnwindCloses(t *testing.T) {
	for _, operation := range []string{"Push", "Flush"} {
		for _, exit := range []string{"panic", "Goexit"} {
			t.Run(operation+"/"+exit, func(t *testing.T) {
				d, err := NewDemodulator(44100)
				if err != nil {
					t.Fatal(err)
				}
				defer d.Close()
				audio := streamingAudio(t, streamingFrame(t, 1, 2, 3))
				call := func(fn func(*Packet) bool) error { return d.Push(audio, fn) }
				if operation == "Flush" {
					if err := d.Push(pendingPacketAudio(t), nil); err != nil {
						t.Fatal(err)
					}
					if d.PacketCount() != 63 {
						t.Fatalf("expected one pending packet, have %d", d.PacketCount())
					}
					call = d.Flush
				}

				called, returned := false, false
				var recovered any
				done := make(chan struct{})
				go func() {
					defer close(done)
					defer func() { recovered = recover() }()
					_ = call(func(*Packet) bool {
						called = true
						if exit == "Goexit" {
							runtime.Goexit()
						}
						panic("callback panic")
					})
					returned = true
				}()
				<-done
				if !called || returned {
					t.Fatalf("callback unwind: called=%v returned=%v", called, returned)
				}
				if exit == "panic" && recovered != "callback panic" {
					t.Fatalf("panic changed during cleanup: %v", recovered)
				}
				if exit == "Goexit" && recovered != nil {
					t.Fatalf("Goexit panicked: %v", recovered)
				}
				// Empty input must also reject the closed receiver without entering C.
				if err := d.Push(nil, nil); err != ErrInvalidArg {
					t.Fatalf("Push after %s = %v, want ErrInvalidArg", exit, err)
				}
				if err := d.Push(audio, nil); err != ErrInvalidArg {
					t.Fatalf("nonempty Push after %s = %v, want ErrInvalidArg", exit, err)
				}
				if err := d.Flush(nil); err != ErrInvalidArg {
					t.Fatalf("Flush after %s = %v, want ErrInvalidArg", exit, err)
				}
				d.Desync()
				if d.Done() || d.Stopped() || d.PacketCount() != 0 {
					t.Fatal("closed decoder reported progress")
				}
				d.Close()
				d.Close()
			})
		}
	}
}

func TestDemodulatorInvalidAndClosed(t *testing.T) {
	for _, rate := range []float32{0, -1, float32(math.NaN()), float32(math.Inf(1)), float32(math.Inf(-1))} {
		if d, err := NewDemodulator(rate); err != ErrInvalidArg || d != nil {
			t.Fatalf("NewDemodulator(%v) = %v, %v", rate, d, err)
		}
	}
	d, err := NewDemodulator(44100)
	if err != nil {
		t.Fatal(err)
	}
	if err := d.Push(nil, nil); err != nil {
		t.Fatalf("empty push = %v", err)
	}
	if err := d.Flush(nil); err != nil || d.Done() {
		t.Fatalf("flush before sync = %v, done=%v", err, d.Done())
	}
	d.Close()
	for _, d := range []*Demodulator{d, nil} {
		d.Close()
		d.Desync()
		if d.Done() || d.Stopped() || d.PacketCount() != 0 {
			t.Fatal("closed decoder reported progress")
		}
		if _, ok := d.Tail(); ok {
			t.Fatal("closed decoder reported a tail")
		}
		if err := d.Push(nil, nil); err != ErrInvalidArg {
			t.Fatalf("closed Push = %v", err)
		}
		if err := d.Flush(nil); err != ErrInvalidArg {
			t.Fatalf("closed Flush = %v", err)
		}
	}
}

func TestSynthVoiceChunksResetAndOwnedParams(t *testing.T) {
	params := PatchParams{OscWave: 0.5, OscFreq: 0.24, OscAtk: 0.1, OscDcy: 0.6,
		ModMode: 0.5, ModRate: 0.15, ModAmt: 0.54, NFilFrq: 0.4, NFilQ: 0.2,
		NEnvDcy: 0.3, Mix: 0.35, DistAmt: 0.2, EQFreq: 0.4, EQGain: 0.55, Level: 0.8}
	s := NewSynth(44100)
	want, err := s.Render(&params, 100, 0.1)
	if err != nil {
		t.Fatal(err)
	}
	v, err := s.NewVoice(&params, 100, 0.1)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	params = PatchParams{} // the voice must retain no pointer into params
	runtime.GC()
	if v.SamplesRemaining() != 4410 || v.Done() {
		t.Fatalf("new voice remaining=%d done=%v", v.SamplesRemaining(), v.Done())
	}
	if out, err := v.RenderF32(0); err != nil || len(out) != 0 || v.SamplesRemaining() != 4410 {
		t.Fatal("zero render changed voice state")
	}
	if _, err := v.RenderF32(-1); err != ErrInvalidArg {
		t.Fatalf("negative RenderF32 = %v", err)
	}
	for _, chunk := range []int{1, 257, 1024, 10000} {
		v.Reset()
		var got []float32
		for !v.Done() {
			part, err := v.RenderF32(chunk)
			if err != nil {
				t.Fatal(err)
			}
			got = append(got, part...)
		}
		if v.SamplesRemaining() != 0 || !reflect.DeepEqual(got, want) {
			t.Fatalf("chunk size %d differs from one-shot synthesis", chunk)
		}
		if out, err := v.Render(8); err != nil || len(out) != 0 {
			t.Fatal("exhausted voice returned samples")
		}
	}
	v.Reset()
	got, err := v.Render(5000)
	if err != nil || !reflect.DeepEqual(got, want) {
		t.Fatal("Render alias or reset changed synthesis")
	}
}

func TestSynthVoiceInertAndClosed(t *testing.T) {
	params := PatchParams{}
	for _, duration := range []float32{0, -1, float32(math.NaN()), float32(math.Inf(1)), float32(math.Inf(-1)), math.MaxFloat32} {
		v, err := NewSynthVoice(44100, &params, 100, duration)
		if err != nil {
			t.Fatal(err)
		}
		if !v.Done() || v.SamplesRemaining() != 0 {
			t.Fatalf("duration %v created a live voice", duration)
		}
		if out, err := v.RenderF32(8); err != nil || len(out) != 0 {
			t.Fatalf("inert render = %d samples, %v", len(out), err)
		}
		v.Close()
	}
	if v, err := NewSynthVoice(44100, nil, 100, 1); v != nil || err != ErrInvalidArg {
		t.Fatalf("nil params = %v, %v", v, err)
	}
	var s *Synth
	if v, err := s.NewVoice(&params, 100, 1); v != nil || err != ErrInvalidArg {
		t.Fatalf("nil synth = %v, %v", v, err)
	}
	v, err := NewSynthVoice(0, &params, 100, 1)
	if err != nil || !v.Done() {
		t.Fatalf("zero sample rate should create an inert voice: %v", err)
	}
	v.Close()
	for _, v := range []*SynthVoice{v, nil} {
		v.Close()
		v.Reset()
		if !v.Done() || v.SamplesRemaining() != 0 {
			t.Fatal("closed voice should be done")
		}
		if _, err := v.RenderF32(1); err != ErrInvalidArg {
			t.Fatalf("closed RenderF32 = %v", err)
		}
	}
}

func TestSynthVoiceRejectsCountThatCannotFitInt(t *testing.T) {
	// The C duration helper may accept counts above Go's signed int range.
	// Refuse them instead of exposing a wrapped or inexact remaining count.
	if ^uint(0)>>63 == 0 {
		t.Skip("64-bit size_t boundary")
	}
	v, err := NewSynthVoice(44100, &PatchParams{}, 100, 3e14)
	if v != nil {
		v.Close()
	}
	if v != nil || err != ErrRange {
		t.Fatalf("unrepresentable voice = %v, %v; want nil, ErrRange", v, err)
	}
}

func TestModulatorOwnsFrameAndResetsAfterGC(t *testing.T) {
	frame := streamingFrame(t, 1, 4)
	want := streamingAudio(t, frame)
	m := NewModulator(frame, 44100)
	for i := range frame {
		frame[i] = 0
	}
	runtime.GC()
	for pass := 0; pass < 2; pass++ {
		var got []float32
		for !m.Done() {
			chunk, err := m.RenderF32(1001)
			if err != nil {
				t.Fatal(err)
			}
			got = append(got, chunk...)
		}
		if !reflect.DeepEqual(got, want) {
			t.Fatal("streaming modulator differs from one-shot audio")
		}
		m.Reset()
		runtime.GC()
	}
	got, err := m.RenderF32(int(^uint(0) >> 1))
	if err != nil || !reflect.DeepEqual(got, want) {
		t.Fatal("large capacity should render only the remaining samples")
	}
	m.Close()
	m.Close()
	m.Reset()
	if !m.Done() || m.SamplesRemaining() != 0 {
		t.Fatal("closed modulator still has samples")
	}
	if _, err := m.RenderF32(1); err != ErrInvalidArg {
		t.Fatalf("closed modulator RenderF32 = %v", err)
	}
}

func TestModulatorRendersSmallChunksFromLargeFrame(t *testing.T) {
	// At 44100 Hz, this frame needs more than 2^31 samples. A 32-bit
	// renderer must bound the native count before converting it to int.
	frame := make([]byte, 8<<20)
	for i := range frame[:32] {
		frame[i] = byte(i)
	}
	want := streamingAudio(t, frame[:32])[:1024]
	m := NewModulator(frame, 44100)
	defer m.Close()
	got, err := m.RenderF32(len(want))
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatal("large frame prefix differs from one-shot audio")
	}
	if m.Done() {
		t.Fatal("small chunk exhausted the large frame")
	}
}
