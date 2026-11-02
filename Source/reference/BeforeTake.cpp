#include "BeforeTake.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
namespace
{
    constexpr size_t keepSpares = 6;    // a few seconds of real-time recording without help
    constexpr size_t maxSpares = 12;    // more than this are given back to the system
    constexpr float fromInt = 1.0f / 32767.0f;

    inline int16_t toInt (float x) noexcept
    {
        return (int16_t) std::lrint (juce::jlimit (-1.0f, 1.0f, x) * 32767.0f);
    }
}

void BeforeTake::Chunk::reset() noexcept
{
    std::fill (filled.begin(), filled.end(), (uint8_t) 0);
    std::fill (blockEnergy.begin(), blockEnergy.end(), 0.0f);
    std::fill (blockFrames.begin(), blockFrames.end(), (uint16_t) 0);
    generation = 0;
}

bool BeforeTake::Chunk::any() const noexcept
{
    return std::any_of (filled.begin(), filled.end(), [] (uint8_t f) { return f != 0; });
}

double BeforeTake::Chunk::meanSquare() const noexcept
{
    double e = 0.0, frames = 0.0;
    for (size_t b = 0; b < filled.size(); ++b)
        if (filled[b] != 0)
        {
            e += blockEnergy[b];
            frames += blockFrames[b];
        }
    return frames > 0.0 ? e / frames : 0.0;
}

int BeforeTake::Take::seconds() const
{
    return (int) std::count_if (chunks.begin(), chunks.end(), [] (const Chunk* c) { return c != nullptr; });
}

BeforeTake::BeforeTake()
    : live (std::make_unique<Take>()), before (std::make_unique<Take>())
{
}

BeforeTake::~BeforeTake() = default;

std::unique_ptr<BeforeTake::Chunk> BeforeTake::makeChunk() const
{
    auto c = std::make_unique<Chunk>();
    c->data.assign ((size_t) chunkLen * 2, 0);
    const size_t blocks = (size_t) ((chunkLen + blockSize - 1) / blockSize);
    c->filled.assign (blocks, 0);
    c->blockEnergy.assign (blocks, 0.0f);
    c->blockFrames.assign (blocks, 0);
    return c;
}

void BeforeTake::prepare (double sampleRate)
{
    if (sampleRate <= 0.0)
        return;
    const juce::SpinLock::ScopedLockType sl (lock);
    if (std::abs (sampleRate - rate.load()) > 0.5)
    {
        // a recording at another rate can't play back at the right speed: start over
        clearAllLocked();
        storage.clear();
        spares.clear();
        chunkLen = juce::jmax (1000, (int) std::lround (sampleRate));
        rate.store (sampleRate);
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        kShelf[ch].setKShelf (sampleRate);
        kHigh[ch].setKHighPass (sampleRate);
        kShelf[ch].reset();
        kHigh[ch].reset();
    }
    expected = -1;
}

BeforeTake::Chunk* BeforeTake::newChunk (bool allowAllocate) noexcept
{
    if (! spares.empty())
    {
        auto* c = spares.back();
        spares.pop_back();
        return c;
    }
    if (! allowAllocate || (int) storage.size() >= maxChunks)
        return nullptr;
    // offline render only: the host waits for us, so allocating here is fine
    try
    {
        storage.push_back (makeChunk());
        return storage.back().get();
    }
    catch (...)
    {
        return nullptr;
    }
}

void BeforeTake::release (Chunk* c)
{
    if (c == nullptr)
        return;
    c->reset();
    spares.push_back (c);
}

void BeforeTake::updateCounts()
{
    beforeSeconds.store (before->seconds());
    liveSeconds.store (live->seconds());
}

void BeforeTake::record (const float* l, const float* r, int n, int64_t hostSample, bool allowAllocate) noexcept
{
    if (n <= 0 || hostSample < 0 || l == nullptr || r == nullptr)
        return;
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || rate.load() <= 0.0)
    {
        expected = -1;
        return;
    }
    if (hostSample != expected)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            kShelf[ch].reset();
            kHigh[ch].reset();
        }
        lastBlock = -1;
    }
    expected = hostSample + n;

    const int64_t len = chunkLen;
    bool grew = false;
    for (int i = 0; i < n; ++i)
    {
        const int64_t pos = hostSample + i;
        const int64_t sec = pos / len;
        if (sec >= maxSeconds)
            break;
        auto& slot = live->chunks[(size_t) sec];
        if (slot == nullptr)
        {
            slot = newChunk (allowAllocate);
            if (slot == nullptr)
            {
                full.store (true);
                // skip the rest of this second
                i += (int) (len - (pos - sec * len)) - 1;
                continue;
            }
            grew = true;
        }
        auto& c = *slot;
        const int off = (int) (pos - sec * len);
        c.data[(size_t) off * 2] = toInt (l[i]);
        c.data[(size_t) off * 2 + 1] = toInt (r[i]);

        // loudness per block (K-weighted), replaced when the spot is played again
        const int64_t globalBlock = pos / blockSize;
        const auto b = (size_t) (off / blockSize);
        if (globalBlock != lastBlock)
        {
            c.blockEnergy[b] = 0.0f;
            c.blockFrames[b] = 0;
            lastBlock = globalBlock;
        }
        const double kl = kHigh[0].process (kShelf[0].process ((double) l[i]));
        const double kr = kHigh[1].process (kShelf[1].process ((double) r[i]));
        c.blockEnergy[b] += (float) (kl * kl + kr * kr);
        ++c.blockFrames[b];
        c.filled[b] = 1;
    }
    if (grew)
        liveSeconds.store (live->seconds());
}

int BeforeTake::render (float* l, float* r, int n, int64_t hostSample) noexcept
{
    if (n <= 0 || l == nullptr || r == nullptr)
        return 0;
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || hostSample < 0)
    {
        missingHere.store (hostSample < 0);
        return 0;
    }
    const float target = matchOn.load() ? juce::Decibels::decibelsToGain (matchDb.load()) : 1.0f;
    const float step = 1.0f / (0.05f * (float) chunkLen);   // ~50 ms to a new gain
    const int64_t len = chunkLen;
    int got = 0;
    for (int i = 0; i < n; ++i)
    {
        const int64_t pos = hostSample + i;
        const int64_t sec = pos / len;
        if (sec >= maxSeconds)
            break;
        const auto* c = before->chunks[(size_t) sec];
        if (c == nullptr)
            continue;
        const int off = (int) (pos - sec * len);
        if (c->filled[(size_t) (off / blockSize)] == 0)
            continue;
        gainNow += juce::jlimit (-step, step, target - gainNow);
        l[i] = c->data[(size_t) off * 2] * fromInt * gainNow;
        r[i] = c->data[(size_t) off * 2 + 1] * fromInt * gainNow;
        ++got;
    }
    missingHere.store (got * 2 < n);
    return got;
}

float BeforeTake::chunkDb (const Chunk& c)
{
    const double ms = c.meanSquare();
    return ms > 1.0e-12 ? (float) (10.0 * std::log10 (ms)) : -200.0f;
}

void BeforeTake::service (double songSeconds)
{
    // allocate outside the lock, so the audio thread is never kept waiting
    std::vector<std::unique_ptr<Chunk>> fresh;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const size_t room = (size_t) juce::jmax (0, maxChunks - (int) storage.size());
        const size_t want = spares.size() < keepSpares ? keepSpares - spares.size() : 0;
        fresh.resize (std::min (want, room));
    }
    if (rate.load() <= 0.0)
        return;
    for (auto& f : fresh)
        f = makeChunk();

    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& f : fresh)
    {
        if ((int) storage.size() >= maxChunks)
            break;
        spares.push_back (f.get());
        storage.push_back (std::move (f));
    }
    // too many spares (after a freeze or a clear): give the memory back
    while (spares.size() > maxSpares)
    {
        auto* c = spares.back();
        spares.pop_back();
        storage.erase (std::remove_if (storage.begin(), storage.end(), [c] (const std::unique_ptr<Chunk>& p) { return p.get() == c; }),
                       storage.end());
    }

    // level match: compare the before take with what plays now, around the play head
    auto meanDb = [] (double e, int count) { return count > 0 && e > 1.0e-12 ? 10.0 * std::log10 (e / count) : -200.0; };
    auto compare = [&] (int from, int to, double& dbBefore, double& dbLive)
    {
        double eb = 0.0, el = 0.0;
        int count = 0;
        for (int s = juce::jmax (0, from); s <= juce::jmin (maxSeconds - 1, to); ++s)
        {
            const auto* b = before->chunks[(size_t) s];
            const auto* v = live->chunks[(size_t) s];
            if (b == nullptr || v == nullptr)
                continue;
            const double mb = b->meanSquare(), ml = v->meanSquare();
            if (mb <= 1.0e-9 || ml <= 1.0e-9)   // silence says nothing about the level
                continue;
            eb += mb;
            el += ml;
            ++count;
        }
        dbBefore = meanDb (eb, count);
        dbLive = meanDb (el, count);
        return count;
    };
    double db = 0.0, dl = 0.0;
    int found = 0;
    if (songSeconds >= 0.0)
    {
        const int s = (int) songSeconds;
        found = compare (s - 4, s + 4, db, dl);
    }
    if (found == 0)
        found = compare (0, maxSeconds - 1, db, dl);
    if (found > 0 && db > -70.0 && dl > -70.0)
        matchDb.store ((float) juce::jlimit (-24.0, 24.0, dl - db));
}

bool BeforeTake::freeze()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (live->seconds() == 0)
        return false;
    const int gen = generation.load() + 1;
    for (size_t s = 0; s < live->chunks.size(); ++s)
    {
        auto*& c = live->chunks[s];
        if (c == nullptr)
            continue;
        release (before->chunks[s]);
        before->chunks[s] = c;
        c->generation = gen;
        c = nullptr;
    }
    generation.store (gen);
    full.store (false);
    updateCounts();
    return true;
}

void BeforeTake::clearBefore()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    importGeneration = -1;
    for (auto& c : before->chunks)
    {
        release (c);
        c = nullptr;
    }
    full.store (false);
    matchDb.store (0.0f);
    updateCounts();
}

void BeforeTake::clearAllLocked()
{
    importGeneration = -1;
    for (auto* t : { live.get(), before.get() })
        for (auto& c : t->chunks)
        {
            release (c);
            c = nullptr;
        }
    full.store (false);
    matchDb.store (0.0f);
    updateCounts();
}

void BeforeTake::clearAll()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    clearAllLocked();
}

std::vector<uint8_t> BeforeTake::beforeCoverage() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const int gen = generation.load();
    std::vector<uint8_t> out;
    for (size_t s = 0; s < before->chunks.size(); ++s)
        if (const auto* c = before->chunks[s])
        {
            out.resize (s + 1, 0);
            out[s] = c->generation == gen ? 2 : 1;
        }
    return out;
}

std::vector<uint8_t> BeforeTake::liveCoverage() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    std::vector<uint8_t> out;
    for (size_t s = 0; s < live->chunks.size(); ++s)
        if (live->chunks[s] != nullptr)
        {
            out.resize (s + 1, 0);
            out[s] = 2;
        }
    return out;
}

BeforeTake::Export BeforeTake::exportBefore() const
{
    Export e;
    const juce::SpinLock::ScopedLockType sl (lock);
    int first = -1, last = -1;
    for (int s = 0; s < maxSeconds; ++s)
        if (before->chunks[(size_t) s] != nullptr)
        {
            if (first < 0)
                first = s;
            last = s;
        }
    if (first < 0)
        return e;
    e.sampleRate = rate.load();
    e.firstSecond = first;
    const int seconds = last - first + 1;
    e.audio.setSize (2, seconds * chunkLen);
    e.audio.clear();
    e.covered.assign ((size_t) seconds, 0);
    e.loudness.assign ((size_t) seconds, -200.0f);
    for (int s = 0; s < seconds; ++s)
    {
        const auto* c = before->chunks[(size_t) (first + s)];
        if (c == nullptr)
            continue;
        e.covered[(size_t) s] = 1;
        e.loudness[(size_t) s] = chunkDb (*c);
        float* l = e.audio.getWritePointer (0, s * chunkLen);
        float* r = e.audio.getWritePointer (1, s * chunkLen);
        for (int i = 0; i < chunkLen; ++i)
            if (c->filled[(size_t) (i / blockSize)] != 0)
            {
                l[i] = c->data[(size_t) i * 2] * fromInt;
                r[i] = c->data[(size_t) i * 2 + 1] * fromInt;
            }
    }
    return e;
}

void BeforeTake::importBefore (const Export& e)
{
    const double hostRate = rate.load();
    if (hostRate <= 0.0 || e.sampleRate <= 0.0 || e.audio.getNumSamples() == 0)
        return;

    // to the host rate, if it was saved at another one
    juce::AudioBuffer<float> audio;
    const double ratio = e.sampleRate / hostRate;
    if (std::abs (ratio - 1.0) < 1.0e-9)
        audio.makeCopyOf (e.audio);
    else
    {
        const int outLen = (int) std::floor (e.audio.getNumSamples() / ratio);
        audio.setSize (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, e.audio.getReadPointer (juce::jmin (ch, e.audio.getNumChannels() - 1)), audio.getWritePointer (ch), outLen);
        }
    }

    // build the chunks without the lock, then swap them in
    std::vector<std::pair<int, std::unique_ptr<Chunk>>> made;
    Biquad shelf[2], high[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        shelf[ch].setKShelf (hostRate);
        high[ch].setKHighPass (hostRate);
    }
    for (size_t s = 0; s < e.covered.size(); ++s)
    {
        const int second = e.firstSecond + (int) s;
        if (e.covered[s] == 0 || second >= maxSeconds)
            continue;
        const int start = (int) s * chunkLen;
        if (start >= audio.getNumSamples())
            break;
        auto c = makeChunk();
        const int frames = juce::jmin (chunkLen, audio.getNumSamples() - start);
        const float* l = audio.getReadPointer (0, start);
        const float* r = audio.getReadPointer (1, start);
        for (int i = 0; i < frames; ++i)
        {
            c->data[(size_t) i * 2] = toInt (l[i]);
            c->data[(size_t) i * 2 + 1] = toInt (r[i]);
            const auto b = (size_t) (i / blockSize);
            const double kl = high[0].process (shelf[0].process ((double) l[i]));
            const double kr = high[1].process (shelf[1].process ((double) r[i]));
            c->blockEnergy[b] += (float) (kl * kl + kr * kr);
            ++c->blockFrames[b];
            c->filled[b] = 1;
        }
        made.emplace_back (second, std::move (c));
    }

    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& c : before->chunks)
    {
        release (c);
        c = nullptr;
    }
    const int gen = generation.load() + 1;
    for (auto& [second, c] : made)
    {
        if ((int) storage.size() >= maxChunks)
            break;
        c->generation = gen;
        before->chunks[(size_t) second] = c.get();
        storage.push_back (std::move (c));
    }
    generation.store (gen);
    updateCounts();
}
int BeforeTake::getChunkLength() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return chunkLen;
}

std::pair<int, int> BeforeTake::beforeSpan() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    int first = -1, last = -1;
    for (int s = 0; s < maxSeconds; ++s)
        if (before->chunks[(size_t) s] != nullptr)
        {
            if (first < 0)
                first = s;
            last = s;
        }
    return { first, last };
}

bool BeforeTake::readBeforeSecond (int second, float* l, float* r) const
{
    if (! juce::isPositiveAndBelow (second, maxSeconds))
        return false;
    const juce::SpinLock::ScopedLockType sl (lock);
    const auto* c = before->chunks[(size_t) second];
    if (c == nullptr)
        return false;
    for (int i = 0; i < chunkLen; ++i)
    {
        const bool has = c->filled[(size_t) (i / blockSize)] != 0;
        l[i] = has ? c->data[(size_t) i * 2] * fromInt : 0.0f;
        r[i] = has ? c->data[(size_t) i * 2 + 1] * fromInt : 0.0f;
    }
    return true;
}

void BeforeTake::beginImport()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& c : before->chunks)
    {
        release (c);
        c = nullptr;
    }
    const int gen = generation.load() + 1;
    generation.store (gen);
    importGeneration = gen;
    matchDb.store (0.0f);
    updateCounts();
}

bool BeforeTake::importSecond (int second, const float* l, const float* r, int frames)
{
    if (! juce::isPositiveAndBelow (second, maxSeconds) || frames <= 0)
        return false;
    int len = 0;
    double sr = 0.0;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (importGeneration < 0 || importGeneration != generation.load())
            return false;   // a freeze or a clear came in between: theirs wins
        len = chunkLen;
        sr = rate.load();
    }
    // build the chunk without the lock
    auto c = makeChunk();
    if ((int) c->data.size() != len * 2)
        return false;
    Biquad shelf[2], high[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        shelf[ch].setKShelf (sr);
        high[ch].setKHighPass (sr);
    }
    frames = juce::jmin (frames, len);
    for (int i = 0; i < frames; ++i)
    {
        c->data[(size_t) i * 2] = toInt (l[i]);
        c->data[(size_t) i * 2 + 1] = toInt (r[i]);
        const auto b = (size_t) (i / blockSize);
        const double kl = high[0].process (shelf[0].process ((double) l[i]));
        const double kr = high[1].process (shelf[1].process ((double) r[i]));
        c->blockEnergy[b] += (float) (kl * kl + kr * kr);
        ++c->blockFrames[b];
        c->filled[b] = 1;
    }

    const juce::SpinLock::ScopedLockType sl (lock);
    if (importGeneration != generation.load() || chunkLen != len || (int) storage.size() >= maxChunks)
        return false;
    c->generation = importGeneration;
    release (before->chunks[(size_t) second]);
    before->chunks[(size_t) second] = c.get();
    storage.push_back (std::move (c));
    return true;
}

void BeforeTake::endImport()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    importGeneration = -1;
    updateCounts();
}
} // namespace wwma
