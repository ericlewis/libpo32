# PO-32 Patch Parameter Reference

Each of the 8 instruments is a two-layer drum synthesizer: a tonal
**oscillator** and a **noise generator**. Each instrument has two patches
(**Left** and **Right**) that serve as morph endpoints — the PO-32's morph
knob interpolates linearly between them.

## Transfer Encoding

- Tag: `0x37B2` (PATCH)
- Payload: 43 bytes = 1 command byte + 21 parameters x 2 bytes
- Command byte: `0x1N` = Left patch, `0x2N` = Right patch (N = instrument 0-15)
- Each parameter: 16-bit unsigned little-endian, range 0x0000 (0.0) to 0xFFFF (~1.0)

## Parameters

### Oscillator (0-3)

| # | Name    | Type       | Range / Values               | Default    |
|---|---------|------------|------------------------------|------------|
| 0 | OscWave | discrete   | Sine \| Triangle \| Saw      | Sine       |
| 1 | OscFreq | continuous | 20–20000 Hz (logarithmic)    | 0.47256    |
| 2 | OscAtk  | continuous | 0–10000 ms (exponential)     | 0 ms       |
| 3 | OscDcy  | continuous | 10–10000 ms (exponential)    | 316 ms     |

**OscWave** — Waveform shape of the tonal oscillator. Sine (smooth,
sub-bassy), Triangle (slightly brighter, more mid content), Saw (harsh,
buzzy, full of harmonics).

**OscFreq** — Pitch of the oscillator on a logarithmic scale:
`Hz = 20 * pow(1000, x)`. Low values make kicks, mid values make toms,
high values make rimshots and clicks.

**OscAtk** — How quickly the oscillator reaches full volume after a trigger.
0 = instant snap, higher = slower fade-in. Exponential curve:
`time = x * pow(10, (12x-7)/5)` seconds.

**OscDcy** — How quickly the oscillator fades to silence. Controls the
"length" of the tonal part. Exponential curve:
`time = pow(10, 3x-2)` seconds.

### Modulation (4-6)

| # | Name    | Type       | Range / Values                   | Default    |
|---|---------|------------|----------------------------------|------------|
| 4 | ModMode | discrete   | Decay \| Sine \| Noise           | Decay      |
| 5 | ModRate | continuous | mode-dependent (see below)       | 0.5        |
| 6 | ModAmt  | continuous | ±96 semitones (linear)           | 0          |

**ModMode** — What modulates the oscillator pitch. Decay = pitch sweeps down
(classic 808 kick), Sine = vibrato / FM wobble, Noise = random pitch jitter
(metallic crashes, unstable oscillators).

**ModRate** — Speed/frequency of modulation. **Unit depends on ModMode:**
- **Decay mode**: rate is a pitch modulation decay time in ms. At param=0
  the display shows "inf ms" (modulation never decays). Factory presets use
  values in the 40–1100 ms range. Approximate mapping: `pow(10, 3x-2)`
  seconds, but the exact formula at the extremes may differ from OscDcy.
- **Sine mode**: rate is an LFO frequency in Hz (0–2000 Hz). Factory presets
  range from 0 Hz (claves — no vibrato) to 2000 Hz (cowbell — audio-rate FM).
  Mapping appears to be `2000 * x` or similar starting from 0.
- **Noise mode**: rate is a noise filter cutoff in Hz (20–20000 Hz). Mapped
  via `20 * pow(1000, x)`. Higher = faster/rougher random pitch changes.
  Lower = smoother/slower random drift.

**ModAmt** — Depth of pitch modulation in semitones. Range ±96 semitones
(8 octaves). Stored as normalized 0–1 where 0.0 = -96 sm, 0.5 = 0 sm,
1.0 = +96 sm. Linear mapping: `semitones = (param - 0.5) * 192`.

### Noise (7-12)

| # | Name    | Type       | Range / Values                  | Default    |
|---|---------|------------|---------------------------------|------------|
| 7 | NFilMod | discrete   | LP \| BP \| HP                  | LP         |
| 8 | NFilFrq | continuous | 20–20000 Hz (logarithmic)       | 1.0        |
| 9 | NFilQ   | continuous | 0.1–10000 (5-decade exponential)| 0.17       |
|10 | NEnvMod | discrete   | Exp \| Lin \| Mod               | Exp        |
|11 | NEnvAtk | continuous | 0–10000 ms (exponential)        | 0 ms       |
|12 | NEnvDcy | continuous | 10–10000 ms (exponential)       | 316 ms     |

**NFilMod** — Noise filter type. LP (low-pass) = dark noise like a brushed
snare. BP (band-pass) = focused noise at a specific frequency. HP (high-pass)
= bright hissy noise like a hi-hat.

**NFilFrq** — Cutoff / center frequency of the noise filter. Same logarithmic
scale as OscFreq: `Hz = 20 * pow(1000, x)`. Low = dark rumble, high = bright
sizzle.

**NFilQ** — Resonance of the noise filter. 5-decade exponential range:
`Q = 0.1 * pow(10, 5x)`. At param 0 → Q = 0.1 (very broad). At param 1 →
Q = 10000 (extremely narrow resonance). Low = broad natural noise. High =
narrow ringing emphasis, useful for metallic hi-hat sounds.

**NEnvMod** — Noise volume envelope shape:
- **Exp** (< 0.333): Exponential attack/decay. Natural drum decay.
- **Lin** (0.333–0.666): Linear ramps. Times scaled by 2/3.
- **Mod** (> 0.666): Damped cosine tremolo. The noise amplitude oscillates
  via cos(2π·t/T) while decaying exponentially, creating a ringing/shimmer
  quality. Period = `(dcy_param/sr) * pow(10000, dcy_param)`. Short periods
  (< 1.5ms) switch to an 8x-period mode with `sqrt(0.5/period)` amplitude
  correction, producing audio-rate ring modulation that adds spectral
  sidebands.

**NEnvAtk** — Noise attack time. Same curve as OscAtk. Usually 0 for
percussive sounds.

**NEnvDcy** — Noise decay time. How long the noise rings out. Short = closed
hi-hat. Long = open hi-hat or crash. Same curve as OscDcy.

### Mix and Effects (13-16)

| # | Name    | Type       | Range / Values          | Default    |
|---|---------|------------|-------------------------|------------|
|13 | Mix     | continuous | 0–100% (dB crossfade)   | 50 (center)|
|14 | DistAmt | continuous | 0–100%                  | 0          |
|15 | EQFreq  | continuous | 20–20000 Hz (logarithmic)| 0.47256   |
|16 | EQGain  | continuous | -40 to +40 dB           | 0 dB       |

**Mix** — Balance between oscillator and noise. Center (0.5) = equal blend.
Below 0.5 = mostly oscillator (kicks, toms). Above 0.5 = mostly noise
(hi-hats, snares). Uses an asymmetric dB crossfade: `n = mix*2 - 1`,
then osc gain = `(1-n) * pow(10, -25n/20)` when n≥0, else 1.0;
noise gain = `(1+n) * pow(10, 25n/20)` when n<0, else 1.0.

**DistAmt** — Distortion amount. Cubic waveshaper with automatic gain
compensation. 0 = clean. 100 = hard-clipped and aggressive.

**EQFreq** — Center frequency of a single-band parametric EQ. Same
logarithmic scale as OscFreq. Sits after the distortion stage.

**EQGain** — EQ boost or cut. -40 dB = deep notch, 0 dB = flat, +40 dB =
extreme boost. Stored as `(dB + 40) / 80`.

### Output (17-20)

| # | Name    | Type       | Range / Values          | Default    |
|---|---------|------------|-------------------------|------------|
|17 | Level   | continuous | -inf to +10 dB          | 0 dB       |
|18 | OscVel  | continuous | 0–200%                  | 0%         |
|19 | NVel    | continuous | 0–200%                  | 0%         |
|20 | ModVel  | continuous | 0–200%                  | 0%         |

**Level** — Master volume for this instrument. Exponential curve:
`dB = 60x - 49 - 1/x`. At x=0 → -inf dB (silence), x=0.836 → 0 dB (unity),
x=1.0 → +10 dB.

**OscVel** — How much MIDI velocity affects oscillator volume. 0% = every
hit identical. 100% = normal dynamics. 200% = exaggerated dynamics. The
PO-32 sends velocity based on pattern accent flags. Stored as `val / 200`.

**NVel** — Velocity sensitivity for the noise layer, independent of the
oscillator. Lets soft hits keep noise (snare rattle) while reducing tone.

**ModVel** — Velocity sensitivity for modulation depth. At 200%, soft hits
have almost no pitch sweep while hard hits get the full sweep.

## Exact Mapping Functions

### Frequency (OscFreq, NFilFrq, EQFreq, ModRate in Noise mode)
```
Hz = 20 * pow(1000, x)
  x=0.0 ->    20 Hz
  x=0.5 ->   632 Hz
  x=1.0 -> 20000 Hz
```

### ModRate in Sine mode (linear, NOT logarithmic)
```
Hz = 2000 * x
  x=0.0 ->    0 Hz
  x=0.5 -> 1000 Hz
  x=1.0 -> 2000 Hz
```

### Attack Time (OscAtk, NEnvAtk)
```
time_seconds = x * pow(10, (12*x - 7) / 5)
  x=0.00 ->     0 ms
  x=0.25 ->  0.05 ms
  x=0.50 ->     2 ms
  x=0.75 ->    50 ms
  x=1.00 -> 10000 ms
```

### Decay Time (OscDcy, NEnvDcy, ModRate in Decay mode)
```
time_seconds = pow(10, 3*x - 2)
  x=0.00 ->    10 ms
  x=0.25 ->    18 ms
  x=0.50 ->   100 ms
  x=0.75 ->   562 ms
  x=1.00 -> 10000 ms
```

### Level Gain
```
dB = 60*x - 49 - 1/x
  x=0.00 -> -inf dB
  x=0.50 ->  -21 dB
  x=0.836 ->   0 dB (unity)
  x=1.00 ->  +10 dB
```

### Velocity Gain
```
x = max(1 - (127-vel)/63 * sensitivity, 0)
gain = x * pow(10, (37*x - 37) / 20)
```

### Filter Q
```
Q = 0.1 * pow(10, 5*x)
  x=0.0 ->     0.1
  x=0.2 ->     1.0
  x=0.4 ->    10.0
  x=0.6 ->   100.0
  x=0.8 ->  1000.0
  x=1.0 -> 10000.0
```

### Mod Amount
```
semitones = (x - 0.5) * 192
  x=0.0 -> -96 sm
  x=0.5 ->   0 sm
  x=1.0 -> +96 sm
```

## Morph System

The PO-32 stores two patches per instrument: Left (command `0x1N`) and
Right (command `0x2N`). The morph knob linearly interpolates all 21
parameters between the two patches:

```
current_value = left_value + (right_value - left_value) * morph_position
```

## .mtdrum File Format

The `.mtdrum` format uses a simple `key: value` text layout with units.
The `po32_patch_parse_mtdrum_text()` function in the C API handles parsing.
