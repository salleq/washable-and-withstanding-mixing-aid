#include "Analyzer.h"
#include <algorithm>

namespace wwma
{
void Analyzer::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;

    for (int c = 0; c < 2; ++c)
    {
        kShelf[c].setKShelf (sr);
        kHigh[c].setKHighPass (sr);
        truePeak[c].prepare (sr);
    }
    subLen = juce::jmax (1, (int) std::lround (sr * 0.05));
    envLen = juce::jmax (1, (int) std::lround (sr * 0.005));
    // power envelopes: two one-pole smoothers, slow enough to iron out the ripple of a 40 Hz tone
    lowEnvCoef = 1.0 - std::exp (-2.0 * pi * 20.0 / sr);
    highEnvCoef = 1.0 - std::exp (-2.0 * pi * 60.0 / sr);
    subCoef = 1.0 - std::exp (-0.05 / 3.0);

    // kick / bass region and snare / hat region, two cascaded 2nd order sections per edge
    for (int i = 0; i < 2; ++i)
    {
        lowHp[i].setHighPass (sr, 40.0, 0.707);
        lowLp[i].setLowPass (sr, 120.0, 0.707);
        highHp[i].setHighPass (sr, 1500.0, 0.707);
        highLp[i].setLowPass (sr, 6000.0, 0.707);
    }

    // FFT bins no wider than ~3 Hz, so the lowest third-octave bands are resolved
    int order = 10;
    while ((1 << order) < (int) (sr / 3.0) && order < 16)
        ++order;
    fftSize = 1 << order;
    hop = fftSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    midFifo.assign ((size_t) fftSize, 0.0f);
    sideFifo.assign ((size_t) fftSize, 0.0f);
    work.assign ((size_t) fftSize * 2, 0.0f);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / fftSize));
    fftNowCoef = 1.0 - std::exp (-(hop / sr) / 3.0);

    // how much of each FFT bin falls inside each third-octave band
    binWeights.clear();
    const double df = sr / fftSize;
    for (int b = 0; b < Bands::count; ++b)
    {
        bandStart[(size_t) b] = (int) binWeights.size();
        const double lo = Bands::lower (b), hi = juce::jmin (Bands::upper (b), sr * 0.5);
        for (int j = 1; j < fftSize / 2; ++j)
        {
            const double overlap = juce::jmin (hi, (j + 0.5) * df) - juce::jmax (lo, (j - 0.5) * df);
            if (overlap > 0.0)
                binWeights.push_back ({ j, (float) (overlap / df) });
        }
    }
    bandStart[Bands::count] = (int) binWeights.size();

    reset();
}

void Analyzer::reset() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        kShelf[c].reset();
        kHigh[c].reset();
        truePeak[c].reset();
        lowHp[c].reset(); lowLp[c].reset(); highHp[c].reset(); highLp[c].reset();
    }
    subPos = 0; subSum = 0.0;
    ring.fill (0.0); ringPos = ringFilled = 0;
    momentary = shortTerm = maxMomentary = maxShortTerm = integrated = -200.0;
    loudnessRange = 0.0;
    integratedHist.reset();
    rangeHist.reset();

    subPeak = peakMax = subPeakL = subPeakR = 0.0f;
    peakRing.fill (0.0f);
    recentL.fill (0.0f);
    recentR.fill (0.0f);

    cLR = cLL = cRR = nowLR = nowLL = nowRR = songLR = songLL = songRR = 0.0;
    dcSumL = dcSumR = 0.0;
    dcCount = 0;
    samplePeak = 0.0f;
    clipRunL = clipRunR = clipEvents = 0;

    std::fill (midFifo.begin(), midFifo.end(), 0.0f);
    std::fill (sideFifo.begin(), sideFifo.end(), 0.0f);
    fifoPos = hopCount = 0;
    midNowP.fill (0.0); sideNowP.fill (0.0); midSongP.fill (0.0); sideSongP.fill (0.0);
    nowFrames = 0; songFrames = 0;

    envPos = 0; lowEnv1 = lowEnv2 = highEnv1 = highEnv2 = 0.0;
    highEnv.fill (-200.0f);
    lowLine.fill (-200.0f);
    frameIndex = 0; lastOnset = -1000; numPending = 0;
    onsetHistory.fill (0); onsetCount = 0;
    kickSeps.fill (0.0f); kickFrames.fill (-100000); kickPos = 0;
    kickSepSum = 0.0; kickSepCount = 0;
    envRingPos = envFilled = 0;
    highContrastNow = highContrastSum = 0.0;
    contrastCount = 0;

    secondsAnalysed = 0.0;
}

bool Analyzer::gateOpen() const noexcept
{
    // only real music counts: skip silence, fades and count-ins
    if (momentary < -50.0)
        return false;
    return integrated < -100.0 || momentary > integrated - 20.0;
}

void Analyzer::process (const float* left, const float* right, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
        reset();

    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (numSamples - done, subLen - subPos);
        processChunk (left + done, right + done, n);
        done += n;
        subPos += n;
        if (subPos >= subLen)
        {
            subPos = 0;
            endSubBlock();
        }
    }
}

void Analyzer::processChunk (const float* left, const float* right, int n) noexcept
{
    const float pl = truePeak[0].process (left, n);
    const float pr = right == left ? pl : truePeak[1].process (right, n);
    subPeakL = juce::jmax (subPeakL, pl);
    subPeakR = juce::jmax (subPeakR, pr);
    subPeak = juce::jmax (subPeak, juce::jmax (pl, pr));
    dcCount += n;

    for (int i = 0; i < n; ++i)
    {
        const double l = left[i], r = right[i];

        const double kl = kHigh[0].process (kShelf[0].process (l));
        const double kr = kHigh[1].process (kShelf[1].process (r));
        subSum += kl * kl + kr * kr;

        cLR += l * r; cLL += l * l; cRR += r * r;

        // DC, sample peak, and flat-topped clipping (3+ samples in a row at full scale)
        dcSumL += l;
        dcSumR += r;
        const float al = std::abs ((float) l), ar = std::abs ((float) r);
        samplePeak = juce::jmax (samplePeak, al, ar);
        clipRunL = al >= 0.999f ? clipRunL + 1 : 0;
        clipRunR = ar >= 0.999f ? clipRunR + 1 : 0;
        if (clipRunL == 3 || clipRunR == 3)
            ++clipEvents;

        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        midFifo[(size_t) fifoPos] = (float) m;
        sideFifo[(size_t) fifoPos] = (float) s;
        if (++fifoPos == fftSize)
            fifoPos = 0;
        if (++hopCount == hop)
        {
            hopCount = 0;
            runFft();
        }

        const double lo = lowLp[1].process (lowLp[0].process (lowHp[1].process (lowHp[0].process (m)))) ;
        const double hi = highLp[1].process (highLp[0].process (highHp[1].process (highHp[0].process (m))));
        lowEnv1 += lowEnvCoef * (lo * lo - lowEnv1);
        lowEnv2 += lowEnvCoef * (lowEnv1 - lowEnv2);
        highEnv1 += highEnvCoef * (hi * hi - highEnv1);
        highEnv2 += highEnvCoef * (highEnv1 - highEnv2);
        if (++envPos == envLen)
        {
            envPos = 0;
            endEnvelopeFrame();
        }
    }
}

void Analyzer::endEnvelopeFrame() noexcept
{
    const double lowDb = powerToDb (lowEnv2), highDb = powerToDb (highEnv2);
    detectKicks ((float) lowDb);
    if (! gateOpen())
        return;
    highEnv[(size_t) envRingPos] = (float) highDb;
    envRingPos = (envRingPos + 1) % envRingSize;
    envFilled = juce::jmin (envFilled + 1, envRingSize);
}

bool Analyzer::rhythmic() const noexcept
{
    // Kicks come in a pattern: the gaps between the last onsets are (close to) whole
    // multiples of the shortest common gap. Random low end movement isn't, and is ignored.
    const int n = (int) juce::jmin<int64_t> (onsetCount, (int64_t) onsetHistory.size());
    if (n < 5)
        return false;
    std::array<int64_t, 8> t {};
    for (int k = 0; k < n; ++k)
        t[(size_t) k] = onsetHistory[(size_t) ((onsetCount - n + k) % (int64_t) onsetHistory.size())];
    std::array<double, 7> gaps {};
    for (int k = 1; k < n; ++k)
        gaps[(size_t) k - 1] = (double) (t[(size_t) k] - t[(size_t) k - 1]);
    if (gaps[(size_t) n - 2] > 300.0) // more than 1.5 s since the previous onset
        return false;
    auto sorted = gaps;
    std::sort (sorted.begin(), sorted.begin() + n - 1);
    const double unit = sorted[(size_t) (n - 1) / 3];
    int regular = 0;
    for (int k = 0; k < n - 1; ++k)
    {
        const double ratio = gaps[(size_t) k] / unit;
        const double whole = std::round (ratio);
        if (whole >= 1.0 && whole <= 4.0 && std::abs (ratio - whole) < 0.12 * whole)
            ++regular;
    }
    return regular >= (n - 1) * 3 / 4 + ((n - 1) * 3 % 4 != 0 ? 1 : 0);
}

void Analyzer::detectKicks (float lowDb) noexcept
{
    auto at = [this] (int64_t i) { return lowLine[(size_t) (i % lineSize)]; };
    const int64_t now = frameIndex;
    lowLine[(size_t) (now % lineSize)] = lowDb;

    // a kick: the low end rises at least 3 dB within 40 ms (at most one per 200 ms)
    if (now >= 16 && now - lastOnset > 40 && lowDb > -60.0f && lowDb - at (now - 8) > 3.0f && numPending < (int) pendingOnsets.size())
    {
        lastOnset = now;
        pendingOnsets[(size_t) numPending++] = now - 4; // the rise started about here
        onsetHistory[(size_t) (onsetCount++ % (int64_t) onsetHistory.size())] = now - 4;
    }

    // 100 ms after a kick: how far its peak rose above the bass just before it
    while (numPending > 0 && now - pendingOnsets[0] >= 20)
    {
        const int64_t o = pendingOnsets[0];
        float pre = 0.0f;
        for (int64_t i = o - 12; i <= o - 3; ++i)
            pre += at (i);
        pre /= 10.0f;
        float peak = -200.0f;
        for (int64_t i = o; i <= o + 16; ++i)
            peak = juce::jmax (peak, at (i));
        const float sep = peak - pre;
        if (gateOpen() && rhythmic())
        {
            kickSeps[(size_t) kickPos] = sep;
            kickFrames[(size_t) kickPos] = o;
            kickPos = (kickPos + 1) % (int) kickSeps.size();
            kickSepSum += sep;
            ++kickSepCount;
        }

        for (int k = 1; k < numPending; ++k)
            pendingOnsets[(size_t) k - 1] = pendingOnsets[(size_t) k];
        --numPending;
    }
    ++frameIndex;
}

void Analyzer::endSubBlock() noexcept
{
    // --- loudness
    ring[(size_t) ringPos] = subSum / subLen;
    ringPos = (ringPos + 1) % ringSize;
    ringFilled = juce::jmin (ringFilled + 1, ringSize);
    subSum = 0.0;

    auto meanOfLast = [this] (int count)
    {
        double sum = 0.0;
        for (int k = 1; k <= count; ++k)
            sum += ring[(size_t) ((ringPos - k + ringSize) % ringSize)];
        return -0.691 + powerToDb (sum / count);
    };

    if (ringFilled >= momentaryBlocks)
    {
        momentary = meanOfLast (momentaryBlocks);
        maxMomentary = juce::jmax (maxMomentary, momentary);
        integratedHist.add (momentary);
        integrated = integratedHist.integrated();
    }
    if (ringFilled > 0)
        shortTerm = meanOfLast (ringFilled);
    if (ringFilled >= ringSize)
    {
        maxShortTerm = juce::jmax (maxShortTerm, shortTerm);
        rangeHist.add (shortTerm);
        loudnessRange = rangeHist.range();
    }

    // --- true peak
    peakRing[(size_t) ((ringPos - 1 + ringSize) % ringSize)] = subPeak;
    peakMax = juce::jmax (peakMax, subPeak);
    subPeak = 0.0f;
    recentL[(size_t) (ringPos % (int) recentL.size())] = subPeakL;
    recentR[(size_t) (ringPos % (int) recentR.size())] = subPeakR;
    subPeakL = subPeakR = 0.0f;

    const bool open = gateOpen();

    // --- correlation
    nowLR += subCoef * (cLR - nowLR);
    nowLL += subCoef * (cLL - nowLL);
    nowRR += subCoef * (cRR - nowRR);
    if (open)
    {
        songLR += cLR; songLL += cLL; songRR += cRR;
        secondsAnalysed += (double) subLen / sr;
    }
    cLR = cLL = cRR = 0.0;

    // --- drum and bass contrast over the last 3 s of music
    if (envFilled >= envRingSize / 3)
    {
        // snare / hat region: loudest moments (95th percentile) versus the quieter ones (30th)
        auto contrast = [this] (const std::array<float, envRingSize>& env, double lowPercentile)
        {
            std::copy (env.begin(), env.begin() + envFilled, scratch.begin());
            auto* first = scratch.data();
            auto* last = first + envFilled;
            auto* p95 = first + (int) (0.95 * (envFilled - 1));
            std::nth_element (first, p95, last);
            const float hi = *p95;
            auto* pLow = first + (int) (lowPercentile * (envFilled - 1));
            std::nth_element (first, pLow, p95);
            return (double) (hi - *pLow);
        };
        highContrastNow = contrast (highEnv, 0.30);
        if (open && envFilled == envRingSize)
        {
            highContrastSum += highContrastNow;
            ++contrastCount;
        }
    }

    publish();
}

void Analyzer::runFft() noexcept
{
    const bool open = gateOpen();
    const bool audible = momentary > -60.0;
    if (! open && ! audible)
        return;

    std::array<double, Bands::count> midP {}, sideP {};
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto& fifo = pass == 0 ? midFifo : sideFifo;
        // oldest sample first
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = fifo[(size_t) ((fifoPos + i) % fftSize)] * window[(size_t) i];
        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (work.data(), true);

        auto& dest = pass == 0 ? midP : sideP;
        for (int b = 0; b < Bands::count; ++b)
        {
            double p = 0.0;
            for (int k = bandStart[(size_t) b]; k < bandStart[(size_t) b + 1]; ++k)
            {
                const auto& bw = binWeights[(size_t) k];
                const double mag = work[(size_t) bw.bin];
                p += bw.weight * mag * mag;
            }
            dest[(size_t) b] = p;
        }
    }

    if (audible)
    {
        const double k = nowFrames == 0 ? 1.0 : fftNowCoef;
        for (int b = 0; b < Bands::count; ++b)
        {
            midNowP[(size_t) b] += k * (midP[(size_t) b] - midNowP[(size_t) b]);
            sideNowP[(size_t) b] += k * (sideP[(size_t) b] - sideNowP[(size_t) b]);
        }
        ++nowFrames;
    }
    if (open)
    {
        for (int b = 0; b < Bands::count; ++b)
        {
            midSongP[(size_t) b] += midP[(size_t) b];
            sideSongP[(size_t) b] += sideP[(size_t) b];
        }
        ++songFrames;
    }
}

void Analyzer::publish() noexcept
{
    auto& s = published.writeBuffer();
    s.momentary = (float) momentary;
    s.shortTerm = (float) shortTerm;
    s.integrated = (float) integrated;
    s.maxMomentary = (float) maxMomentary;
    s.maxShortTerm = (float) maxShortTerm;
    s.loudnessRange = (float) loudnessRange;
    s.truePeakMax = (float) gainToDb (peakMax);
    s.truePeakRecent = (float) gainToDb (*std::max_element (peakRing.begin(), peakRing.end()));
    s.peakNowL = (float) gainToDb (*std::max_element (recentL.begin(), recentL.end()));
    s.peakNowR = (float) gainToDb (*std::max_element (recentR.begin(), recentR.end()));

    auto corr = [] (double lr, double ll, double rr)
    {
        const double d = std::sqrt (ll * rr);
        return d > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, lr / d) : 1.0f;
    };
    s.correlationNow = corr (nowLR, nowLL, nowRR);
    s.correlationSong = corr (songLR, songLL, songRR);

    auto balance = [] (double ll, double rr)
    {
        return ll > 1.0e-12 && rr > 1.0e-12 ? (float) (10.0 * std::log10 (ll / rr)) : 0.0f;
    };
    s.balanceNow = balance (nowLL, nowRR);
    s.balanceSong = balance (songLL, songRR);
    s.dcOffset = dcCount > 0 ? (float) gainToDb (juce::jmax (std::abs (dcSumL), std::abs (dcSumR)) / (double) dcCount) : -200.0f;
    s.samplePeakMax = (float) gainToDb (samplePeak);
    s.clipEvents = clipEvents;

    {
        // kicks of the last 3 seconds
        double sum = 0.0;
        int n = 0;
        for (size_t k = 0; k < kickSeps.size(); ++k)
            if (frameIndex - kickFrames[k] <= envRingSize + 50)
            {
                sum += kickSeps[k];
                ++n;
            }
        s.kickNowValid = n >= 3;
        s.lowContrastNow = n > 0 ? (float) (sum / n) : 0.0f;
        s.kickSongValid = kickSepCount >= 8;
        s.lowContrastSong = kickSepCount > 0 ? (float) (kickSepSum / (double) kickSepCount) : 0.0f;
    }
    s.highContrastNow = (float) highContrastNow;
    s.contrastNowValid = envFilled >= envRingSize / 3;
    s.contrastSongValid = contrastCount >= 20;
    s.highContrastSong = contrastCount > 0 ? (float) (highContrastSum / contrastCount) : 0.0f;

    s.secondsAnalysed = (float) secondsAnalysed;
    s.secondsNow = (float) (ringFilled * 0.05);

    s.spectrumNowValid = nowFrames >= 4;
    s.spectrumSongValid = songFrames >= 8;
    for (int b = 0; b < Bands::count; ++b)
    {
        s.midNow[(size_t) b] = (float) powerToDb (midNowP[(size_t) b]);
        s.sideNow[(size_t) b] = (float) powerToDb (sideNowP[(size_t) b]);
        const double n = songFrames > 0 ? (double) songFrames : 1.0;
        s.midSong[(size_t) b] = (float) powerToDb (midSongP[(size_t) b] / n);
        s.sideSong[(size_t) b] = (float) powerToDb (sideSongP[(size_t) b] / n);
    }
    s.sequence = ++sequence;
    published.publish();
}
} // namespace wwma
