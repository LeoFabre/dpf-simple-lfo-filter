#pragma once
#include <cmath>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

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
