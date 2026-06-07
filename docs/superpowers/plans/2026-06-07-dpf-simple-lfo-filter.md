# DPF Simple LFO Filter — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the JUCE "Peak Filter with LFO Modulation and Gain Control" effect to the DISTRHO Plugin Framework (VST3), with a smooth per-sample LFO and a fix for the divide-by-zero that silences audio at `FILTER_GAIN = 0`.

**Architecture:** Header-only, DPF-independent DSP (a peaking-EQ biquad with input clamps + non-finite guard, and a sine LFO) is unit-tested with a standalone test binary. A DPF `Plugin` subclass exposes the 5 parameters and runs the DSP per-sample. A NanoVG `UI` hosts five hand-written vertical-slider sub-widgets plus labels and a title.

**Tech Stack:** C++17, DISTRHO Plugin Framework (DPF, vendored as a git submodule), DGL + NanoVG for the UI, CMake build, `clang++` for the standalone DSP tests.

---

## File Structure

```
dpf-simple-lfo-filter/
├── CMakeLists.txt                       # add_subdirectory(dpf); dpf_add_plugin(... TARGETS vst3)
├── .gitmodules                          # dpf submodule
├── dpf/                                 # git submodule: DISTRHO/DPF
├── plugins/SimpleLFOFilter/
│   ├── DistrhoPluginInfo.h              # plugin metadata + format/UI toggles
│   ├── ParamInfo.h                      # shared parameter enum + descriptor table (DSP + UI)
│   ├── Biquad.hpp                       # PeakCoeffs (ports makePeakFilter, clamps) + BiquadState
│   ├── Lfo.hpp                          # sine LFO (phase, rate, tick)
│   ├── SimpleLFOFilterPlugin.cpp        # DPF Plugin: params + per-sample DSP
│   ├── VSlider.hpp / VSlider.cpp        # NanoVG vertical-slider sub-widget
│   └── SimpleLFOFilterUI.cpp            # UI: 5 sliders + labels + title
└── tests/
    └── test_dsp.cpp                     # standalone asserts for Biquad + Lfo
```

**Responsibilities**
- `ParamInfo.h` — single source of truth for parameter index enum, symbols, names, ranges, defaults, and per-param hints. Included by both the plugin and the UI so they never drift.
- `Biquad.hpp` — `PeakCoeffs::set()` builds normalized peaking-EQ coefficients with the bug-fix clamps; `BiquadState::process()` runs transposed-direct-form-II and self-heals on non-finite output. No DPF dependency.
- `Lfo.hpp` — sine oscillator: `setRate()`, per-sample `tick()`, `reset()`. No DPF dependency.
- `SimpleLFOFilterPlugin.cpp` — glue: maps the 5 params, computes mod-freq per sample, drives one `PeakCoeffs` shared by two `BiquadState`s.
- `VSlider.*` — reusable vertical slider; reports drag via a callback with begin/change/end semantics.
- `SimpleLFOFilterUI.cpp` — lays out the five sliders + labels + title, bridges slider callbacks to `setParameterValue`/`editParameter`, and reflects host changes via `parameterChanged`.

---

## Task 1: Peaking-EQ biquad with bug-fix clamps (TDD)

**Files:**
- Create: `plugins/SimpleLFOFilter/Biquad.hpp`
- Create: `tests/test_dsp.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_dsp.cpp`:

```cpp
#include "Biquad.hpp"
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>

// |H(e^{jw})| for a normalized biquad (a0 == 1).
static double magAt(const PeakCoeffs& c, double omega) {
    using cd = std::complex<double>;
    const cd z1 = std::exp(cd(0.0, -omega));
    const cd z2 = z1 * z1;
    const cd num = cd(c.b0) + cd(c.b1) * z1 + cd(c.b2) * z2;
    const cd den = cd(1.0) + cd(c.a1) * z1 + cd(c.a2) * z2;
    return std::abs(num / den);
}

static void test_peak_gain_at_center() {
    PeakCoeffs c;
    const double sr = 48000.0, f = 1000.0, q = 4.0, g = 4.0;
    assert(c.set(sr, f, q, g));
    const double omega = 2.0 * M_PI * f / sr;
    const double mag = magAt(c, omega);
    // peaking filter magnitude at its center ~= the linear gain factor
    assert(std::fabs(mag - g) < 0.15 * g);
}

static void test_unity_gain_is_flat() {
    PeakCoeffs c;
    assert(c.set(48000.0, 1000.0, 2.0, 1.0));
    // gain factor 1.0 -> numerator == denominator -> flat 0 dB everywhere
    for (double f : {50.0, 500.0, 1000.0, 8000.0}) {
        const double mag = magAt(c, 2.0 * M_PI * f / 48000.0);
        assert(std::fabs(mag - 1.0) < 1e-9);
    }
}

static void test_gain_zero_is_finite() {
    // The original bug: gainFactor == 0 -> alpha/A divides by zero -> NaN coeffs -> silence.
    PeakCoeffs c;
    const bool ok = c.set(48000.0, 1000.0, 1.0, 0.0);
    assert(ok);
    assert(std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2));
    assert(std::isfinite(c.a1) && std::isfinite(c.a2));

    BiquadState s;
    for (int i = 0; i < 256; ++i) {
        const float y = s.process(std::sin(0.1 * i), c);
        assert(std::isfinite(y));
    }
}

static void test_state_self_heals() {
    // A poisoned input must not latch the filter into permanent NaN.
    PeakCoeffs c;
    assert(c.set(48000.0, 1000.0, 1.0, 2.0));
    BiquadState s;
    (void) s.process(std::nanf(""), c);          // inject NaN
    const float y = s.process(1.0f, c);           // next real sample
    assert(std::isfinite(y));
}

int main() {
    test_peak_gain_at_center();
    test_unity_gain_is_flat();
    test_gain_zero_is_finite();
    test_state_self_heals();
    std::printf("ALL DSP TESTS PASSED\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o /tmp/test_dsp`
Expected: FAIL to compile — `fatal error: 'Biquad.hpp' file not found`.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/SimpleLFOFilter/Biquad.hpp`:

```cpp
#pragma once
#include <cmath>

// Peaking-EQ biquad coefficients, normalized so a0 == 1.
// Ports juce::dsp::IIR::Coefficients::makePeakFilter, adding input clamps so the
// gainFactor==0 divide-by-zero (alpha / A with A = sqrt(0)) can never occur.
struct PeakCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // Returns false (leaving coefficients unchanged) if the computed set is
    // non-finite, so a transient bad value keeps the last good coefficients.
    bool set(double sampleRate, double freq, double Q, double gainFactor) {
        const double sr = (sampleRate > 0.0) ? sampleRate : 44100.0;
        if (freq < 20.0)         freq = 20.0;
        if (freq > 0.45 * sr)    freq = 0.45 * sr;
        if (Q < 1.0e-3)          Q = 1.0e-3;
        if (gainFactor < 1.0e-4) gainFactor = 1.0e-4;   // <-- the fix: A is never 0

        const double A          = std::sqrt(gainFactor);
        const double omega       = 2.0 * M_PI * freq / sr;
        const double alpha       = 0.5 * std::sin(omega) / Q;
        const double c2          = -2.0 * std::cos(omega);
        const double alphaTimesA = alpha * A;
        const double alphaOverA  = alpha / A;
        const double a0          = 1.0 + alphaOverA;

        const double nb0 = (1.0 + alphaTimesA) / a0;
        const double nb1 = c2 / a0;
        const double nb2 = (1.0 - alphaTimesA) / a0;
        const double na1 = c2 / a0;
        const double na2 = (1.0 - alphaOverA) / a0;

        if (!(std::isfinite(nb0) && std::isfinite(nb1) && std::isfinite(nb2)
              && std::isfinite(na1) && std::isfinite(na2)))
            return false;

        b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
        return true;
    }
};

// Transposed Direct Form II state. Coefficients are passed in so one PeakCoeffs
// can drive multiple channels.
struct BiquadState {
    double z1 = 0.0, z2 = 0.0;

    inline float process(double x, const PeakCoeffs& c) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        if (!std::isfinite(y)) { z1 = z2 = 0.0; return 0.0f; }  // self-heal, no latched silence
        return static_cast<float>(y);
    }

    void reset() { z1 = z2 = 0.0; }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o /tmp/test_dsp && /tmp/test_dsp`
Expected: `ALL DSP TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add plugins/SimpleLFOFilter/Biquad.hpp tests/test_dsp.cpp
git commit -m "feat(dsp): peaking biquad with gain=0 divide-by-zero fix"
```

---

## Task 2: Sine LFO (TDD)

**Files:**
- Create: `plugins/SimpleLFOFilter/Lfo.hpp`
- Modify: `tests/test_dsp.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_dsp.cpp` — insert these two functions above `int main()`:

```cpp
#include "Lfo.hpp"

static void test_lfo_completes_one_cycle() {
    // 1 Hz at 100 samples/s -> phase returns near 0 after exactly 100 ticks.
    Lfo lfo;
    lfo.setRate(1.0, 100.0);
    for (int i = 0; i < 100; ++i) (void) lfo.tick();
    assert(std::fabs(lfo.phase) < 1e-9 || std::fabs(lfo.phase - 2.0 * M_PI) < 1e-9);
}

static void test_lfo_range_and_start() {
    Lfo lfo;
    lfo.setRate(2.0, 48000.0);
    assert(std::fabs(lfo.tick()) < 1e-12);     // first sample is sin(0) == 0
    for (int i = 0; i < 48000; ++i) {
        const double v = lfo.tick();
        assert(v >= -1.0001 && v <= 1.0001);
    }
}
```

And add these two calls at the top of `main()` (before the print):

```cpp
    test_lfo_completes_one_cycle();
    test_lfo_range_and_start();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o /tmp/test_dsp`
Expected: FAIL to compile — `fatal error: 'Lfo.hpp' file not found`.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/SimpleLFOFilter/Lfo.hpp`:

```cpp
#pragma once
#include <cmath>

// Sine LFO advanced one sample at a time, so modulation is smooth and
// independent of host block size.
struct Lfo {
    double phase = 0.0;   // radians, [0, 2*pi)
    double inc   = 0.0;   // radians per sample

    void setRate(double rateHz, double sampleRate) {
        const double sr = (sampleRate > 0.0) ? sampleRate : 44100.0;
        inc = 2.0 * M_PI * rateHz / sr;
    }

    // Returns the current sine value, then advances and wraps the phase.
    double tick() {
        const double v = std::sin(phase);
        phase += inc;
        while (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        return v;
    }

    void reset() { phase = 0.0; }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o /tmp/test_dsp && /tmp/test_dsp`
Expected: `ALL DSP TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add plugins/SimpleLFOFilter/Lfo.hpp tests/test_dsp.cpp
git commit -m "feat(dsp): per-sample sine LFO"
```

---

## Task 3: Shared parameter descriptors

**Files:**
- Create: `plugins/SimpleLFOFilter/ParamInfo.h`

No unit test (pure data); it is exercised by the build in later tasks.

- [ ] **Step 1: Create the descriptor table**

Create `plugins/SimpleLFOFilter/ParamInfo.h`:

```cpp
#pragma once

// Parameter indices. Order is the host-facing parameter order.
enum ParamIndex {
    kParamFreq = 0,   // FILTER_FREQ
    kParamReso,       // FILTER_RESONANCE (Q)
    kParamDepth,      // LFO_DEPTH (Hz)
    kParamRate,       // LFO_RATE (Hz)
    kParamGain,       // FILTER_GAIN (linear)
    kParamCount
};

struct ParamDescriptor {
    const char* symbol;
    const char* name;
    float min;
    float max;
    float def;
    bool  logarithmic;   // reproduces JUCE's 0.5 skew for freq/rate
};

// Ranges, defaults, and names taken verbatim from the JUCE source.
static const ParamDescriptor kParams[kParamCount] = {
    { "filter_freq", "Filter Frequency", 20.0f, 20000.0f, 1000.0f, true  },
    { "filter_reso", "Filter Resonance", 0.1f,  10.0f,    1.0f,    false },
    { "lfo_depth",   "LFO Depth",        0.0f,  1000.0f,  100.0f,  false },
    { "lfo_rate",    "LFO Rate",         0.1f,  20.0f,    5.0f,    true  },
    { "filter_gain", "Filter Gain",      0.0f,  10.0f,    1.0f,    false },
};
```

- [ ] **Step 2: Verify it compiles standalone**

Run: `clang++ -std=c++17 -fsyntax-only -xc++ plugins/SimpleLFOFilter/ParamInfo.h`
Expected: no output, exit code 0.

- [ ] **Step 3: Commit**

```bash
git add plugins/SimpleLFOFilter/ParamInfo.h
git commit -m "feat: shared parameter descriptor table"
```

---

## Task 4: Vendor DPF + build skeleton

**Files:**
- Create: `.gitmodules` (via `git submodule add`)
- Create: `dpf/` (submodule)
- Create: `CMakeLists.txt`
- Create: `plugins/SimpleLFOFilter/DistrhoPluginInfo.h`
- Create: `plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp` (minimal, no DSP yet)

This task gets a VST3 building before the DSP/UI are wired in, so integration problems surface early. Read DPF references first — the submodule ships authoritative examples and headers.

- [ ] **Step 1: Add the DPF submodule**

Run:
```bash
git submodule add https://github.com/DISTRHO/DPF.git dpf
git -C dpf submodule update --init --recursive
```
Expected: `dpf/` populated; `dpf/CMakeLists.txt` and `dpf/distrho/DistrhoPlugin.hpp` exist.

- [ ] **Step 2: Read DPF reference files**

Read these to confirm the exact API used in later tasks (do not skip — they are the source of truth, this plan was written from memory of them):
- `dpf/distrho/DistrhoPlugin.hpp` — `Plugin`, `Parameter`, `ParameterRanges`, hint flags (`kParameterIsAutomatable`, `kParameterIsLogarithmic`), `d_version`, `d_cconst`.
- `dpf/distrho/DistrhoUI.hpp` — `UI`, `DISTRHO_UI_USE_NANOVG`, `onNanoDisplay`, `parameterChanged`, `setParameterValue`, `editParameter`, default-size macros.
- `dpf/dgl/NanoVG.hpp` — drawing API (`beginPath`, `fillColor`, `rect`, `fill`, `fontFace`, `text`, `loadSharedResources`) and `NanoSubWidget`.
- `dpf/examples/Parameters/` — a complete minimal DSP-only plugin (CMake + Plugin subclass) to mirror.
- Confirm `dpf_add_plugin` signature in `dpf/cmake/DPF-plugin.cmake` (argument names `TARGETS`, `FILES_DSP`, `FILES_UI`, `UI_TYPE`).

If any symbol/signature below differs from the headers, follow the headers and note the deviation in the commit message.

- [ ] **Step 3: Write `DistrhoPluginInfo.h`**

Create `plugins/SimpleLFOFilter/DistrhoPluginInfo.h`:

```cpp
#pragma once

#define DISTRHO_PLUGIN_BRAND        "Leozor"
#define DISTRHO_PLUGIN_NAME         "SimpleLFOFilter"
#define DISTRHO_PLUGIN_URI          "https://github.com/LeoFabre/dpf-simple-lfo-filter"

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0

#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_DEFAULT_WIDTH     500
#define DISTRHO_UI_DEFAULT_HEIGHT    350

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Filter"
```

- [ ] **Step 4: Write a minimal plugin (params only, silent passthrough DSP)**

Create `plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp`:

```cpp
#include "DistrhoPlugin.hpp"
#include "ParamInfo.h"
#include <cstring>

START_NAMESPACE_DISTRHO

class SimpleLFOFilterPlugin : public Plugin {
public:
    SimpleLFOFilterPlugin() : Plugin(kParamCount, 0, 0) {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fParams[i] = kParams[i].def;
    }

protected:
    const char* getLabel()       const override { return "SimpleLFOFilter"; }
    const char* getDescription() const override { return "Peak filter with LFO frequency modulation and gain."; }
    const char* getMaker()       const override { return "Leozor"; }
    const char* getHomePage()    const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense()     const override { return "ISC"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('L', 'z', 'L', 'f'); }

    void initParameter(uint32_t index, Parameter& p) override {
        const ParamDescriptor& d = kParams[index];
        p.hints      = kParameterIsAutomatable | (d.logarithmic ? kParameterIsLogarithmic : 0x0);
        p.name       = d.name;
        p.symbol     = d.symbol;
        p.ranges.def = d.def;
        p.ranges.min = d.min;
        p.ranges.max = d.max;
    }

    float getParameterValue(uint32_t index) const override { return fParams[index]; }
    void  setParameterValue(uint32_t index, float value) override { fParams[index] = value; }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
        // Placeholder passthrough; real DSP arrives in Task 5.
        for (uint32_t ch = 0; ch < DISTRHO_PLUGIN_NUM_OUTPUTS; ++ch)
            if (outputs[ch] != inputs[ch])
                std::memcpy(outputs[ch], inputs[ch], sizeof(float) * frames);
    }

private:
    float fParams[kParamCount];
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleLFOFilterPlugin)
};

Plugin* createPlugin() { return new SimpleLFOFilterPlugin(); }

END_NAMESPACE_DISTRHO
```

- [ ] **Step 5: Write the top-level `CMakeLists.txt`**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(SimpleLFOFilter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(dpf)

dpf_add_plugin(SimpleLFOFilter
    TARGETS vst3
    FILES_DSP
        plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp)

target_include_directories(SimpleLFOFilter
    PUBLIC plugins/SimpleLFOFilter)
```

- [ ] **Step 6: Configure and build**

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
Expected: build succeeds; a VST3 bundle appears under `build/bin/` (e.g. `build/bin/SimpleLFOFilter.vst3`).

- [ ] **Step 7: Commit**

```bash
git add .gitmodules dpf CMakeLists.txt plugins/SimpleLFOFilter/DistrhoPluginInfo.h plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp
git commit -m "build: vendor DPF and build VST3 skeleton with parameters"
```

---

## Task 5: Wire the DSP into the plugin

**Files:**
- Modify: `plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp`

- [ ] **Step 1: Add includes and DSP members**

In `SimpleLFOFilterPlugin.cpp`, add after `#include "ParamInfo.h"`:

```cpp
#include "Biquad.hpp"
#include "Lfo.hpp"
```

Replace the `private:` member block with:

```cpp
private:
    float        fParams[kParamCount];
    PeakCoeffs   fCoeffs;
    BiquadState  fStateL, fStateR;
    Lfo          fLfo;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleLFOFilterPlugin)
```

- [ ] **Step 2: Reset DSP state on activate**

Add these overrides to the `protected:` section (just above `run`):

```cpp
    void activate() override {
        fStateL.reset();
        fStateR.reset();
        fLfo.reset();
    }
```

- [ ] **Step 3: Replace `run()` with the real per-sample DSP**

Replace the placeholder `run()` body with:

```cpp
    void run(const float** inputs, float** outputs, uint32_t frames) override {
        const float* inL  = inputs[0];
        const float* inR  = inputs[1];
        float*       outL = outputs[0];
        float*       outR = outputs[1];

        const double sr    = getSampleRate();
        const double freq  = fParams[kParamFreq];
        const double q     = fParams[kParamReso];
        const double depth = fParams[kParamDepth];
        const double rate  = fParams[kParamRate];
        const double gain  = fParams[kParamGain];

        fLfo.setRate(rate, sr);

        for (uint32_t i = 0; i < frames; ++i) {
            const double modFreq = freq + fLfo.tick() * depth;  // clamped inside set()
            fCoeffs.set(sr, modFreq, q, gain);                  // keeps last good set if non-finite
            outL[i] = fStateL.process(inL[i], fCoeffs);
            outR[i] = fStateR.process(inR[i], fCoeffs);
        }
    }
```

- [ ] **Step 4: Rebuild**

Run: `cmake --build build -j`
Expected: build succeeds, no warnings about unused members.

- [ ] **Step 5: Verify the DSP tests still pass (no regression in headers)**

Run: `clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o /tmp/test_dsp && /tmp/test_dsp`
Expected: `ALL DSP TESTS PASSED`

- [ ] **Step 6: Commit**

```bash
git add plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp
git commit -m "feat: per-sample LFO-modulated peak filter DSP"
```

---

## Task 6: Vertical slider widget

**Files:**
- Create: `plugins/SimpleLFOFilter/VSlider.hpp`
- Create: `plugins/SimpleLFOFilter/VSlider.cpp`

No unit test (interactive widget); verified by build here and visually in Task 7.

- [ ] **Step 1: Write the header**

Create `plugins/SimpleLFOFilter/VSlider.hpp`:

```cpp
#pragma once
#include "NanoVG.hpp"

START_NAMESPACE_DGL

// A vertical slider drawn with NanoVG. Value is normalized [0,1]; the owner maps
// it to a parameter range. Reports drag start/change/end through a callback so
// the owner can call editParameter()/setParameterValue().
class VSlider : public NanoSubWidget {
public:
    class Callback {
    public:
        virtual ~Callback() {}
        virtual void sliderDragStarted(VSlider* slider) = 0;
        virtual void sliderValueChanged(VSlider* slider, float normalized) = 0;
        virtual void sliderDragFinished(VSlider* slider) = 0;
    };

    explicit VSlider(Widget* parent);   // pass the UI (a TopLevelWidget is-a Widget)

    void  setId(uint id) noexcept { fId = id; }
    uint  getId() const noexcept  { return fId; }

    void  setValue(float normalized, bool sendCallback);
    float getValue() const noexcept { return fValue; }

    void  setCallback(Callback* cb) noexcept { fCallback = cb; }

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;

private:
    Callback* fCallback = nullptr;
    uint  fId    = 0;
    float fValue = 0.0f;   // normalized [0,1]
    bool  fDragging = false;

    float valueFromY(double y) const;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VSlider)
};

END_NAMESPACE_DGL
```

- [ ] **Step 2: Write the implementation**

Create `plugins/SimpleLFOFilter/VSlider.cpp`:

```cpp
#include "VSlider.hpp"
#include <algorithm>

START_NAMESPACE_DGL

VSlider::VSlider(Widget* parent)
    : NanoSubWidget(parent) {}

void VSlider::setValue(float normalized, bool sendCallback) {
    normalized = std::min(1.0f, std::max(0.0f, normalized));
    if (d_isEqual(normalized, fValue))
        return;
    fValue = normalized;
    if (sendCallback && fCallback != nullptr)
        fCallback->sliderValueChanged(this, fValue);
    repaint();
}

float VSlider::valueFromY(double y) const {
    const double h = static_cast<double>(getHeight());
    if (h <= 0.0) return fValue;
    // top of the track is value 1.0, bottom is 0.0
    double v = 1.0 - (y / h);
    return static_cast<float>(std::min(1.0, std::max(0.0, v)));
}

void VSlider::onNanoDisplay() {
    const float w = getWidth();
    const float h = getHeight();
    const float trackW = 6.0f;
    const float trackX = (w - trackW) * 0.5f;

    // track
    beginPath();
    rect(trackX, 0.0f, trackW, h);
    fillColor(Color(0.20f, 0.20f, 0.20f));
    fill();

    // filled portion below the thumb
    const float thumbY = (1.0f - fValue) * h;
    beginPath();
    rect(trackX, thumbY, trackW, h - thumbY);
    fillColor(Color(0.55f, 0.35f, 0.10f));
    fill();

    // thumb (darkorange, matching the JUCE original)
    const float thumbH = 14.0f;
    beginPath();
    rect(0.0f, std::max(0.0f, thumbY - thumbH * 0.5f), w, thumbH);
    fillColor(Color(1.0f, 0.55f, 0.0f));
    fill();
}

bool VSlider::onMouse(const MouseEvent& ev) {
    if (ev.button != 1)
        return false;

    if (ev.press && contains(ev.pos)) {
        fDragging = true;
        if (fCallback != nullptr)
            fCallback->sliderDragStarted(this);
        setValue(valueFromY(ev.pos.getY()), true);
        return true;
    }

    if (!ev.press && fDragging) {
        fDragging = false;
        if (fCallback != nullptr)
            fCallback->sliderDragFinished(this);
        return true;
    }

    return false;
}

bool VSlider::onMotion(const MotionEvent& ev) {
    if (!fDragging)
        return false;
    setValue(valueFromY(ev.pos.getY()), true);
    return true;
}

END_NAMESPACE_DGL
```

- [ ] **Step 3: Add VSlider to the build and compile**

In `CMakeLists.txt`, change the `dpf_add_plugin` call to add a UI section:

```cmake
dpf_add_plugin(SimpleLFOFilter
    TARGETS vst3
    UI_TYPE opengl
    FILES_DSP
        plugins/SimpleLFOFilter/SimpleLFOFilterPlugin.cpp
    FILES_UI
        plugins/SimpleLFOFilter/VSlider.cpp)
```

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
Expected: build succeeds. (No UI class yet — DPF may warn about a missing `createUI`; if the link fails on `createUI`, that is expected and resolved in Task 7. To keep this task green, if the linker complains, proceed directly to Task 7 before re-running the build.)

- [ ] **Step 4: Commit**

```bash
git add plugins/SimpleLFOFilter/VSlider.hpp plugins/SimpleLFOFilter/VSlider.cpp CMakeLists.txt
git commit -m "feat(ui): NanoVG vertical slider widget"
```

---

## Task 7: Assemble the UI

**Files:**
- Create: `plugins/SimpleLFOFilter/SimpleLFOFilterUI.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the UI class**

Create `plugins/SimpleLFOFilter/SimpleLFOFilterUI.cpp`:

```cpp
#include "DistrhoUI.hpp"
#include "VSlider.hpp"
#include "ParamInfo.h"

START_NAMESPACE_DISTRHO

class SimpleLFOFilterUI : public UI, public VSlider::Callback {
public:
    SimpleLFOFilterUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT) {
        loadSharedResources();   // loads the built-in "sans" font

        const uint sliderW = 60;
        const uint sliderH = 200;
        const uint spacing  = 40;
        const uint topPad   = 90;   // room for title + per-slider label
        const uint totalW   = kParamCount * sliderW + (kParamCount - 1) * spacing;
        const uint startX   = (DISTRHO_UI_DEFAULT_WIDTH - totalW) / 2;

        for (uint i = 0; i < kParamCount; ++i) {
            fSliders[i] = new VSlider(this);
            fSliders[i]->setId(i);
            fSliders[i]->setCallback(this);
            fSliders[i]->setAbsolutePos(startX + i * (sliderW + spacing), topPad);
            fSliders[i]->setSize(sliderW, sliderH);
            fSliders[i]->setValue(normalize(i, kParams[i].def), false);
        }
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < kParamCount)
            fSliders[index]->setValue(normalize(index, value), false);
    }

    void onNanoDisplay() override {
        const float w = getWidth();

        // background
        beginPath();
        rect(0.0f, 0.0f, w, getHeight());
        fillColor(Color(0.16f, 0.16f, 0.18f));
        fill();

        // title
        fontFace("sans");
        fontSize(15.0f);
        fillColor(Color(1.0f, 1.0f, 1.0f));
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(w * 0.5f, 12.0f, "Peak Filter with LFO Modulation and Gain Control", nullptr);

        // per-slider labels
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        for (uint i = 0; i < kParamCount; ++i) {
            const float cx = fSliders[i]->getAbsoluteX() + fSliders[i]->getWidth() * 0.5f;
            text(cx, fSliders[i]->getAbsoluteY() - 6.0f, kParams[i].name, nullptr);
        }
    }

    // VSlider::Callback
    void sliderDragStarted(VSlider* s) override { editParameter(s->getId(), true); }
    void sliderDragFinished(VSlider* s) override { editParameter(s->getId(), false); }
    void sliderValueChanged(VSlider* s, float normalized) override {
        setParameterValue(s->getId(), denormalize(s->getId(), normalized));
    }

private:
    VSlider* fSliders[kParamCount];

    static float normalize(uint i, float value) {
        const ParamDescriptor& d = kParams[i];
        return (value - d.min) / (d.max - d.min);
    }
    static float denormalize(uint i, float normalized) {
        const ParamDescriptor& d = kParams[i];
        return d.min + normalized * (d.max - d.min);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleLFOFilterUI)
};

UI* createUI() { return new SimpleLFOFilterUI(); }

END_NAMESPACE_DISTRHO
```

- [ ] **Step 2: Add the UI file to the build**

In `CMakeLists.txt`, add the UI source to the `FILES_UI` list:

```cmake
    FILES_UI
        plugins/SimpleLFOFilter/SimpleLFOFilterUI.cpp
        plugins/SimpleLFOFilter/VSlider.cpp
```

- [ ] **Step 3: Configure and build the full plugin**

Run:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
Expected: build succeeds; `build/bin/SimpleLFOFilter.vst3` is produced and links `createUI`.

- [ ] **Step 4: Commit**

```bash
git add plugins/SimpleLFOFilter/SimpleLFOFilterUI.cpp CMakeLists.txt
git commit -m "feat(ui): assemble sliders, labels, and title"
```

---

## Task 8: Validation & manual verification

**Files:** none (verification only)

- [ ] **Step 1: Re-run the DSP unit tests**

Run: `clang++ -std=c++17 -Iplugins/SimpleLFOFilter tests/test_dsp.cpp -o /tmp/test_dsp && /tmp/test_dsp`
Expected: `ALL DSP TESTS PASSED`

- [ ] **Step 2: Validate the VST3 (if pluginval is installed)**

Run: `pluginval --strictness-level 5 --validate build/bin/SimpleLFOFilter.vst3`
Expected: `ALL TESTS PASSED`. If `pluginval` is not installed, skip and note it; do not treat absence as failure.

- [ ] **Step 3: Manual checks in a host**

Load `build/bin/SimpleLFOFilter.vst3` in a DAW and confirm:
- [ ] Five sliders render with labels and the centered title; dragging a slider moves the host parameter and vice-versa.
- [ ] With audio playing and `LFO Depth` > 0, the filter sweeps **smoothly** (no zipper/stepping) and the rate tracks `LFO Rate`.
- [ ] **Set `Filter Gain` to 0 — audio must keep flowing (no dropout/silence).** This is the bug-fix acceptance check.
- [ ] Set `LFO Depth` to 0 — the filter sits at `Filter Frequency`, audio passes cleanly.
- [ ] Sweeping `Filter Frequency` and `Filter Resonance` audibly changes the peak; no clicks/NaN dropouts at the extremes.

- [ ] **Step 4: Write the README and commit**

Create `README.md` summarizing: what the plugin is, how to build (`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`), the submodule init step, and a note that it is a DPF port of `juce-simple-lfo-filter` with a smooth LFO and a fix for the `Filter Gain = 0` divide-by-zero dropout.

```bash
git add README.md
git commit -m "docs: build instructions and port notes"
```

---

## Notes for the implementer

- **DPF API is the source of truth.** This plan was written from knowledge of the DPF headers, not a local copy. In Task 4 Step 2 you read the actual headers — if any signature differs (e.g. `MouseEvent`/`MotionEvent` field names, `loadSharedResources` availability, `dpf_add_plugin` argument spelling), follow the headers and adjust the code, noting the change in the commit.
- **NanoVG font:** `loadSharedResources()` registers a font named `"sans"`. If your DPF revision names it differently, use the name from `dpf/dgl/`.
- **The bug fix is the headline.** The `gainFactor < 1e-4` clamp in `Biquad.hpp` is what prevents the `alpha / A` divide-by-zero; the `test_gain_zero_is_finite` test and Task 8 Step 3's gain-0 check are its acceptance criteria. Do not remove the clamp to "simplify."
```
