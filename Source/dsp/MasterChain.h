#pragma once

#include "analysis/Filters.h"
#include "analysis/TruePeak.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wwma
{
/** The settings of the mastering EQ and limiter, exactly as the user set the knobs.
    WWMA never changes these by itself: it only shows suggested values next to them. */
struct ChainSettings
{
    enum Band { lowShelf, lowMid, highMid, highShelf, numBands };

    bool eqOn = false;
    std::array<float, numBands> freq { 100.0f, 250.0f, 3000.0f, 10000.0f };
    std::array<float, numBands> gain { 0.0f, 0.0f, 0.0f, 0.0f };   // dB
    std::array<float, numBands> q { 0.7f, 0.7f, 0.7f, 0.7f };      // used by the two bells

    bool limiterOn = false;
    float limiterGain = 0.0f;      // dB into the limiter
    float ceiling = -1.0f;         // dBTP
    float releaseMs = 100.0f;

    bool anyOn() const noexcept { return eqOn || limiterOn; }

    bool sameAs (const ChainSettings& o) const noexcept
    {
        return eqOn == o.eqOn && freq == o.freq && gain == o.gain && q == o.q && limiterOn == o.limiterOn
               && std::abs (limiterGain - o.limiterGain) < 1.0e-6f && std::abs (ceiling - o.ceiling) < 1.0e-6f
               && std::abs (releaseMs - o.releaseMs) < 1.0e-6f;
    }
};

/** Four-band mastering EQ: low shelf, two bells, high shelf. Runs in double precision. */
class MasterEq
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    /** Recalculates the filters (smoothed towards the new settings in small steps). */
    void setTarget (const ChainSettings& s) noexcept { target = s; }
    void process (float* left, float* right, int numSamples) noexcept;

    /** The EQ's response at a frequency for the given settings, dB (for drawing and for advice). */
    static double responseDb (const ChainSettings& s, double hz, double sampleRate = 48000.0) noexcept;

private:
    static void design (Biquad& f, int band, const ChainSettings& s, double sr) noexcept;
    void updateFilters() noexcept;

    double sr = 48000.0;
    ChainSettings target, current;
    std::array<std::array<Biquad, ChainSettings::numBands>, 2> filters {};
    int untilUpdate = 0;
    bool first = true;
};

/** Stereo-linked true peak limiter with a short look-ahead.

    Peaks are found on a 4x interpolated signal (the same one the true peak meter uses),
    so inter-sample peaks are caught too. The gain is the lowest needed gain over the
    look-ahead, released smoothly and then averaged over the look-ahead, which reaches the
    needed gain exactly when the peak arrives. A final clip at the ceiling catches
    rounding. The delay is fixed (see latency()). */
class TruePeakLimiter
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    int latency() const noexcept { return latencySamples; }

    /** Runs the limiter on audio that has already been delayed by nothing: the output is
        latency() samples late. */
    void process (float* left, float* right, int numSamples, float gainDb, float ceilingDb, float releaseMs) noexcept;

    /** Largest gain reduction since the last call, dB (positive). Message thread. */
    float takeGainReduction() noexcept { return reductionDb.exchange (0.0f); }

private:
    double sr = 48000.0;
    int lookahead = 72, holdLength = 74, latencySamples = 80;
    TruePeakChannel detect[2];

    std::vector<float> delayL, delayR; // audio delay line
    int delayPos = 0;

    // sliding minimum of the needed gain (monotonic queue in a ring)
    std::vector<float> needed;
    std::vector<int64_t> queue;
    int64_t sampleIndex = 0, qHead = 0, qTail = 0;

    float released = 1.0f;

    // moving average of the released gain over the look-ahead
    std::vector<float> box;
    int boxPos = 0;
    double boxSum = 0.0;
    int boxRecount = 0;

    std::atomic<float> reductionDb { 0.0f };
};

/** EQ then limiter, with a fixed latency so the DAW can line everything up whether
    the stages are on or off. With both stages off the output is the input, bit for bit,
    just delayed by the latency. Switching a stage on or off crossfades in 30 ms. */
class MasterChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    int latency() const noexcept { return limiter.latency(); }

    /** Processes in place. Also keeps a copy of the delayed input for the level-matched bypass. */
    void process (float* left, float* right, int numSamples, const ChainSettings& s) noexcept;

    /** The input, delayed like the output, for the samples of the last process() call
        (all of them when it was given at most maxBlock samples). */
    const float* dryLeft() const noexcept { return dryL.data(); }
    const float* dryRight() const noexcept { return dryR.data(); }

    float takeGainReduction() noexcept { return limiter.takeGainReduction(); }

private:
    MasterEq eq;
    TruePeakLimiter limiter;
    double sr = 48000.0;
    float eqMix = 0.0f, limMix = 0.0f, step = 0.001f;
    std::vector<float> eqL, eqR, limL, limR, dryL, dryR;
    std::vector<float> dryDelayL, dryDelayR; // input delayed by the latency
    int dryPos = 0;
    std::vector<float> preDelayL, preDelayR; // EQ output delayed by the latency (when the limiter is off)
    int prePos = 0;
};
} // namespace wwma
