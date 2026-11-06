#include "ReferenceTracks.h"
#include "analysis/Analyzer.h"
#include <algorithm>

namespace wwma
{
ReferenceTracks::ReferenceTracks()
{
    formats.registerBasicFormats();
}

ReferenceTracks::~ReferenceTracks()
{
    alive->store (false);
    pool.removeAllJobs (true, 20000);
}

juce::String ReferenceTracks::wildcard()
{
    return formats.getWildcardForAllFormats();
}

bool ReferenceTracks::canRead (const juce::File& f)
{
    return f.existsAsFile() && formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

ReferenceTracks::Result ReferenceTracks::readAndMeasure (const juce::File& file, double rate, bool measure)
{
    Result res;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        res.error = "Could not read this file.";
        return res;
    }
    const double fileRate = reader->sampleRate;
    if (fileRate < 8000.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        res.error = "The file has no audio.";
        return res;
    }

    const auto len = (int) std::min ((juce::int64) reader->lengthInSamples, (juce::int64) (maxSeconds * fileRate));
    constexpr int pad = 256; // room for the resampler to read past the end
    juce::AudioBuffer<float> audio (2, len + pad);
    audio.clear();
    reader->read (&audio, 0, len, 0, true, true); // mono files fill both channels
    if (reader->numChannels == 1)
        audio.copyFrom (1, 0, audio, 0, 0, len);

    if (measure)
    {
        auto info = std::make_shared<ReferenceInfo>();
        info->file = file;
        info->name = file.getFileName();
        info->seconds = len / fileRate;

        auto analyzer = std::make_unique<Analyzer>();
        analyzer->prepare (fileRate);
        const float* l = audio.getReadPointer (0);
        const float* r = audio.getReadPointer (1);
        for (int i = 0; i < len; i += 4096)
        {
            analyzer->setSongPosition (i / fileRate);
            analyzer->process (l + i, r + i, juce::jmin (4096, len - i));
        }
        analyzer->readSnapshot (info->snap);
        analyzer->readTimeline (info->timeline);

        info->wave.assign (waveColumns, 0.0f);
        float top = 1.0e-6f;
        for (int c = 0; c < waveColumns; ++c)
        {
            const int from = (int) ((juce::int64) len * c / waveColumns);
            const int to = (int) ((juce::int64) len * (c + 1) / waveColumns);
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                peak = juce::jmax (peak, audio.getMagnitude (ch, from, juce::jmax (1, to - from)));
            info->wave[(size_t) c] = peak;
            top = juce::jmax (top, peak);
        }
        for (auto& w : info->wave)
            w /= top;
        res.info = info;
    }

    // playback copy at the host's rate
    auto pb = std::make_shared<Playback>();
    pb->rate = rate;
    if (std::abs (rate - fileRate) < 0.5)
    {
        pb->audio.setSize (2, len);
        for (int ch = 0; ch < 2; ++ch)
            pb->audio.copyFrom (ch, 0, audio, ch, 0, len);
    }
    else
    {
        const auto outLen = (int) ((double) len * rate / fileRate);
        pb->audio.setSize (2, juce::jmax (1, outLen));
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::WindowedSincInterpolator interp;
            interp.process (fileRate / rate, audio.getReadPointer (ch), pb->audio.getWritePointer (ch), pb->audio.getNumSamples());
        }
    }
    res.playback = pb;
    res.ok = true;
    return res;
}

void ReferenceTracks::load (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    auto& s = slots[(size_t) slot];
    const int job = ++s.job;
    s.status = Status::loading;
    s.error = {};
    ++generation;

    const double rate = playbackRate.load();
    auto flag = alive;
    pool.addJob ([this, flag, slot, job, file, rate]
    {
        if (! flag->load())
            return;
        auto res = readAndMeasure (file, rate, true);
        juce::MessageManager::callAsync ([this, flag, slot, job, res]() mutable
        {
            if (flag->load())
                install (slot, job, std::move (res), false);
        });
    });
}

bool ReferenceTracks::loadNow (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return false;
    const int job = ++slots[(size_t) slot].job;
    install (slot, job, readAndMeasure (file, playbackRate.load(), true), false);
    return slots[(size_t) slot].status == Status::ready;
}

void ReferenceTracks::install (int slot, int job, Result r, bool keepInfo)
{
    auto& s = slots[(size_t) slot];
    if (job != s.job)
        return; // a newer request replaced this one

    if (r.ok)
    {
        if (! keepInfo)
        {
            s.info = r.info;
            s.loopStart = s.loopEnd = 0.0; // a new file: the old loop doesn't apply
            s.resume = 0.0;
            s.earlier = false;
            s.pairs.clear();   // pairs and the alignment belong to the old file
            s.earlierOffset = 0.0;
            s.align = Align::none;
            s.alignKey = {};
        }
        s.playback = r.playback;
        s.status = Status::ready;
        s.error = {};
        if (active.load() < 0 || slots[(size_t) active.load()].status != Status::ready)
            active.store (slot);
    }
    else if (! keepInfo)
    {
        s.status = Status::failed;
        s.error = r.error;
        s.info.reset();
        s.playback.reset();
    }
    updateAudioSide();
    ++generation;
}

void ReferenceTracks::remove (int slot)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    auto& s = slots[(size_t) slot];
    ++s.job;
    s = Slot { Status::empty, {}, {}, {}, s.job, 0.0, 0.0 };
    if (active.load() == slot)
    {
        active.store (-1);
        for (int i = 0; i < numSlots; ++i)
            if (slots[(size_t) i].status == Status::ready)
            {
                active.store (i);
                break;
            }
    }
    updateAudioSide();
    ++generation;
}

ReferenceTracks::Status ReferenceTracks::getStatus (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].status : Status::empty;
}

juce::String ReferenceTracks::getError (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].error : juce::String();
}

std::shared_ptr<const ReferenceInfo> ReferenceTracks::getInfo (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].info : nullptr;
}

void ReferenceTracks::setActive (int slot)
{
    if (juce::isPositiveAndBelow (slot, numSlots) && slots[(size_t) slot].status == Status::ready)
    {
        active.store (slot);
        updateAudioSide();
        ++generation;
    }
}

void ReferenceTracks::setLoop (int slot, double start, double end)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    start = juce::jlimit (0.0, 1.0, start);
    end = juce::jlimit (0.0, 1.0, end);
    if (end < start)
        std::swap (start, end);
    if (end - start < 0.002)
        start = end = 0.0; // too short to be a loop
    slots[(size_t) slot].loopStart = start;
    slots[(size_t) slot].loopEnd = end;
    if (slot == active.load())
    {
        loopStartNow.store (start);
        loopEndNow.store (end);
    }
    ++generation;
}

void ReferenceTracks::setEarlier (int slot, bool isEarlier)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || slots[(size_t) slot].earlier == isEarlier)
        return;
    auto& s = slots[(size_t) slot];
    s.earlier = isEarlier;
    s.align = Align::none;   // lined up again, against what is recorded now
    s.alignKey = {};
    pushSync();
    ++generation;
}

// ---- following the mix

void ReferenceTracks::setSyncToMix (bool on)
{
    if (syncToMix == on)
        return;
    syncToMix = on;
    pushSync();
    ++generation;
}

void ReferenceTracks::addPair (int slot, double songSeconds, double refFraction)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || slots[(size_t) slot].info == nullptr)
        return;
    auto& s = slots[(size_t) slot];
    const SyncPair p { juce::jmax (0.0, songSeconds), juce::jlimit (0.0, 1.0, refFraction) * s.info->seconds };
    auto& v = s.pairs;
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const SyncPair& o) { return std::abs (o.song - p.song) < 1.0; }), v.end());
    v.insert (std::upper_bound (v.begin(), v.end(), p, [] (const SyncPair& a, const SyncPair& b) { return a.song < b.song; }), p);
    pushSync();
    ++generation;
}

void ReferenceTracks::removePair (int slot, int index)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || ! juce::isPositiveAndBelow (index, (int) slots[(size_t) slot].pairs.size()))
        return;
    auto& v = slots[(size_t) slot].pairs;
    v.erase (v.begin() + index);
    pushSync();
    ++generation;
}

void ReferenceTracks::setPairs (int slot, std::vector<SyncPair> pairs)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    std::sort (pairs.begin(), pairs.end(), [] (const SyncPair& a, const SyncPair& b) { return a.song < b.song; });
    slots[(size_t) slot].pairs = std::move (pairs);
    pushSync();
    ++generation;
}

std::vector<SyncPair> ReferenceTracks::getPairs (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].pairs : std::vector<SyncPair>();
}

double ReferenceTracks::refSecondsAt (const std::vector<SyncPair>& pairs, double songSeconds)
{
    if (pairs.empty())
        return songSeconds;
    auto it = std::upper_bound (pairs.begin(), pairs.end(), songSeconds, [] (double x, const SyncPair& p) { return x < p.song; });
    const auto& p = it == pairs.begin() ? pairs.front() : *std::prev (it);
    return p.ref + (songSeconds - p.song);
}

int64_t ReferenceTracks::SyncMap::offsetAt (int64_t hostSample) const noexcept
{
    if (song.empty())
        return base;
    auto it = std::upper_bound (song.begin(), song.end(), hostSample);
    return offset[it == song.begin() ? 0 : (size_t) (std::prev (it) - song.begin())];
}

double ReferenceTracks::getEarlierOffset (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].earlierOffset : 0.0;
}

void ReferenceTracks::setEarlierOffset (int slot, double seconds)
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return;
    slots[(size_t) slot].earlierOffset = seconds;
    pushSync();
    ++generation;
}

ReferenceTracks::Align ReferenceTracks::getAlignState (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].align : Align::none;
}

juce::String ReferenceTracks::getAlignKey (int slot) const
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slots[(size_t) slot].alignKey : juce::String();
}

void ReferenceTracks::align (int slot, std::vector<float> songMono, double songRate, double songStart, const juce::String& key)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || slots[(size_t) slot].playback == nullptr)
        return;
    auto& s = slots[(size_t) slot];
    s.align = Align::running;
    s.alignKey = key;
    ++generation;

    std::shared_ptr<const Playback> pb = s.playback;
    const int job = s.job;
    auto flag = alive;
    auto song = std::make_shared<std::vector<float>> (std::move (songMono));
    pool.addJob ([this, flag, slot, job, key, pb, songRate, songStart, song]
    {
        if (! flag->load())
            return;
        const int refLen = pb->audio.getNumSamples();
        std::vector<float> ref ((size_t) refLen);
        const float* l = pb->audio.getReadPointer (0);
        const float* r = pb->audio.getReadPointer (1);
        for (int i = 0; i < refLen; ++i)
            ref[(size_t) i] = 0.5f * (l[i] + r[i]);
        const int64_t at = std::abs (songRate - pb->rate) > 0.5 ? -1
                         : findInReference (song->data(), (int) song->size(), ref.data(), refLen, pb->rate,
                                            (int64_t) std::llround (songStart * pb->rate));
        const double rate = pb->rate;
        juce::MessageManager::callAsync ([this, flag, slot, job, key, at, rate, songStart]
        {
            if (flag->load())
                finishAlign (slot, job, key, at, rate, songStart);
        });
    });
}

bool ReferenceTracks::alignNow (int slot, std::vector<float> songMono, double songRate, double songStart, const juce::String& key)
{
    if (! juce::isPositiveAndBelow (slot, numSlots) || slots[(size_t) slot].playback == nullptr)
        return false;
    auto& s = slots[(size_t) slot];
    s.alignKey = key;
    const auto& audio = s.playback->audio;
    std::vector<float> ref ((size_t) audio.getNumSamples());
    for (int i = 0; i < audio.getNumSamples(); ++i)
        ref[(size_t) i] = 0.5f * (audio.getSample (0, i) + audio.getSample (1, i));
    const double rate = s.playback->rate;
    const int64_t at = std::abs (songRate - rate) > 0.5 ? -1
                     : findInReference (songMono.data(), (int) songMono.size(), ref.data(), (int) ref.size(), rate,
                                        (int64_t) std::llround (songStart * rate));
    finishAlign (slot, s.job, key, at, rate, songStart);
    return at >= 0;
}

void ReferenceTracks::finishAlign (int slot, int job, const juce::String& key, int64_t at, double rate, double songStart)
{
    auto& s = slots[(size_t) slot];
    if (s.job != job || s.alignKey != key)
        return; // a new file, or lined up again since
    if (at >= 0)
    {
        s.earlierOffset = (double) at / rate - songStart;
        s.align = Align::done;
    }
    else
        s.align = Align::failed;   // keeps the offset it had
    pushSync();
    ++generation;
}

int64_t ReferenceTracks::findInReference (const float* song, int songLen, const float* ref, int refLen, double rate, int64_t expectedAt)
{
    // 1. onsets, 100 per second: how much louder each 10 ms got. Another mix of the same song
    //    changes the tone and the level, not where the hits are.
    const int hop = juce::jmax (1, (int) std::lround (rate / 100.0));
    auto onsets = [hop] (const float* x, int len)
    {
        const int frames = len / hop;
        std::vector<float> e ((size_t) frames), d ((size_t) frames, 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            double sum = 0.0;
            for (int i = 0; i < hop; ++i)
                sum += (double) x[f * hop + i] * x[f * hop + i];
            e[(size_t) f] = (float) std::log10 (1.0e-10 + sum / hop);
        }
        for (int f = 1; f < frames; ++f)
            d[(size_t) f] = juce::jmax (0.0f, e[(size_t) f] - e[(size_t) f - 1]);
        return d;
    };
    auto fs = onsets (song, songLen);
    const auto fr = onsets (ref, refLen);
    const int m = (int) fs.size(), n = (int) fr.size();
    if (m < 200 || n < m)
        return -1;   // under 2 s of song, or a reference shorter than it

    double mean = 0.0;
    for (auto v : fs)
        mean += v;
    mean /= m;
    double normS = 0.0;
    for (auto& v : fs)
    {
        v -= (float) mean;
        normS += (double) v * v;
    }
    if (normS < 1.0e-9)
        return -1;   // no hits to go by
    normS = std::sqrt (normS);

    std::vector<double> c1 ((size_t) n + 1, 0.0), c2 ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
    {
        c1[(size_t) i + 1] = c1[(size_t) i] + fr[(size_t) i];
        c2[(size_t) i + 1] = c2[(size_t) i] + (double) fr[(size_t) i] * fr[(size_t) i];
    }
    std::vector<float> score ((size_t) (n - m + 1), -1.0f);
    float best = -1.0f;
    for (int k = 0; k <= n - m; ++k)
    {
        double dot = 0.0;
        for (int j = 0; j < m; ++j)
            dot += (double) fs[(size_t) j] * fr[(size_t) (k + j)];
        const double s1 = c1[(size_t) (k + m)] - c1[(size_t) k];
        const double var = c2[(size_t) (k + m)] - c2[(size_t) k] - s1 * s1 / m;
        score[(size_t) k] = var > 1.0e-12 ? (float) (dot / (normS * std::sqrt (var))) : -1.0f;
        best = juce::jmax (best, score[(size_t) k]);
    }
    if (best < 0.3f)
        return -1;
    // a song repeats itself: of the matches about as good, the one nearest to "no offset"
    int bestK = -1;
    for (int k = 0; k <= n - m; ++k)
        if (score[(size_t) k] >= best - 0.02f
            && (bestK < 0 || std::abs ((int64_t) k * hop - expectedAt) < std::abs ((int64_t) bestK * hop - expectedAt)))
            bestK = k;

    // 2. to the sample: the loudest second of the song, against the reference around that spot
    const int win = juce::jmin (songLen - 1, (int) rate);
    int w0 = 0;
    {
        double most = -1.0;
        for (int f = 0; f * hop + win + 1 <= songLen; f += 10)
        {
            double e = 0.0;
            for (int i = 0; i < win; i += 16)
                e += (double) song[f * hop + i] * song[f * hop + i];
            if (e > most)
            {
                most = e;
                w0 = f * hop;
            }
        }
    }
    const int64_t coarse = (int64_t) bestK * hop;
    int64_t bestLag = 0;
    double bestCorr = -2.0;
    for (int lag = -2 * hop; lag <= 2 * hop; ++lag)
    {
        const int64_t r0 = coarse + w0 + lag;
        if (r0 < 0 || r0 + win + 1 > refLen)
            continue;
        double dot = 0.0, es = 0.0, er = 0.0;
        for (int i = 0; i < win; ++i)
        {
            // first differences: the highs count too, not just the bass
            const double a = (double) song[w0 + i + 1] - song[w0 + i];
            const double b = (double) ref[r0 + i + 1] - ref[r0 + i];
            dot += a * b;
            es += a * a;
            er += b * b;
        }
        const double c = es > 0.0 && er > 0.0 ? dot / std::sqrt (es * er) : -2.0;
        if (c > bestCorr)
        {
            bestCorr = c;
            bestLag = lag;
        }
    }
    return juce::jmax ((int64_t) 0, coarse + bestLag);
}

std::pair<double, double> ReferenceTracks::getLoop (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, numSlots))
        return { 0.0, 0.0 };
    return { slots[(size_t) slot].loopStart, slots[(size_t) slot].loopEnd };
}

void ReferenceTracks::setCue (double fraction)
{
    pendingCue.store (juce::jlimit (0.0, 1.0, fraction));
}

void ReferenceTracks::setPlaybackRate (double hostRate)
{
    if (hostRate <= 0.0 || std::abs (hostRate - playbackRate.load()) < 0.5)
        return;
    playbackRate.store (hostRate);

    auto rebuild = [this, flag = alive, hostRate]
    {
        if (! flag->load())
            return;
        for (int i = 0; i < numSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            if (s.status != Status::ready || s.info == nullptr)
                continue;
            const int job = s.job;
            const auto file = s.info->file;
            pool.addJob ([this, flag, i, job, file, hostRate]
            {
                if (! flag->load())
                    return;
                auto res = readAndMeasure (file, hostRate, false);
                juce::MessageManager::callAsync ([this, flag, i, job, res]() mutable
                {
                    if (flag->load())
                        install (i, job, std::move (res), true);
                });
            });
        }
    };

    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && mm->isThisTheMessageThread())
        rebuild();
    else
        juce::MessageManager::callAsync (rebuild);
}

void ReferenceTracks::updateAudioSide()
{
    std::shared_ptr<Playback> next;
    const int a = active.load();
    loopStartNow.store (juce::isPositiveAndBelow (a, numSlots) ? slots[(size_t) a].loopStart : 0.0);
    loopEndNow.store (juce::isPositiveAndBelow (a, numSlots) ? slots[(size_t) a].loopEnd : 0.0);
    if (juce::isPositiveAndBelow (a, numSlots) && slots[(size_t) a].status == Status::ready)
        next = slots[(size_t) a].playback;

    std::shared_ptr<Playback> old;
    bool changed = false;
    {
        const juce::SpinLock::ScopedLockType lock (audioLock);
        if (next != current)
        {
            // each track keeps its place: switching back continues where it was left. A rebuilt
            // copy of the same track (new sample rate) also continues from the same spot.
            const double here = playPosition.load();
            if (juce::isPositiveAndBelow (currentSlot, numSlots) && currentSlot != a)
                slots[(size_t) currentSlot].resume = here;
            const double from = currentSlot == a ? here : (juce::isPositiveAndBelow (a, numSlots) ? slots[(size_t) a].resume : 0.0);
            old = std::exchange (current, next);
            currentSlot = next != nullptr ? a : -1;
            cueSamples = 0;
            freePosition = 0;
            expectedHost = -1;
            fadeLeft = 0;
            // an earlier version follows the song by its own offset (pushSync): only its own
            // play button continues from where it was left
            const bool earlierNow = next != nullptr && slots[(size_t) a].earlier;
            if (earlierNow)
                freePosition = (int64_t) (from * (double) next->audio.getNumSamples());
            pendingCue.store (next != nullptr && ! earlierNow ? from : -1.0);
            playPosition.store (from);
            changed = true;
        }
    }
    // old is released here, outside the lock
    pushSync (changed);
}

void ReferenceTracks::pushSync (bool slotChanged)
{
    auto map = std::make_shared<SyncMap>();
    const int a = active.load();
    const double rate = playbackRate.load();
    if (juce::isPositiveAndBelow (a, numSlots) && slots[(size_t) a].status == Status::ready)
    {
        const auto& s = slots[(size_t) a];
        if (s.earlier)
        {
            map->hasBase = true;
            map->base = (int64_t) std::llround (s.earlierOffset * rate);
        }
        else if (syncToMix)
            for (const auto& p : s.pairs)
            {
                map->song.push_back ((int64_t) std::llround (p.song * rate));
                map->offset.push_back ((int64_t) std::llround ((p.ref - p.song) * rate));
            }
    }

    std::shared_ptr<const SyncMap> old;
    {
        const juce::SpinLock::ScopedLockType lock (audioLock);
        const bool hadBase = syncMap != nullptr && syncMap->hasBase;
        // the earlier version's offset is applied when it changes or the slot does; otherwise a
        // click on its waveform (a jump of the user's own) stays
        if ((slotChanged && map->hasBase) || hadBase != map->hasBase || (hadBase && syncMap->base != map->base))
            applyBase = true;
        old = std::exchange (syncMap, std::shared_ptr<const SyncMap> (map));
    }
    // old is released here, outside the lock
}

bool ReferenceTracks::render (float* l, float* r, int n, int64_t hostSample, bool hostPlaying, float gainDb) noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (audioLock);
    if (! lock.isLocked() || current == nullptr)
        return false;

    const auto& audio = current->audio;
    const int64_t len = audio.getNumSamples();
    if (len <= 0)
        return false;

    // the reference follows the DAW's song position, unless WWMA's own play button runs it
    const bool synced = hostSample >= 0 && ! ownTransport.load();
    if (std::exchange (applyBase, false))
        cueSamples = syncMap != nullptr ? syncMap->base : 0;
    if (const double cue = pendingCue.exchange (-1.0); cue >= 0.0)
    {
        const auto target = (int64_t) (cue * (double) len);
        if (hostSample >= 0)
            cueSamples = target - hostSample;
        freePosition = target;
    }

    // without the DAW's song position (the standalone app, or a host that gives none) the
    // reference plays only while its own play button is on: B alone never starts it
    if ((synced && ! hostPlaying) || (! synced && ! ownTransport.load()))
    {
        expectedHost = -1;
        fadeLeft = 0;
        return false;
    }

    // a loop keeps playback between its two points
    int64_t loopA = 0, loopB = len;
    if (const double la = loopStartNow.load(), lb = loopEndNow.load(); lb > la)
    {
        loopA = juce::jlimit ((int64_t) 0, len - 1, (int64_t) (la * (double) len));
        loopB = juce::jlimit (loopA + 1, len, (int64_t) (lb * (double) len));
    }
    const int64_t loopLen = loopB - loopA;
    auto wrap = [&] (int64_t p)
    {
        if (p >= loopA && p < loopB)
            return p;
        p = (p - loopA) % loopLen;
        if (p < 0)
            p += loopLen;
        return p + loopA;
    };

    const int fadeN = juce::jmax (1, (int) (0.02 * current->rate));   // 20 ms
    int64_t pos = 0;
    if (synced)
    {
        // with pairs, the pair at or before this spot of the song says where the reference is
        const int64_t offset = syncMap != nullptr && ! syncMap->song.empty() ? syncMap->offsetAt (hostSample) : cueSamples;
        if (hostSample == expectedHost && offset != lastOffset)
        {
            fadeFrom = lastOffset;   // the DAW plays on and the reference jumps: fade from where it was
            fadeLeft = fadeN;
        }
        else if (hostSample != expectedHost)
            fadeLeft = 0;            // the DAW jumped too: nothing to fade from
        lastOffset = offset;
        expectedHost = hostSample + n;
        pos = wrap (hostSample + offset);
    }
    else
    {
        pos = freePosition;
        if (pos < loopA || pos >= loopB)
            pos = loopA; // outside the loop on its own transport: start at the marked point
        expectedHost = -1;
        fadeLeft = 0;
    }
    int64_t oldPos = fadeLeft > 0 ? wrap (hostSample + fadeFrom) : 0;

    const float target = juce::Decibels::decibelsToGain (juce::jlimit (-30.0f, 30.0f, gainDb));
    const float step = (target - gainNow) / (float) juce::jmax (1, n);
    const float* srcL = audio.getReadPointer (0);
    const float* srcR = audio.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        gainNow += step;
        float a = srcL[pos], b = srcR[pos];
        if (fadeLeft > 0)
        {
            const float w = (float) fadeLeft / (float) fadeN;
            a += w * (srcL[oldPos] - a);
            b += w * (srcR[oldPos] - b);
            --fadeLeft;
            if (++oldPos >= loopB)
                oldPos = loopA;
        }
        l[i] = a * gainNow;
        r[i] = b * gainNow;
        if (++pos >= loopB)
            pos = loopA;
    }
    freePosition = pos;
    playPosition.store ((double) pos / (double) len);
    return true;
}
} // namespace wwma
