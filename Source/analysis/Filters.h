#pragma once

#include <cmath>

namespace wwma
{
constexpr double pi = 3.14159265358979323846;

/** A plain biquad in double precision (analysis filters run at very low
    frequencies, where float coefficients get noisy). Never allocates. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setNormalised (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    void setLowPass (double sr, double f, double q) noexcept
    {
        const auto w = omega (sr, f), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        setNormalised ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighPass (double sr, double f, double q) noexcept
    {
        const auto w = omega (sr, f), c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        setNormalised ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    /** ITU-R BS.1770-4 K-weighting, stage 1 (head related high shelf), for any sample rate. */
    void setKShelf (double sr) noexcept
    {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (pi * f0 / sr);
        const double vh = std::pow (10.0, g / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        setNormalised (vh + vb * k / q + k * k, 2.0 * (k * k - vh), vh - vb * k / q + k * k,
                       1.0 + k / q + k * k, 2.0 * (k * k - 1.0), 1.0 - k / q + k * k);
    }

    /** ITU-R BS.1770-4 K-weighting, stage 2 (RLB high pass). */
    void setKHighPass (double sr) noexcept
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (pi * f0 / sr);
        setNormalised (1.0, -2.0, 1.0, 1.0 + k / q + k * k, 2.0 * (k * k - 1.0), 1.0 - k / q + k * k);
    }

private:
    static double omega (double sr, double f) noexcept
    {
        const auto fc = std::fmin (std::fmax (f, 5.0), sr * 0.45);
        return 2.0 * pi * fc / sr;
    }
};

inline double powerToDb (double p) noexcept { return p > 1.0e-20 ? 10.0 * std::log10 (p) : -200.0; }
inline double gainToDb (double g) noexcept  { return g > 1.0e-10 ? 20.0 * std::log10 (g) : -200.0; }
} // namespace wwma
