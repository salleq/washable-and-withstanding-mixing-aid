#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "analysis/Snapshot.h"
#include "analysis/Timeline.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace wwma
{
/** What WWMA learned from a reference track file. */
struct ReferenceInfo
{
    juce::File file;
    juce::String name;          // file name without the folder
    double seconds = 0.0;
    Snapshot snap;              // the whole track, measured like a mix
    Timeline timeline;          // second by second, for the sections and the listening report
    std::vector<float> wave;    // peak per column, 0..1, for the little waveform
};

/** A spot of the DAW song paired with a spot of a reference, both in seconds. */
struct SyncPair
{
    double song = 0.0, ref = 0.0;
    bool operator== (const SyncPair& o) const { return song == o.song && ref == o.ref; }
};

/** Two reference track slots. A track is read and measured once on a
    background thread. For A/B listening it is kept in memory at the host's
    sample rate and played back loudness-matched, only while the user has
    chosen to listen to it.

    Thread use: everything is message thread, except render() (audio thread)
    and the atomics it reads. */
class ReferenceTracks
{
public:
    static constexpr int numSlots = 2;
    static constexpr double maxSeconds = 20.0 * 60.0;
    static constexpr int waveColumns = 800;

    enum class Status { empty, loading, ready, failed };

    ReferenceTracks();
    ~ReferenceTracks();

    /** Formats this build can read, e.g. "*.wav;*.aiff;..." */
    juce::String wildcard();
    bool canRead (const juce::File& f);

    /** Starts reading and measuring a file in the background. */
    void load (int slot, const juce::File& file);
    /** Same, but waits for it (tests, and hosts without a running message loop). */
    bool loadNow (int slot, const juce::File& file);
    void remove (int slot);

    Status getStatus (int slot) const;
    juce::String getError (int slot) const;
    std::shared_ptr<const ReferenceInfo> getInfo (int slot) const;
    /** Bumps whenever a slot changes, so the editor knows to redraw. */
    int getGeneration() const { return generation.load(); }

    int getActive() const { return active.load(); }
    void setActive (int slot);

    /** Jumps the active track's playback to this point, 0..1 (click on the waveform). */
    void setCue (double fraction);
    /** Where the playback of the active slot is now, 0..1. */
    double getPlayPosition() const { return playPosition.load(); }

    /** WWMA's own play/pause for the reference: plays it without the DAW's transport. */
    void setPlaying (bool shouldPlay) { ownTransport.store (shouldPlay); }
    bool isPlaying() const { return ownTransport.load(); }

    /** A loop region of a slot, 0..1 (end <= start: no loop). Kept per slot; playback of the
        active slot stays inside it. */
    void setLoop (int slot, double start, double end);
    void clearLoop (int slot) { setLoop (slot, 0.0, 0.0); }
    std::pair<double, double> getLoop (int slot) const;
    bool hasLoop (int slot) const { const auto l = getLoop (slot); return l.second > l.first; }

    /** The user marks a slot as their own earlier version of the song (not a real reference).
        WWMA then shows how the current mix moved against the reference, compared with that version. */
    void setEarlier (int slot, bool isEarlier);
    bool isEarlier (int slot) const { return juce::isPositiveAndBelow (slot, numSlots) && slots[(size_t) slot].earlier; }

    // ---- following the mix
    /** "Sync references to mix": while on, a click on a reference with the DAW at a spot pairs the
        two (addPair), and playback by the DAW's song position follows the pairs. */
    void setSyncToMix (bool on);
    bool getSyncToMix() const { return syncToMix; }
    /** Pairs this spot of the song (seconds) with this spot of the reference (0..1). A pair within
        a second of the same song spot is replaced. */
    void addPair (int slot, double songSeconds, double refFraction);
    void removePair (int slot, int index);
    void setPairs (int slot, std::vector<SyncPair> pairs);   // sorted by song position
    std::vector<SyncPair> getPairs (int slot) const;
    /** Where the reference is for this song position, seconds (the pair at or before it, else the
        first one). No pairs: the song position itself. */
    static double refSecondsAt (const std::vector<SyncPair>& pairs, double songSeconds);

    /** An earlier version is the same song: how far its bounce is from the song, seconds
        (reference time - song time). Found by lining it up against the BEFORE take; 0 until then. */
    double getEarlierOffset (int slot) const;
    void setEarlierOffset (int slot, double seconds);
    enum class Align { none, running, done, failed };
    Align getAlignState (int slot) const;
    /** What the last alignment of this slot was done against (empty: none yet). */
    juce::String getAlignKey (int slot) const;
    /** Lines the slot up against this stretch of the song (mono, at the host rate, starting at
        songStart seconds) in the background. A different rate than the playback copy's fails. key: what it was made from, see getAlignKey. */
    void align (int slot, std::vector<float> songMono, double songRate, double songStart, const juce::String& key);
    /** Same, but waits for it (tests). Returns true when a match was found. */
    bool alignNow (int slot, std::vector<float> songMono, double songRate, double songStart, const juce::String& key);
    /** Where `song` (mono) starts inside `ref` (mono), samples, both at `rate`: onsets first, then
        to the sample. expectedAt: where it would be with no offset; of matches about as good the
        nearest wins, since songs repeat themselves. -1: no clear match. */
    static int64_t findInReference (const float* song, int songLen, const float* ref, int refLen, double rate, int64_t expectedAt = 0);

    /** Host sample rate changes: playback copies are rebuilt for the new rate. */
    void setPlaybackRate (double hostRate);

    /** Audio thread. Writes the active reference (gain in dB) into l and r.
        hostSample < 0 means "no transport": the reference then plays on its own.
        Returns false when there is nothing to play. */
    bool render (float* l, float* r, int n, int64_t hostSample, bool hostPlaying, float gainDb) noexcept;

private:
    struct Playback
    {
        juce::AudioBuffer<float> audio;
        double rate = 0.0;
    };

    struct Slot
    {
        Status status = Status::empty;
        juce::String error;
        std::shared_ptr<const ReferenceInfo> info;
        std::shared_ptr<Playback> playback;
        int job = 0;   // newest load request, older results are dropped
        double loopStart = 0.0, loopEnd = 0.0;
        double resume = 0.0;   // 0..1, where this track was when another one took over
        bool earlier = false;  // the user's own earlier version
        std::vector<SyncPair> pairs;   // sorted by song position
        double earlierOffset = 0.0;    // seconds, reference - song
        Align align = Align::none;
        juce::String alignKey;
    };

    /** What render() needs to follow the song: offsets in samples (reference - song). */
    struct SyncMap
    {
        std::vector<int64_t> song, offset;   // pairs
        bool hasBase = false;                // an earlier version: one offset for the whole song
        int64_t base = 0;
        int64_t offsetAt (int64_t hostSample) const noexcept;
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        std::shared_ptr<ReferenceInfo> info;
        std::shared_ptr<Playback> playback;
    };

    Result readAndMeasure (const juce::File& file, double playbackRate, bool measure);
    void install (int slot, int job, Result r, bool keepInfo);
    void updateAudioSide();
    void pushSync (bool slotChanged = false);   // the active slot's pairs / offset to the audio side
    void finishAlign (int slot, int job, const juce::String& key, int64_t at, double rate, double songStart);

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("WWMA reference") };

    std::array<Slot, numSlots> slots;
    std::atomic<int> active { -1 }, generation { 0 };
    std::atomic<double> playbackRate { 48000.0 };

    // audio side: the active playback, swapped under the lock
    juce::SpinLock audioLock;
    std::shared_ptr<Playback> current;
    int currentSlot = -1;   // the slot `current` belongs to
    int64_t cueSamples = 0, freePosition = 0;
    std::atomic<double> pendingCue { -1.0 };
    std::atomic<double> playPosition { 0.0 };
    std::atomic<bool> ownTransport { false };
    std::atomic<double> loopStartNow { 0.0 }, loopEndNow { 0.0 }; // the active slot's loop
    float gainNow = 1.0f;
    bool syncToMix = false;
    std::shared_ptr<const SyncMap> syncMap;   // under audioLock
    bool applyBase = false;                   // under audioLock: start from the map's base offset
    int64_t expectedHost = -1, lastOffset = 0, fadeFrom = 0;   // audio side: a jump while playing crossfades
    int fadeLeft = 0;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE (ReferenceTracks)
};
} // namespace wwma
