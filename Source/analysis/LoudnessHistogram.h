#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace wwma
{
/** Keeps a histogram of block loudness values, so gated measurements (BS.1770
    integrated loudness, EBU Tech 3342 loudness range) can be computed for a song
    of any length without storing every block. Bins are 0.05 LU wide. */
class LoudnessHistogram
{
public:
    static constexpr double minDb = -70.0, maxDb = 10.0, step = 0.05;
    static constexpr int numBins = 1600;

    void reset() noexcept
    {
        counts.fill (0);
        energy.fill (0.0);
        total = 0;
    }

    /** Adds one block. Blocks at or below the -70 LUFS absolute gate are ignored. */
    void add (double lufs) noexcept
    {
        if (! (lufs > minDb))
            return;
        const int bin = binFor (lufs);
        counts[(size_t) bin]++;
        energy[(size_t) bin] += std::pow (10.0, (lufs + 0.691) / 10.0);
        ++total;
    }

    int count() const noexcept { return (int) total; }

    /** BS.1770-4 integrated loudness: absolute gate -70 LUFS, relative gate -10 LU. */
    double integrated() const noexcept
    {
        if (total == 0)
            return -200.0;
        const double gate = meanLoudness (0) - 10.0;
        const int first = firstBinAbove (gate);
        return meanLoudness (first);
    }

    /** EBU Tech 3342 loudness range from short-term blocks: relative gate -20 LU,
        then the spread between the 10th and the 95th percentile. */
    double range() const noexcept
    {
        if (total == 0)
            return 0.0;
        const double gate = meanLoudness (0) - 20.0;
        const int first = firstBinAbove (gate);
        uint64_t n = 0;
        for (int i = first; i < numBins; ++i)
            n += counts[(size_t) i];
        if (n < 2)
            return 0.0;
        const double lo = percentile (first, n, 0.10), hi = percentile (first, n, 0.95);
        return hi - lo;
    }

private:
    static int binFor (double lufs) noexcept
    {
        const int b = (int) std::floor ((lufs - minDb) / step);
        return b < 0 ? 0 : (b >= numBins ? numBins - 1 : b);
    }

    static double binCentre (int b) noexcept { return minDb + (b + 0.5) * step; }

    int firstBinAbove (double gate) const noexcept
    {
        // a block belongs above the gate when its bin centre is above it (0.05 LU resolution)
        for (int i = 0; i < numBins; ++i)
            if (binCentre (i) > gate)
                return i;
        return numBins;
    }

    double meanLoudness (int firstBin) const noexcept
    {
        double e = 0.0;
        uint64_t n = 0;
        for (int i = firstBin; i < numBins; ++i)
        {
            e += energy[(size_t) i];
            n += counts[(size_t) i];
        }
        return n > 0 ? -0.691 + 10.0 * std::log10 (e / (double) n) : -200.0;
    }

    double percentile (int firstBin, uint64_t n, double p) const noexcept
    {
        const double target = p * (double) (n - 1);
        uint64_t seen = 0;
        for (int i = firstBin; i < numBins; ++i)
        {
            seen += counts[(size_t) i];
            if ((double) seen > target)
                return binCentre (i);
        }
        return binCentre (numBins - 1);
    }

    std::array<uint32_t, numBins> counts {};
    std::array<double, numBins> energy {};
    uint64_t total = 0;
};
} // namespace wwma
