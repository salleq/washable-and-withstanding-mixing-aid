#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "analysis/Filters.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace wwma
{
/** "Before": WWMA records its own output, keyed by the song position, so you can hear how
    the same spot sounded before your changes, without exporting.

    Two takes, both one-second chunks indexed by the song second:
    - live: fills all the time while the DAW plays (or exports). Playing a spot again
      replaces it, so it always holds the newest sound of every spot played.
    - before: what "Mark before" (or a snapshot) froze. Freezing lays the live take over the
      old before take, so parts you did not play again keep their older sound; each chunk
      remembers which freeze it came from.

    Playback follows the DAW's play head: loops, jumps and the whole song.

    Threads: record() and render() run on the audio thread and only ever try the lock (a
    block is skipped rather than waited for). Everything else is the message thread. The audio
    thread allocates only while the host renders offline (an export); in real time it takes
    chunks from spares that service() keeps ready. */
class BeforeTake
{
public:
    static constexpr int maxSeconds = 15 * 60;   // song positions beyond this are not recorded
    static constexpr int maxChunks = 20 * 60;    // memory cap for both takes together
    static constexpr int blockSize = 256;        // granularity of "this part was recorded"

    BeforeTake();
    ~BeforeTake();

    /** Message thread (prepareToPlay). A new sample rate forgets everything recorded. */
    void prepare (double sampleRate);
    double getSampleRate() const { return rate.load(); }

    // ---- audio thread
    /** Records the output at this song position (in samples). allowAllocate: offline render only. */
    void record (const float* l, const float* r, int n, int64_t hostSample, bool allowAllocate) noexcept;
    /** Writes the before take over l and r where it has audio, gain-matched. Leaves the rest as it is.
        Returns how many of the n samples came from the before take. */
    int render (float* l, float* r, int n, int64_t hostSample) noexcept;

    // ---- message thread
    /** Keeps spare chunks ready for the audio thread, and updates the level match for this
        song position (seconds, < 0: unknown). Call often (the editor's timer, or the processor). */
    void service (double songSeconds = -1.0);
    /** Live → before. Returns false when nothing has been recorded since the last freeze. */
    bool freeze();
    /** Forgets the before take (the live one keeps recording). */
    void clearBefore();
    /** Forgets everything. */
    void clearAll();

    bool hasBefore() const { return beforeSeconds.load() > 0; }
    /** Seconds of song that the before / live take covers. */
    int getBeforeSeconds() const { return beforeSeconds.load(); }
    int getLiveSeconds() const { return liveSeconds.load(); }
    /** Freezes so far (0: none). */
    int getGeneration() const { return generation.load(); }
    /** Loudness match applied to the before take, dB (positive: before was quieter). */
    float getMatchDb() const { return matchDb.load(); }
    void setMatchLoudness (bool on) { matchOn.store (on); }
    bool getMatchLoudness() const { return matchOn.load(); }
    /** True while the play head is at a spot the before take does not have (set by render()). */
    bool isMissingHere() const { return missingHere.load(); }
    /** True when the audio thread had to drop audio because the memory cap was reached. */
    bool isFull() const { return full.load(); }

    /** Per song second: 0 none, 1 recorded at an older freeze, 2 from the newest freeze. */
    std::vector<uint8_t> beforeCoverage() const;
    std::vector<uint8_t> liveCoverage() const;

    /** The before take as audio, for saving: start second and stereo audio of the covered span
        (missing parts are silent), plus which seconds are covered. Empty buffer if none. */
    struct Export
    {
        double sampleRate = 0.0;
        int firstSecond = 0;
        juce::AudioBuffer<float> audio;
        std::vector<uint8_t> covered;   // per second from firstSecond
        std::vector<float> loudness;    // K-weighted mean square per second, dB (-200: none)
    };
    Export exportBefore() const;
    /** Puts saved audio back as the before take (a different sample rate is resampled). */
    void importBefore (const Export& e);

    // ---- one second at a time, for saving and loading long songs without a big buffer (any thread)
    /** One second of audio at the host rate. */
    int getChunkLength() const;
    /** First and last song second of the before take, {-1, -1} if empty. */
    std::pair<int, int> beforeSpan() const;
    /** Copies one second of the before take into l and r (getChunkLength() frames; unrecorded parts
        are silent). Returns false, and leaves them alone, when that second has nothing. */
    bool readBeforeSecond (int second, float* l, float* r) const;
    /** Replaces the before take, one second at a time: begin, add the seconds, end. The audio must
        be at the host rate. A freeze or clear in between wins over the import. */
    void beginImport();
    bool importSecond (int second, const float* l, const float* r, int frames);
    void endImport();

private:
    struct Chunk
    {
        std::vector<int16_t> data;          // interleaved stereo, one second
        std::vector<uint8_t> filled;        // per blockSize frames: recorded
        std::vector<float> blockEnergy;     // K-weighted sum of squares per block
        std::vector<uint16_t> blockFrames;  // frames in that sum
        int generation = 0;                 // the freeze it came from
        void reset() noexcept;
        bool any() const noexcept;
        double meanSquare() const noexcept;
    };
    struct Take
    {
        std::vector<Chunk*> chunks = std::vector<Chunk*> ((size_t) maxSeconds, nullptr);
        int seconds() const;
    };

    std::unique_ptr<Chunk> makeChunk() const;
    Chunk* newChunk (bool allowAllocate) noexcept;   // under the lock
    void release (Chunk* c);                           // under the lock
    void updateCounts();                               // under the lock
    void clearAllLocked();
    static float chunkDb (const Chunk& c);

    mutable juce::SpinLock lock;
    std::vector<std::unique_ptr<Chunk>> storage;   // owns every chunk
    std::vector<Chunk*> spares;
    std::unique_ptr<Take> live, before;
    int chunkLen = 48000;

    std::atomic<double> rate { 0.0 };
    std::atomic<int> beforeSeconds { 0 }, liveSeconds { 0 }, generation { 0 };
    std::atomic<float> matchDb { 0.0f };
    std::atomic<bool> matchOn { true }, missingHere { false }, full { false };
    float gainNow = 1.0f;
    Biquad kShelf[2], kHigh[2];
    int64_t expected = -1;   // where the next recorded block should start, to reset the filters on jumps
    int64_t lastBlock = -1;
    int importGeneration = -1;   // the generation an import started at (-1: none running)  // the block written last, so a block played again starts its loudness afresh

    JUCE_DECLARE_NON_COPYABLE (BeforeTake)
};
} // namespace wwma
