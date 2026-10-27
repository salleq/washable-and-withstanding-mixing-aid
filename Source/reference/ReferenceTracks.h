#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "analysis/Snapshot.h"
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
    std::vector<float> wave;    // peak per column, 0..1, for the little waveform
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
    static constexpr int waveColumns = 200;

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

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("WWMA reference") };

    std::array<Slot, numSlots> slots;
    std::atomic<int> active { -1 }, generation { 0 };
    std::atomic<double> playbackRate { 48000.0 };

    // audio side: the active playback, swapped under the lock
    juce::SpinLock audioLock;
    std::shared_ptr<Playback> current;
    int64_t cueSamples = 0, freePosition = 0;
    std::atomic<double> pendingCue { -1.0 };
    std::atomic<double> playPosition { 0.0 };
    float gainNow = 1.0f;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE (ReferenceTracks)
};
} // namespace wwma
