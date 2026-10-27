#include "MasterChain.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
namespace
{
    constexpr int smoothEvery = 32; // samples between EQ coefficient updates

    float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    /** Fixed-length delay line on a ring buffer: write one sample, get the one from `length` samples ago. */
    inline float delayed (std::vector<float>& ring, int pos, float x) noexcept
    {
        const float out = ring[(size_t) pos];
        ring[(size_t) pos] = x;
        return out;
    }
}

// =====================================================================================
void MasterEq::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    first = true;
    reset();
}

void MasterEq::reset() noexcept
{
    for (auto& ch : filters)
        for (auto& f : ch)
            f.reset();
    untilUpdate = 0;
    first = true;
}

void MasterEq::design (Biquad& f, int band, const ChainSettings& s, double rate) noexcept
{
    const double hz = s.freq[(size_t) band], g = s.gain[(size_t) band];
    switch (band)
    {
        case ChainSettings::lowShelf:  f.setLowShelf (rate, hz, g); break;
        case ChainSettings::highShelf: f.setHighShelf (rate, hz, g); break;
        default:                       f.setPeak (rate, hz, std::max (0.1, (double) s.q[(size_t) band]), g); break;
    }
}

void MasterEq::updateFilters() noexcept
{
    if (first)
    {
        current = target;
        first = false;
    }
    else
    {
        // about 20 ms to follow a knob, so turning it does not click
        const float a = (float) (1.0 - std::exp (-smoothEvery / (0.02 * sr)));
        for (int b = 0; b < ChainSettings::numBands; ++b)
        {
            const auto i = (size_t) b;
            current.gain[i] += a * (target.gain[i] - current.gain[i]);
            current.freq[i] = std::exp (std::log (current.freq[i]) + a * (std::log (target.freq[i]) - std::log (current.freq[i])));
            current.q[i] += a * (target.q[i] - current.q[i]);
        }
    }
    for (int b = 0; b < ChainSettings::numBands; ++b)
    {
        design (filters[0][(size_t) b], b, current, sr);
        auto& r = filters[1][(size_t) b];
        const auto& l = filters[0][(size_t) b];
        r.b0 = l.b0; r.b1 = l.b1; r.b2 = l.b2; r.a1 = l.a1; r.a2 = l.a2;
    }
}

void MasterEq::process (float* left, float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (untilUpdate <= 0)
        {
            updateFilters();
            untilUpdate = smoothEvery;
        }
        --untilUpdate;
        double l = left[i], r = right[i];
        for (int b = 0; b < ChainSettings::numBands; ++b)
        {
            l = filters[0][(size_t) b].process (l);
            r = filters[1][(size_t) b].process (r);
        }
        left[i] = (float) l;
        right[i] = (float) r;
    }
}

double MasterEq::responseDb (const ChainSettings& s, double hz, double rate) noexcept
{
    double total = 0.0;
    for (int b = 0; b < ChainSettings::numBands; ++b)
    {
        Biquad f;
        design (f, b, s, rate);
        total += f.magnitudeDb (rate, hz);
    }
    return total;
}

// =====================================================================================
void TruePeakLimiter::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& d : detect)
        d.prepare (sr);
    lookahead = std::max (8, (int) std::lround (0.0015 * sr));
    holdLength = lookahead + 2;
    latencySamples = lookahead + detect[0].delay();

    delayL.assign ((size_t) latencySamples, 0.0f);
    delayR.assign ((size_t) latencySamples, 0.0f);
    needed.assign ((size_t) holdLength + 2, 1.0f);
    queue.assign ((size_t) holdLength + 2, 0);
    box.assign ((size_t) lookahead, 1.0f);
    reset();
}

void TruePeakLimiter::reset() noexcept
{
    for (auto& d : detect)
        d.reset();
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    delayPos = 0;
    qHead = qTail = 0;
    sampleIndex = 0;
    released = 1.0f;
    std::fill (box.begin(), box.end(), 1.0f);
    boxPos = 0;
    boxSum = (double) box.size();
    boxRecount = 0;
}

void TruePeakLimiter::process (float* left, float* right, int n, float gainDb, float ceilingDb, float releaseMs) noexcept
{
    const float drive = dbToGain (gainDb);
    const float ceiling = dbToGain (ceilingDb);
    const float aim = ceiling * 0.995f; // aim a hair under the ceiling
    const float relCoef = (float) std::exp (-1.0 / (std::max (1.0f, releaseMs) * 0.001 * sr));
    const int cap = (int) queue.size();
    float lowest = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float xl = left[i] * drive, xr = right[i] * drive;
        const double peak = std::max (detect[0].processSample (xl), detect[1].processSample (xr));
        const float need = peak > aim ? (float) (aim / peak) : 1.0f;

        // lowest needed gain over the hold window (monotonic queue)
        while (qTail > qHead && needed[(size_t) ((qTail - 1) % cap)] >= need)
            --qTail;
        needed[(size_t) (qTail % cap)] = need;
        queue[(size_t) (qTail % cap)] = sampleIndex;
        ++qTail;
        while (sampleIndex - queue[(size_t) (qHead % cap)] >= holdLength)
            ++qHead;
        const float held = needed[(size_t) (qHead % cap)];
        ++sampleIndex;

        // instant down, smooth release up
        released = held < released ? held : held - (held - released) * relCoef;

        // average over the look-ahead: reaches the needed gain right when the peak arrives
        boxSum += released - box[(size_t) boxPos];
        box[(size_t) boxPos] = released;
        boxPos = (boxPos + 1) % lookahead;
        if (++boxRecount >= 4096)
        {
            boxRecount = 0;
            double s = 0.0;
            for (auto v : box)
                s += v;
            boxSum = s;
        }
        const float g = (float) (boxSum / lookahead);
        lowest = std::min (lowest, g);

        const float yl = delayed (delayL, delayPos, xl), yr = delayed (delayR, delayPos, xr);
        delayPos = (delayPos + 1) % latencySamples;
        left[i] = std::clamp (yl * g, -ceiling, ceiling);
        right[i] = std::clamp (yr * g, -ceiling, ceiling);
    }

    const float grDb = lowest < 1.0f ? -20.0f * std::log10 (std::max (lowest, 1.0e-6f)) : 0.0f;
    if (grDb > reductionDb.load (std::memory_order_relaxed))
        reductionDb.store (grDb, std::memory_order_relaxed);
}

// =====================================================================================
void MasterChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlock = std::max (maxBlock, 1);
    eq.prepare (sr);
    limiter.prepare (sr, maxBlock);
    step = (float) (1.0 / (0.03 * sr));
    for (auto* v : { &eqL, &eqR, &limL, &limR, &dryL, &dryR })
        v->assign ((size_t) maxBlock, 0.0f);
    const auto lat = (size_t) std::max (1, limiter.latency());
    dryDelayL.assign (lat, 0.0f);
    dryDelayR.assign (lat, 0.0f);
    preDelayL.assign (lat, 0.0f);
    preDelayR.assign (lat, 0.0f);
    reset();
}

void MasterChain::reset() noexcept
{
    eq.reset();
    limiter.reset();
    for (auto* v : { &dryDelayL, &dryDelayR, &preDelayL, &preDelayR })
        std::fill (v->begin(), v->end(), 0.0f);
    dryPos = prePos = 0;
    eqMix = limMix = 0.0f;
}

void MasterChain::process (float* left, float* right, int numSamples, const ChainSettings& s) noexcept
{
    const int maxBlock = (int) eqL.size();
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);
        float* l = left + start;
        float* r = right + start;
        const int lat = (int) dryDelayL.size();

        // the input, delayed like the output (level-matched bypass)
        for (int i = 0; i < n; ++i)
        {
            dryL[(size_t) i] = delayed (dryDelayL, dryPos, l[i]);
            dryR[(size_t) i] = delayed (dryDelayR, dryPos, r[i]);
            dryPos = (dryPos + 1) % lat;
        }

        // ---- EQ
        if (s.eqOn || eqMix > 0.0f)
        {
            if (eqMix <= 0.0f)
                eq.reset();
            eq.setTarget (s);
            std::copy (l, l + n, eqL.begin());
            std::copy (r, r + n, eqR.begin());
            eq.process (eqL.data(), eqR.data(), n);
            const float target = s.eqOn ? 1.0f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                eqMix = eqMix < target ? std::min (target, eqMix + step) : std::max (target, eqMix - step);
                l[i] += eqMix * (eqL[(size_t) i] - l[i]);
                r[i] += eqMix * (eqR[(size_t) i] - r[i]);
            }
        }

        // ---- limiter (always the same delay, on or off)
        const bool limiting = s.limiterOn || limMix > 0.0f;
        if (limiting)
        {
            if (limMix <= 0.0f)
                limiter.reset();
            std::copy (l, l + n, limL.begin());
            std::copy (r, r + n, limR.begin());
            limiter.process (limL.data(), limR.data(), n, s.limiterGain, s.ceiling, s.releaseMs);
        }
        const float target = s.limiterOn ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float pl = delayed (preDelayL, prePos, l[i]), pr = delayed (preDelayR, prePos, r[i]);
            prePos = (prePos + 1) % lat;
            if (limiting)
            {
                limMix = limMix < target ? std::min (target, limMix + step) : std::max (target, limMix - step);
                l[i] = pl + limMix * (limL[(size_t) i] - pl);
                r[i] = pr + limMix * (limR[(size_t) i] - pr);
            }
            else
            {
                l[i] = pl;
                r[i] = pr;
            }
        }
    }
}
} // namespace wwma
