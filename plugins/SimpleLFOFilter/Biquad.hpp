#pragma once
#include <cmath>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

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
        if (!std::isfinite(y)) { z1 = z2 = 0.0; return 0.0f; }  // self-heal before touching state
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }

    void reset() { z1 = z2 = 0.0; }
};
