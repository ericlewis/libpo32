# PO-32 Patch Parameter Reference

Each of the 8 instruments is a two-layer drum synthesizer: a tonal
**oscillator** and a **noise generator**. Each instrument has two patches
(**Left** and **Right**) that serve as morph endpoints. The PO-32 morph
knob interpolates linearly between them.

The `.mtdrum` importer accepts full quoted `MicrotonicDrumPatchV3` exports
and ignores non-PO-32 metadata fields such as `Name`, `Path`, `NStereo`,
`Pan`, `Output`, and `Choke`.

## Transfer Encoding

| Field | Meaning |
| --- | --- |
| Tag | `0x37B2` (`PATCH`) |
| Payload | `43` bytes = `1` command byte + `21` parameters x `2` bytes |
| Command byte | `0x1N` = Left patch, `0x2N` = Right patch (`N` = instrument `0-15`) |
| Parameter encoding | 16-bit unsigned little-endian, range `0x0000` (`0.0`) to `0xFFFF` (`~1.0`) |

## Parameters

### Oscillator (0-3)

| # | Name    | Type       | Range / Values               | Default    |
|---|---------|------------|------------------------------|------------|
| 0 | OscWave | discrete   | Sine \| Triangle \| Saw      | Sine       |
| 1 | OscFreq | continuous | 20–20000 Hz (logarithmic)    | 0.47256    |
| 2 | OscAtk  | continuous | 0–10000 ms (exponential)     | 0 ms       |
| 3 | OscDcy  | continuous | 10–10000 ms (exponential)    | 316 ms     |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `OscWave` | Waveform shape of the tonal oscillator | Sine = smooth and sub-bassy, Triangle = brighter with more mids, Saw = harsh and rich in harmonics |
| `OscFreq` | Oscillator pitch | `Hz = 20 * pow(1000, x)`<br/>Low values suit kicks, mid values suit toms, high values suit rimshots and clicks |
| `OscAtk` | Oscillator attack time | `time = x * pow(10, (12x-7)/5)` seconds<br/>`0` = instant snap, larger values = slower fade-in |
| `OscDcy` | Oscillator decay time | `time = pow(10, 3x-2)` seconds<br/>Controls how long the tonal layer rings out |

### Modulation (4-6)

| # | Name    | Type       | Range / Values                   | Default    |
|---|---------|------------|----------------------------------|------------|
| 4 | ModMode | discrete   | Decay \| Sine \| Noise           | Decay      |
| 5 | ModRate | continuous | mode-dependent (see below)       | 0.5        |
| 6 | ModAmt  | continuous | ±96 semitones (linear)           | 0          |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `ModMode` | Oscillator pitch modulation source | Decay = pitch sweep, Sine = vibrato / FM wobble, Noise = random pitch jitter |
| `ModRate` | Modulation speed or frequency | Decay mode: pitch-mod decay time in ms, approximately `pow(10, 3x-2)` seconds, with `param=0` often exported by Microtonic as `inf ms`<br/>Sine mode: LFO frequency in Hz, approximately `2000 * x`<br/>Noise mode: noise filter cutoff in Hz, `20 * pow(1000, x)` |
| `ModAmt` | Pitch modulation depth | `semitones = (param - 0.5) * 192`<br/>Range `-96` to `+96` semitones |

### Noise (7-12)

| # | Name    | Type       | Range / Values                  | Default    |
|---|---------|------------|---------------------------------|------------|
| 7 | NFilMod | discrete   | LP \| BP \| HP                  | LP         |
| 8 | NFilFrq | continuous | 20–20000 Hz (logarithmic)       | 1.0        |
| 9 | NFilQ   | continuous | 0.1–10000 (5-decade exponential)| 0.17       |
|10 | NEnvMod | discrete   | Exp \| Lin \| Mod               | Exp        |
|11 | NEnvAtk | continuous | 0–10000 ms (exponential)        | 0 ms       |
|12 | NEnvDcy | continuous | 10–10000 ms (exponential)       | 316 ms     |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `NFilMod` | Noise filter type | LP = dark brushed-snare style noise, BP = focused band noise, HP = bright hi-hat style hiss |
| `NFilFrq` | Noise filter cutoff / center frequency | `Hz = 20 * pow(1000, x)`<br/>Low = dark rumble, high = bright sizzle |
| `NFilQ` | Noise filter resonance | `Q = 0.1 * pow(10, 5x)`<br/>Range `0.1` to `10000`<br/>Low = broad natural noise, high = narrow metallic emphasis |
| `NEnvMod` | Noise envelope shape | `Exp`: exponential attack/decay<br/>`Lin`: linear ramps with times scaled by `2/3` (Microtonic exports the displayed attack/decay values already scaled, so importers need to invert that display scaling)<br/>`Mod`: damped cosine tremolo with short-period audio-rate ring-mod behavior |
| `NEnvAtk` | Noise attack time | Same curve as `OscAtk`<br/>Usually near `0` for percussive sounds |
| `NEnvDcy` | Noise decay time | Same curve as `OscDcy`<br/>Short = closed hat, long = open hat or crash |

### Mix and Effects (13-16)

| # | Name    | Type       | Range / Values          | Default    |
|---|---------|------------|-------------------------|------------|
|13 | Mix     | continuous | 0–100% (dB crossfade)   | 50 (center)|
|14 | DistAmt | continuous | 0–100%                  | 0          |
|15 | EQFreq  | continuous | 20–20000 Hz (logarithmic)| 0.47256   |
|16 | EQGain  | continuous | -40 to +40 dB           | 0 dB       |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `Mix` | Balance between oscillator and noise | `0.5` = equal blend<br/>Below `0.5` favors oscillator, above `0.5` favors noise<br/>Uses an asymmetric dB crossfade: `n = mix*2 - 1`, then the oscillator and noise gains are scaled separately |
| `DistAmt` | Distortion amount | Cubic waveshaper with automatic gain compensation<br/>`0` = clean, `100` = aggressive clipping |
| `EQFreq` | EQ center frequency | Same logarithmic scale as `OscFreq`<br/>Placed after distortion |
| `EQGain` | EQ boost or cut | Stored as `(dB + 40) / 80`<br/>Range `-40 dB` to `+40 dB` |

### Output (17-20)

| # | Name    | Type       | Range / Values          | Default    |
|---|---------|------------|-------------------------|------------|
|17 | Level   | continuous | -inf to +10 dB          | 0 dB       |
|18 | OscVel  | continuous | 0–200%                  | 0%         |
|19 | NVel    | continuous | 0–200%                  | 0%         |
|20 | ModVel  | continuous | 0–200%                  | 0%         |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `Level` | Final instrument output gain | `dB = 60x - 49 - 1/x`<br/>`x=0` = silence, `x≈0.836` = unity, `x=1` = `+10 dB`<br/>Microtonic may export silence as `-inf dB` |
| `OscVel` | Velocity sensitivity for oscillator level | `0%` = fixed hits, `100%` = normal dynamics, `200%` = exaggerated dynamics<br/>Stored as `val / 200` |
| `NVel` | Velocity sensitivity for noise level | Lets softer hits keep noise while reducing tone independently of the oscillator |
| `ModVel` | Velocity sensitivity for modulation depth | High values reduce sweep on soft hits and keep full sweep on hard hits |

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
