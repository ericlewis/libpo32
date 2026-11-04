# PO-32 Patch Parameter Reference

Each of the PO-32's 16 sound slots stores two patches (**Left** and **Right**)
that serve as morph endpoints. Each patch has a tonal **oscillator** and a
**noise generator**. The host synth renders one supplied patch parameter set.
These notes describe the codec, text importer, and host renderer; they do not
establish exact Microtonic or physical-device synthesis behavior.

The `.mtdrum` importer accepts full quoted `MicrotonicDrumPatchV3` exports
and ignores non-PO-32 metadata fields such as `Name`, `Path`, `NStereo`,
`Pan`, `Output`, and `Choke`.

The API does not supply a default sounding patch. `po32_patch_params_zero()`
sets every normalized field to zero, and the importer leaves omitted fields
at zero. In particular, this means silent `Level` and negative `ModAmt`, not
unity output and neutral modulation.

## Transfer Encoding

| Field | Meaning |
| --- | --- |
| Tag | `0x37B2` (`PATCH`) |
| Payload | `43` bytes = `1` command byte + `21` parameters x `2` bytes |
| Command byte | `0x1N` = Left patch, `0x2N` = Right patch (`N` = instrument `0-15`) |
| Parameter encoding | 16-bit unsigned little-endian, normalized by 65535: `0x0000` = `0.0`, `0xFFFF` = `1.0`; intermediate values are quantized |

## Parameters

### Oscillator (0-3)

| # | Name    | Type       | Range / Values |
|---|---------|------------|----------------|
| 0 | OscWave | discrete   | Sine \| Triangle \| Saw |
| 1 | OscFreq | continuous | 20–20000 Hz before modulation (logarithmic) |
| 2 | OscAtk  | continuous | 0–10000 ms (nonlinear) |
| 3 | OscDcy  | continuous | 10–10000 ms (exponential) |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `OscWave` | Waveform shape of the tonal oscillator | Sine = smooth and sub-bassy, Triangle = brighter with more mids, Saw = harsh and rich in harmonics |
| `OscFreq` | Oscillator pitch | `Hz = 20 * pow(1000, x)`<br/>Low values suit kicks, mid values suit toms, high values suit rimshots and clicks |
| `OscAtk` | Oscillator attack time | Importer and renderer share the [attack-time mapping](#attack-time-oscatk-nenvatk). `0` skips the attack ramp |
| `OscDcy` | Oscillator decay time | `time = pow(10, 3x-2)` seconds<br/>Controls how long the tonal layer rings out |

### Modulation (4-6)

| # | Name    | Type       | Range / Values |
|---|---------|------------|----------------|
| 4 | ModMode | discrete   | Decay \| Sine \| Noise |
| 5 | ModRate | continuous | mode-dependent (see below) |
| 6 | ModAmt  | continuous | ±96 semitones in Decay mode; ±48 in Sine/Noise modes (squared bipolar), neutral at normalized 0.5 |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `ModMode` | Oscillator pitch modulation source | Decay = pitch sweep, Sine = undamped sine modulation starting at maximum excursion, Noise = filtered random pitch jitter |
| `ModRate` | Modulation speed or frequency | Decay mode: `(1.03 - x) / (3*x)` seconds for `x > 0`<br/>Sine mode: `2000 * x * pow(10, 3.5*(x-1))` Hz<br/>Noise mode: `20000 * x * pow(10, 4*(x-1))` Hz<br/>See [Pitch Modulation Rate](#pitch-modulation-rate) for zero-rate behavior |
| `ModAmt` | Pitch modulation depth | `b = 2*x - 1; semitones = b * abs(b) * limit`<br/>`limit = 96` in Decay mode, `48` in Sine/Noise modes |

In decay-mode text import, `inf ms` is accepted and stored as normalized zero.
At zero rate, Decay and Sine hold their initial maximum excursion, while Noise
holds its seeded trigger value. Voice reset restarts the same random sequence;
this deterministic host behavior does not establish the device's random state.

### Noise (7-12)

| # | Name    | Type       | Range / Values |
|---|---------|------------|----------------|
| 7 | NFilMod | discrete   | LP \| BP \| HP |
| 8 | NFilFrq | continuous | 20–20000 Hz mapping, capped at 0.45 times sample rate |
| 9 | NFilQ   | continuous | 0.1–10000 (5-decade exponential) |
|10 | NEnvMod | discrete   | Exp \| Lin \| Mod |
|11 | NEnvAtk | continuous | Same attack mapping as OscAtk; mode-dependent behavior |
|12 | NEnvDcy | continuous | 10–10000 ms mapping; mode-dependent behavior |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `NFilMod` | Noise filter type | LP = dark brushed-snare style noise, BP = focused band noise, HP = bright hi-hat style hiss |
| `NFilFrq` | Noise filter cutoff / center frequency | `Hz = min(20 * pow(1000, x), 0.45 * sample_rate)` in the renderer |
| `NFilQ` | Noise filter resonance | `Q = 0.1 * pow(10, 5x)`<br/>Range `0.1` to `10000`<br/>Low = broad natural noise, high = narrow metallic emphasis |
| `NEnvMod` | Noise envelope shape | `Exp`: exponential attack/decay<br/>`Lin`: linear ramps with renderer-mapped times scaled by `2/3`; the importer first multiplies displayed times by `1.5` before mapping them<br/>`Mod`: holds unity until the mapped attack time, then applies a damped cosine; its period depends on NEnvDcy and sample rate, with plain exponential decay when the derived period is zero |
| `NEnvAtk` | Noise attack time | Same base mapping as `OscAtk`, with the mode-specific behavior above |
| `NEnvDcy` | Noise decay time | Same base curve as `OscDcy`, with the mode-specific behavior above |

### Mix and Effects (13-16)

| # | Name    | Type       | Range / Values |
|---|---------|------------|----------------|
|13 | Mix     | continuous | Normalized 0 = oscillator, 1 = noise |
|14 | DistAmt | continuous | 0–100% |
|15 | EQFreq  | continuous | 20–20000 Hz mapping, capped at 0.45 times sample rate |
|16 | EQGain  | continuous | -40 to +40 dB |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `Mix` | Balance between oscillator and noise | `0.5` = equal path gains<br/>Below `0.5` favors oscillator, above `0.5` favors noise<br/>A piecewise gain curve keeps the favored path at unity while attenuating the other; the two curves mirror each other around `0.5`. Text import reverses the percentage: `x = 1 - percent / 100` |
| `DistAmt` | Distortion amount | Nonlinear waveshaper with automatic gain compensation; normalized values at or below `0.01` bypass it. Text import maps percent to `x = percent / 100` |
| `EQFreq` | EQ center frequency | Same logarithmic mapping as `OscFreq`, capped at `0.45 * sample_rate` in the renderer<br/>Placed after distortion |
| `EQGain` | EQ boost or cut | Stored as `(dB + 40) / 80`<br/>Range `-40 dB` to `+40 dB`<br/>A positive boost of `B` dB also attenuates the whole output by `B/2` dB; cuts add no output compensation |

### Output (17-20)

| # | Name    | Type       | Range / Values |
|---|---------|------------|----------------|
|17 | Level   | continuous | -inf to +10 dB |
|18 | OscVel  | continuous | 0–200% |
|19 | NVel    | continuous | 0–200% |
|20 | ModVel  | continuous | 0–200% |

| Name | Role | Mapping / Notes |
| --- | --- | --- |
| `Level` | Base instrument output gain | `dB = 60x - 49 - 1/x` before EQ boost attenuation<br/>`x=0` = silence, `x≈0.836589` = unity, `x=1` = `+10 dB`<br/>Text import accepts silence as `-inf dB` |
| `OscVel` | Velocity sensitivity for oscillator level | `0%` = fixed hits, `100%` = normal dynamics, `200%` = exaggerated dynamics<br/>Stored as `val / 200` |
| `NVel` | Velocity sensitivity for noise level | Controls how soft hits attenuate the noise path independently of `OscVel`; zero leaves noise velocity-independent |
| `ModVel` | Velocity sensitivity for modulation depth | High values reduce sweep on soft hits and keep full sweep on hard hits |

## Mapping Functions

These equations describe the importer and host renderer. Attack time and pitch
modulation use shared helpers in [`po32_param_maps.h`](../src/po32_param_maps.h);
the importer inverts those maps for text values in milliseconds, Hz, or semitones.
Runtime math uses approximations, so tabulated values are rounded. The host
noise-filter and EQ frequencies are additionally capped at 0.45 times sample rate.

### Frequency (OscFreq, NFilFrq, EQFreq)
```
Hz = 20 * pow(1000, x)
  x=0.0 ->    20 Hz
  x=0.5 ->   632 Hz
  x=1.0 -> 20000 Hz
```

### Pitch Modulation Rate

Decay mode maps the normalized rate to the time for the pitch source to fall
from `1` to `0.001`. Increasing the rate shortens this time. Sine mode sets an
undamped oscillator frequency; Noise mode sets a one-pole filter frequency.

```
Decay time_seconds = (1.03 - x) / (3*x), for x > 0
Sine Hz = 2000 * x * pow(10, 3.5*(x-1))
Noise Hz = 20000 * x * pow(10, 4*(x-1))
```

| x | Decay time (ms) | Sine frequency (Hz) | Noise filter frequency (Hz) |
| ---: | ---: | ---: | ---: |
| 0.00 | infinite (hold) | 0 | 0 |
| 0.25 | 1040.00 | 1.186 | 5 |
| 0.50 | 353.33 | 17.783 | 100 |
| 0.75 | 124.44 | 200.028 | 1500 |
| 1.00 | 10.00 | 2000 | 20000 |

The decay importer uses `x = 1.03 / (1 + 0.003*time_ms)`, clamped to `[0, 1]`,
and handles `inf ms` as zero. Sine and Noise import numerically invert their
respective frequency maps. Unlike the noise generator's `NFilFrq`, Noise-mode
`ModRate` reaches zero Hz and uses a different curve.

### Attack Time (OscAtk, NEnvAtk)

The renderer uses this base curve, and the text importer numerically inverts it:

```
time_seconds = x * pow(10, (12*x - 7) / 5)
```

| x | Attack time (ms) |
| ---: | ---: |
| 0.00 | 0 |
| 0.25 | 39.62 |
| 0.50 | 315.48 |
| 0.75 | 1883.92 |
| 1.00 | 10000 |

For example, importing `OscAtk: 1000 ms` produces approximately 1000 ms of host
attack. Linear noise-envelope mode additionally applies the scaling described
above.

### Amplitude Decay Time (OscDcy, NEnvDcy)
```
time_seconds = pow(10, 3*x - 2)
  x=0.00 ->    10 ms
  x=0.25 -> 56.234 ms
  x=0.50 -> 316.228 ms
  x=0.75 -> 1778.279 ms
  x=1.00 -> 10000 ms
```

### Level Gain
```
dB = 60*x - 49 - 1/x
  x=0.00 -> -inf dB
  x=0.50 ->  -21 dB
  x≈0.836589 -> 0 dB (unity)
  x=1.00 ->  +10 dB
```

Positive EQ boost additionally multiplies the output gain by
`pow(10, -EQ_dB / 40)`: `+20 dB` EQ gives approximately `0.316228` compensation
(`-10 dB`), and `+40 dB` gives `0.1` (`-20 dB`). This attenuation applies to all
positive EQ gains, including the small positive range where the EQ filter
itself is bypassed (`abs(EQ_dB) <= 0.5`). Zero gain and cuts leave Level unchanged.

### Velocity Gain
```
x = clamp(1 - (127-vel)/63 * sensitivity, 0, 1)
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
b = 2*x - 1
semitones = b * abs(b) * limit
limit = 96 in Decay mode, 48 in Sine/Noise modes

             Decay    Sine/Noise
  x=0.00 -> -96 sm     -48 sm
  x=0.25 -> -24 sm     -12 sm
  x=0.50 ->   0 sm       0 sm
  x=0.75 -> +24 sm     +12 sm
  x=1.00 -> +96 sm     +48 sm
```

## Morph System

The codec stores Left (command `0x1N`) and Right (command `0x2N`) endpoints.
The host synth accepts one parameter set and does not implement the device's
morph control. A caller can model morphing by linearly interpolating normalized
values:

```
current_value = left_value + (right_value - left_value) * morph_position
```

This formula is a caller-side model, not a verified device interpolation law
for every parameter. The renderer selects discrete modes at thresholds rather
than crossfading their algorithms.

## .mtdrum File Format

The `.mtdrum` format uses a simple `key: value` text layout with units.
The `po32_patch_parse_mtdrum_text()` function in the C API handles parsing.
The importer resolves `ModMode` and `NEnvMod` first, so they may appear before
or after their dependent fields. `ModRate` and `ModAmt` use the selected
modulation mode; `NEnvAtk` and `NEnvDcy` use the selected noise-envelope mode.

## Validation Scope

The [synth reference-point tests](../tests/test_synth_internal.c) check the
shared maps, zero-rate behavior, EQ boost attenuation, and sustained sine depth
across render chunks. The [import tests](../tests/test_core.c) check physical-unit
conversions and mode selectors appearing after dependent fields. These are
host implementation checks, not evidence of complete audio equivalence with
Microtonic or a physical PO-32. Waveform gain/phase, antialiasing, EQ response,
the modulated noise envelope, and stochastic filtering still need independent
audio comparisons. The normalized packet encoding and 21-field public
parameter struct layout are unchanged.
