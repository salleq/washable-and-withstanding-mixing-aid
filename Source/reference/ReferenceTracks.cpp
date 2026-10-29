#include "ReferenceTracks.h"
#include "analysis/Analyzer.h"

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
            analyzer->process (l + i, r + i, juce::jmin (4096, len - i));
        analyzer->readSnapshot (info->snap);

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
            pendingCue.store (next != nullptr ? from : -1.0);
            playPosition.store (from);
        }
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
    if (const double cue = pendingCue.exchange (-1.0); cue >= 0.0)
    {
        const auto target = (int64_t) (cue * (double) len);
        if (hostSample >= 0)
            cueSamples = target - hostSample;
        freePosition = target;
    }

    if (synced && ! hostPlaying)
        return false;

    // a loop keeps playback between its two points
    int64_t loopA = 0, loopB = len;
    if (const double la = loopStartNow.load(), lb = loopEndNow.load(); lb > la)
    {
        loopA = juce::jlimit ((int64_t) 0, len - 1, (int64_t) (la * (double) len));
        loopB = juce::jlimit (loopA + 1, len, (int64_t) (lb * (double) len));
    }
    const int64_t loopLen = loopB - loopA;

    int64_t pos = synced ? hostSample + cueSamples : freePosition;
    if (pos < loopA || pos >= loopB)
    {
        if (synced)
        {
            pos = (pos - loopA) % loopLen;
            if (pos < 0)
                pos += loopLen;
            pos += loopA;
        }
        else
            pos = loopA; // outside the loop on its own transport: start at the marked point
    }

    const float target = juce::Decibels::decibelsToGain (juce::jlimit (-30.0f, 30.0f, gainDb));
    const float step = (target - gainNow) / (float) juce::jmax (1, n);
    const float* srcL = audio.getReadPointer (0);
    const float* srcR = audio.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        gainNow += step;
        l[i] = srcL[pos] * gainNow;
        r[i] = srcR[pos] * gainNow;
        if (++pos >= loopB)
            pos = loopA;
    }
    freePosition = pos;
    playPosition.store ((double) pos / (double) len);
    return true;
}
} // namespace wwma
