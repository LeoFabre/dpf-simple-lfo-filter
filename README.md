# SimpleLFOFilter (DPF Port)

A stereo peaking-EQ filter whose center frequency is continuously modulated by a sine LFO.
This is a port of [juce-simple-lfo-filter](https://github.com/hollance/juce-simple-lfo-filter)
to the [DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF), shipping as a VST3
with a custom NanoVG GUI featuring five vertical sliders.

---

## Parameters

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Filter Frequency | 20 – 20 000 Hz | 1 000 Hz | Logarithmic |
| Filter Resonance (Q) | 0.1 – 10 | 1.0 | Linear |
| LFO Depth | 0 – 1 000 Hz | 100 Hz | Linear |
| LFO Rate | 0.1 – 20 Hz | 5.0 Hz | Logarithmic |
| Filter Gain | 0 – 10 (linear) | 1.0 | Peak shelf gain |

---

## Building

```bash
git clone <repo-url>
cd dpf-simple-lfo-filter
git submodule update --init --recursive   # fetches DPF
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# result: build/bin/SimpleLFOFilter.vst3
```

Requirements: CMake 3.15+, a C++17-capable compiler (tested with Apple clang on macOS arm64).

---

## Port notes

This is a DPF port of **juce-simple-lfo-filter**.  Two improvements were made over the JUCE original:

1. **Per-sample smooth LFO modulation** — the JUCE version updated the modulated frequency only once per audio block, causing audible stepping ("zipper noise") at low buffer sizes or high LFO rates. The DPF port advances the LFO phase on every sample.

2. **Fixed a divide-by-zero dropout at Filter Gain = 0** — the original biquad coefficient calculation computes `alpha / A` where `A = sqrt(gainFactor)`. When Filter Gain is set to 0, `A` is 0, producing a NaN/inf that silences audio. The fix clamps the gain factor to a small positive epsilon before computing the square root so audio always flows through.

### DSP unit tests

The DSP is unit-tested independently of any plugin host via `tests/test_dsp.cpp`, compiled standalone with `clang++`:

```bash
clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o test_dsp && ./test_dsp
```

The test `test_gain_zero_is_finite` is the automated regression proof for the divide-by-zero fix: it drives the filter with Filter Gain = 0 and asserts that every output sample is finite.

---

## Manual verification checklist

The following checks require loading the plugin in a DAW. They are **pending manual verification**.

- [ ] Five vertical sliders render with labels and a centered title; dragging a slider moves the host parameter, and host automation moves the slider.
- [ ] With audio playing and LFO Depth > 0, the filter sweeps smoothly (no zipper/stepping); the rate tracks LFO Rate.
- [ ] Set Filter Gain to 0 — audio keeps flowing (no dropout/silence). [Bug-fix acceptance check.]
- [ ] Set LFO Depth to 0 — the filter sits at Filter Frequency; audio passes cleanly.
- [ ] Sweeping Filter Frequency and Resonance audibly changes the peak; no clicks/NaN dropouts at extremes.
