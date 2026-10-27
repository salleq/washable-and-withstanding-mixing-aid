#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace wwma
{
/** Third-octave bands used everywhere (spectrum, targets, advice).
    Centres are 1000 * 2^(k/3) for k = -17 ... 12, i.e. 19.7 Hz ... 16 kHz. */
struct Bands
{
    static constexpr int count = 30;
    static constexpr int firstK = -17;

    static double centre (int band) noexcept { return 1000.0 * std::pow (2.0, (band + firstK) / 3.0); }
    static double lower (int band) noexcept  { return centre (band) * std::pow (2.0, -1.0 / 6.0); }
    static double upper (int band) noexcept  { return centre (band) * std::pow (2.0, 1.0 / 6.0); }

    /** Index of the band whose centre is closest to the given frequency. */
    static int nearest (double hz) noexcept
    {
        const int b = (int) std::lround (3.0 * std::log2 (hz / 1000.0)) - firstK;
        return b < 0 ? 0 : (b >= count ? count - 1 : b);
    }
};

/** The six broad ranges the advice talks about. Edges fall on third-octave edges. */
enum class Range { sub, bass, lowMid, mid, presence, air, count };

struct RangeInfo { int firstBand, lastBand; };

inline RangeInfo rangeBands (Range r) noexcept
{
    switch (r)
    {
        case Range::sub:      return { 0, 4 };    //   18 –   56 Hz
        case Range::bass:     return { 5, 8 };    //   56 –  140 Hz
        case Range::lowMid:   return { 9, 13 };   //  140 –  445 Hz
        case Range::mid:      return { 14, 20 };  //  445 – 2245 Hz
        case Range::presence: return { 21, 24 }; // 2.2k – 5.6 kHz
        case Range::air:      return { 25, 29 };  // 5.6k –  18 kHz
        case Range::count:    break;
    }
    return { 0, 0 };
}

constexpr int numRanges = (int) Range::count;

/** Everything the analyser has measured, published from the audio thread
    to the editor about 20 times a second. Levels are in dB. */
struct Snapshot
{
    float momentary = -200.0f, shortTerm = -200.0f, integrated = -200.0f;
    float maxMomentary = -200.0f, maxShortTerm = -200.0f;
    float loudnessRange = 0.0f;
    float truePeakMax = -200.0f;     // dBTP since reset
    float truePeakRecent = -200.0f;  // dBTP, last 3 seconds
    float peakNowL = -200.0f, peakNowR = -200.0f; // dBTP per channel, last 300 ms (level meters)
    float correlationNow = 1.0f, correlationSong = 1.0f;
    float balanceNow = 0.0f, balanceSong = 0.0f;   // left vs right power, dB (+ = left louder)
    float dcOffset = -200.0f;        // largest DC of the two channels since reset, dBFS
    float samplePeakMax = -200.0f;   // plain sample peak since reset, dBFS
    int clipEvents = 0;              // runs of 3+ samples stuck at full scale (flat-topped clipping)

    /** Kick vs bass separation (dB): at every kick in the low end (40-120 Hz), how far the
        kick's peak rises above the bass level just before it. Small = the bass covers the kick. */
    float lowContrastNow = 0.0f, lowContrastSong = 0.0f;
    bool kickNowValid = false, kickSongValid = false;
    /** Same for the snare / hat region (1.5-6 kHz): how much drum hits stick out. */
    float highContrastNow = 0.0f, highContrastSong = 0.0f;

    /** Tempo from how regularly drum hits repeat over the song (0 = not found yet). */
    float tempoBpm = 0.0f;
    float tempoConfidence = 0.0f;  // 0-1, how clearly one beat stands out
    float hitsPerSecond = 0.0f;    // transient density: clear hits per second, song average

    float secondsAnalysed = 0.0f;  // music above the gates since reset
    float secondsNow = 0.0f;       // how much of the "now" window is filled (0-3 s)

    std::array<float, Bands::count> midNow {}, sideNow {}, midSong {}, sideSong {};
    bool spectrumNowValid = false, spectrumSongValid = false;
    bool contrastNowValid = false, contrastSongValid = false;

    uint32_t sequence = 0;
};

/** Lock-free single-writer / single-reader triple buffer. The writer never waits,
    the reader always gets the newest complete value. */
template <typename T>
class TripleBuffer
{
public:
    T& writeBuffer() noexcept { return slots[(size_t) back]; }

    void publish() noexcept
    {
        back = middle.exchange (back | dirtyBit, std::memory_order_acq_rel) & indexMask;
    }

    /** Returns true when a newer value was available. */
    bool read (T& out) noexcept
    {
        if ((middle.load (std::memory_order_acquire) & dirtyBit) == 0)
            return false;
        front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;
        out = slots[(size_t) front];
        return true;
    }

private:
    static constexpr int dirtyBit = 4, indexMask = 3;
    std::array<T, 3> slots {};
    int back = 0, front = 1;
    std::atomic<int> middle { 2 };
};
} // namespace wwma
