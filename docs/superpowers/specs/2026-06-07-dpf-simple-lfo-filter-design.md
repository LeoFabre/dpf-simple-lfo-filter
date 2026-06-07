# DPF Port: Simple LFO Filter — Design

**Date:** 2026-06-07
**Source:** `../juce-simple-lfo-filter` (JUCE 6.1.6, VST3 + Standalone)
**Target:** `dpf-simple-lfo-filter` (DISTRHO Plugin Framework, VST3)

## Goal

Port the JUCE "Peak Filter with LFO Modulation and Gain Control" effect to DPF,
preserving its behavior and look, with two deliberate improvements:

1. **Smooth (per-sample) LFO** instead of the original block-rate modulation.
2. **Fix the divide-by-zero** that silences audio at the gain extreme.

## Source behavior (what we are porting)

A stereo peaking-EQ filter whose center frequency is modulated by a sine LFO.

**Parameters (unchanged ranges/defaults):**

| ID                 | Name             | Range            | Default | Notes                       |
|--------------------|------------------|------------------|---------|-----------------------------|
| `FILTER_FREQ`      | Filter Frequency | 20 – 20000 Hz    | 1000    | log/skew 0.5 in original    |
| `FILTER_RESONANCE` | Filter Resonance | 0.1 – 10 (Q)     | 1.0     | linear                      |
| `LFO_DEPTH`        | LFO Depth        | 0 – 1000 Hz      | 100     | linear; freq deviation      |
| `LFO_RATE`         | LFO Rate         | 0.1 – 20 Hz      | 5.0     | log/skew 0.5 in original    |
| `FILTER_GAIN`      | Filter Gain      | 0 – 10           | 1.0     | linear peak gain factor     |

**Signal flow:** `modFreq = clamp(freq + sin(phase)·depth, 20, 20000)`; build a
peaking-EQ biquad from `(modFreq, Q, gain)`; apply to both channels.

## The bug (confirmed root cause)

JUCE `makePeakFilter` (`juce_dsp` / `juce_IIRFilter.cpp`):

```cpp
auto A          = jmax (0.0f, std::sqrt (gainFactor)); // gainFactor==0 → A = 0
auto omega      = twoPi * jmax (frequency, 2.0) / sampleRate;
auto alpha      = 0.5 * std::sin (omega) / Q;
auto alphaOverA = alpha / A;                           // alpha / 0 = Inf  → NaN coeffs
```

When `FILTER_GAIN` hits **0**, `A = 0` → `alpha / A = Inf` → `Inf`/`NaN`
coefficients → the IIR state latches to `NaN` → channel goes silent
permanently. The original works around it with an `if (gain == 0.0f)
{ passthrough; return; }` branch. (Frequency is already guarded by
`jmax(frequency, 2.0)`, so the LFO path itself does not divide by zero.)

**Fix:** clamp the gain factor to `>= 1e-4` before `A = sqrt(gain)`, so
`alpha / A` is always finite. Removes the divide-by-zero at the source and makes
the `gain == 0` passthrough hack unnecessary.

## Improvements in the port

- **Per-sample LFO:** advance `lfoPhase` and recompute `modFreq` every sample so
  the sweep is continuous and independent of host block size. One peaking biquad
  per channel — per-sample coefficient recompute is cheap enough.
- **Defensive coefficients:** clamp `freq → [20, 0.45·sr]`, `Q → ≥ 1e-3`,
  `gain → ≥ 1e-4`; verify coefficients are finite before applying (else keep last
  good set); flush denormals and reset filter state if a non-finite sample is
  detected, so no edge condition can latch silence.

## Architecture

```
dpf-simple-lfo-filter/
├── CMakeLists.txt                 # add_subdirectory(dpf); dpf_add_plugin(... TARGETS vst3)
├── dpf/                           # git submodule: DISTRHO/DPF
└── plugins/SimpleLFOFilter/
    ├── DistrhoPluginInfo.h        # metadata + format/UI toggles
    ├── Biquad.hpp                 # peaking-EQ biquad (ports makePeakFilter, with clamps)
    ├── SimpleLFOFilterPlugin.cpp  # DSP: initParameter + per-sample LFO + filter
    ├── VSlider.hpp / VSlider.cpp  # NanoVG vertical slider widget
    └── SimpleLFOFilterUI.cpp      # 5 sliders + labels + centered title (500×350)
```

**Components**

- **Parameters** — DPF `initParameter()` for the 5 params; `FILTER_FREQ` and
  `LFO_RATE` get `kParameterIsLogarithmic` to reproduce the 0.5 skew; all
  automatable. DPF persists parameter values via the host, so no custom
  `getState`/`setState` is needed (replaces the JUCE APVTS XML state).
- **Biquad.hpp** — Audio-EQ-Cookbook peaking filter matching JUCE's formula,
  transposed-direct-form-II, per-channel `z1/z2` state, with the clamps above.
- **DSP `run()`** — per-sample: compute `modFreq`, update coefficients, filter
  each channel; advance + wrap `lfoPhase`. Stereo.
- **UI** — `NanoVG`-based `UI`; reusable `VSlider` (track + orange thumb,
  vertical drag → normalized value via `setParameterValue`/`editParameter`),
  five laid out like the original, plus title and per-slider labels.

## Build & verification

- CMake via DPF: `dpf_add_plugin(SimpleLFOFilter ... TARGETS vst3)`.
- Verify VST3 loads; sweep each parameter; confirm **no dropout at
  `FILTER_GAIN = 0`** and a smooth (zipper-free) LFO sweep.
- Spot-check biquad coefficients against the JUCE formula at a few
  `(freq, Q, gain)` points.

## Out of scope (YAGNI)

- Formats other than VST3 (LV2/CLAP/Standalone) — can be added later by editing
  `TARGETS`.
- Presets/programs, MIDI, custom session state, resizable UI, tempo sync.
