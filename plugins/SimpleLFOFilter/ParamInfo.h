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
