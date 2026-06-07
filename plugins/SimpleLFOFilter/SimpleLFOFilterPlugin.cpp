#include "DistrhoPlugin.hpp"
#include "ParamInfo.h"
#include "Biquad.hpp"
#include "Lfo.hpp"

#if defined(__SSE2__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

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
        p.hints      = kParameterIsAutomatable | (d.logarithmic ? kParameterIsLogarithmic : 0x0u);
        p.name       = d.name;
        p.symbol     = d.symbol;
        p.ranges.def = d.def;
        p.ranges.min = d.min;
        p.ranges.max = d.max;
    }

    float getParameterValue(uint32_t index) const override { return fParams[index]; }
    void  setParameterValue(uint32_t index, float value) override { fParams[index] = value; }

    void activate() override {
        fStateL.reset();
        fStateR.reset();
        fLfo.reset();
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override {
       #if defined(__SSE2__)
        // Flush denormals to zero (perf) on x86 hosts; matches the original's
        // ScopedNoDenormals intent. No-op on non-SSE targets.
        const unsigned int oldMXCSR = _mm_getcsr();
        _mm_setcsr(oldMXCSR | 0x8040);  // FTZ | DAZ
       #endif

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

       #if defined(__SSE2__)
        _mm_setcsr(oldMXCSR);
       #endif
    }

private:
    float        fParams[kParamCount];
    PeakCoeffs   fCoeffs;
    BiquadState  fStateL, fStateR;
    Lfo          fLfo;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleLFOFilterPlugin)
};

Plugin* createPlugin() { return new SimpleLFOFilterPlugin(); }

END_NAMESPACE_DISTRHO
