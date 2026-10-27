#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Filters.h"
#include "LoudnessHistogram.h"
#include "Snapshot.h"
#include "TruePeak.h"
#include <atomic>
#include <memory>
#include <vector>

namespace wwma
{
/** Measures everything the advice needs. It only reads the audio and never changes it.

    Runs on the audio thread, never allocates there, and publishes a Snapshot
    every 50 ms through a lock-free triple buffer.

    - Loudness: BS.1770-4 K-weighting, momentary (400 ms), short-term (3 s),
      gated integrated, EBU loudness range, 4x oversampled true peak.
    - Spectrum: third-octave mid and side levels from a 16k-point FFT,
      as a 3 second running average ("now") and a gated song average.
    - Stereo: correlation now and over the song.
    - Drums: kick detection on a 5 ms envelope of the low end (how far each kick
      rises above the bass), and the spread of the snare/hat region envelope,
      used to spot a kick that drowns in the bass, or drums squashed flat. */
class Analyzer
{
public:
    void prepare (double sampleRate);
    void requestReset() noexcept { resetRequested.store (true); }

    /** Audio thread. right may equal left for a mono signal. */
    void process (const float* left, const float* right, int numSamples) noexcept;

    /** Any thread but the audio thread (one reader). */
    bool readSnapshot (Snapshot& s) noexcept { return published.read (s); }

    double getSampleRate() const noexcept { return sr; }

private:
    void reset() noexcept;
    void processChunk (const float* left, const float* right, int n) noexcept;
    void endSubBlock() noexcept;
    void endEnvelopeFrame() noexcept;
    void runFft() noexcept;
    bool gateOpen() const noexcept;
    void publish() noexcept;

    static constexpr int ringSize = 60;       // 3 s of 50 ms sub-blocks
    static constexpr int momentaryBlocks = 8; // 400 ms
    static constexpr int envRingSize = 600;   // 3 s of 5 ms frames

    double sr = 48000.0;
    std::atomic<bool> resetRequested { false };

    // loudness
    Biquad kShelf[2], kHigh[2];
    int subLen = 2400, subPos = 0;
    double subSum = 0.0;
    std::array<double, ringSize> ring {};
    int ringPos = 0, ringFilled = 0;
    double momentary = -200.0, shortTerm = -200.0, maxMomentary = -200.0, maxShortTerm = -200.0;
    LoudnessHistogram integratedHist, rangeHist;
    double integrated = -200.0, loudnessRange = 0.0;

    // true peak
    TruePeakChannel truePeak[2];
    float subPeak = 0.0f, peakMax = 0.0f, subPeakL = 0.0f, subPeakR = 0.0f;
    std::array<float, 6> recentL {}, recentR {};
    std::array<float, ringSize> peakRing {};

    // correlation
    double cLR = 0.0, cLL = 0.0, cRR = 0.0;
    double nowLR = 0.0, nowLL = 0.0, nowRR = 0.0;
    double songLR = 0.0, songLL = 0.0, songRR = 0.0;
    double subCoef = 0.0;

    // spectrum
    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 16384, hop = 4096, fifoPos = 0, hopCount = 0;
    std::vector<float> midFifo, sideFifo, window, work;
    struct BinWeight { int bin; float weight; };
    std::vector<BinWeight> binWeights;
    std::array<int, Bands::count + 1> bandStart {};
    std::array<double, Bands::count> midNowP {}, sideNowP {}, midSongP {}, sideSongP {};
    double fftNowCoef = 0.0;
    int nowFrames = 0;
    int64_t songFrames = 0;

    // drum and bass contrast
    Biquad lowHp[2], lowLp[2], highHp[2], highLp[2];
    int envLen = 240, envPos = 0;
    double lowEnv1 = 0.0, lowEnv2 = 0.0, highEnv1 = 0.0, highEnv2 = 0.0, lowEnvCoef = 0.0, highEnvCoef = 0.0;
    std::array<float, envRingSize> highEnv {}, scratch {};

    // kick detection on the low end envelope
    static constexpr int lineSize = 128;     // 640 ms of 5 ms frames
    std::array<float, lineSize> lowLine {};
    int64_t frameIndex = 0, lastOnset = -1000;
    std::array<int64_t, 8> pendingOnsets {};
    int numPending = 0;
    std::array<float, 16> kickSeps {};
    std::array<int64_t, 16> kickFrames {};
    int kickPos = 0;
    double kickSepSum = 0.0;
    int64_t kickSepCount = 0;
    std::array<int64_t, 8> onsetHistory {};
    int64_t onsetCount = 0;
    void detectKicks (float lowDb) noexcept;
    bool rhythmic() const noexcept;
    int envRingPos = 0, envFilled = 0;
    double highContrastNow = 0.0, highContrastSum = 0.0;
    int64_t contrastCount = 0;

    // balance, DC, clipping
    double dcSumL = 0.0, dcSumR = 0.0;
    int64_t dcCount = 0;
    float samplePeak = 0.0f;
    int clipRunL = 0, clipRunR = 0, clipEvents = 0;

    double secondsAnalysed = 0.0;
    uint32_t sequence = 0;

    TripleBuffer<Snapshot> published;
};
} // namespace wwma
