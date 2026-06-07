#include "Biquad.hpp"
#include "Lfo.hpp"
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
    assert(std::fabs(mag - g) < 0.01 * g);
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
    (void) s.process(std::nan(""), c);           // inject NaN
    const float y = s.process(1.0f, c);           // next real sample
    assert(std::isfinite(y));
}

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

int main() {
    test_lfo_completes_one_cycle();
    test_lfo_range_and_start();
    test_peak_gain_at_center();
    test_unity_gain_is_flat();
    test_gain_zero_is_finite();
    test_state_self_heals();
    std::printf("ALL DSP TESTS PASSED\n");
    return 0;
}
