#pragma once

#include "Filters.h"
#include <array>
#include <cmath>

namespace wwma
{
/** True peak meter (ITU-R BS.1770-4 annex 2 style): 4x oversampling with a
    polyphase windowed-sinc interpolator, then the absolute maximum.
    At 176.4 kHz and above the samples are already dense enough and the plain
    sample peak is used. */
class TruePeakChannel
{
public:
    static constexpr int factor = 4, tapsPerPhase = 16, numTaps = factor * tapsPerPhase;

    void prepare (double sampleRate) noexcept
    {
        oversample = sampleRate < 176000.0;
        const double centre = (numTaps - 1) * 0.5;
        for (int i = 0; i < numTaps; ++i)
        {
            const double x = (i - centre) / factor;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            h[(size_t) i] = sinc * kaiser ((i - centre) / centre, 8.0);
        }
        // every phase passes DC at unity gain
        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k)
                sum += h[(size_t) (p + factor * k)];
            for (int k = 0; k < tapsPerPhase; ++k)
                phase[(size_t) p][(size_t) k] = h[(size_t) (p + factor * k)] / sum;
        }
        reset();
    }

    void reset() noexcept
    {
        hist.fill (0.0);
        pos = 0;
    }

    /** Returns the true peak (linear) of the given samples. */
    float process (const float* x, int n) noexcept
    {
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double s = x[i];
            if (! oversample)
            {
                peak = std::fmax (peak, std::abs (s));
                continue;
            }
            pos = (pos == 0 ? tapsPerPhase : pos) - 1;
            hist[(size_t) pos] = hist[(size_t) (pos + tapsPerPhase)] = s; // mirrored, so reads are contiguous
            const double* xs = hist.data() + pos;
            for (int p = 0; p < factor; ++p)
            {
                const auto& c = phase[(size_t) p];
                double y = 0.0;
                for (int k = 0; k < tapsPerPhase; ++k)
                    y += c[(size_t) k] * xs[k];
                peak = std::fmax (peak, std::abs (y));
            }
        }
        return (float) peak;
    }

private:
    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    static double kaiser (double t, double beta) noexcept
    {
        const double a = 1.0 - t * t;
        return a <= 0.0 ? 0.0 : besselI0 (beta * std::sqrt (a)) / besselI0 (beta);
    }

    std::array<double, numTaps> h {};
    std::array<std::array<double, tapsPerPhase>, factor> phase {};
    std::array<double, 2 * tapsPerPhase> hist {};
    int pos = 0;
    bool oversample = true;
};
} // namespace wwma
