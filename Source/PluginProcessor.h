#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "advice/MixReport.h"
#include "analysis/Analyzer.h"
#include "advice/Advisor.h"
#include "advice/Profiles.h"
#include "advice/TrackAdvice.h"
#include "reference/ReferenceTracks.h"
#include "dsp/MasterChain.h"
#include <atomic>
#include <optional>

/** WWMA measures and explains. The sound changes only when the user turns on the
    mastering EQ or limiter (both off by default) or listens to a reference track.
    With both stages off the output is the input, bit for bit (delayed by the limiter's
    fixed look-ahead, which the DAW compensates). WWMA never turns a knob by itself:
    it only shows suggested values next to them. */
class WwmaAudioProcessor : public juce::AudioProcessor
{
public:
    WwmaAudioProcessor();
    ~WwmaAudioProcessor() override { wwma::TrackBus::shared().leave (busId); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "WWMA Mixing Aid"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    wwma::Analyzer analyzer;

    /** Built-in profiles followed by the learned reference slot. */
    int getNumProfiles() const { return (int) wwma::builtInProfiles().size() + 1; }
    int referenceIndex() const { return (int) wwma::builtInProfiles().size(); }
    int getProfileIndex() const;
    void setProfileIndex (int index);
    /** The selected style profile with the loudness target applied. */
    wwma::Profile getProfile() const;
    /** True when the "Reference" preset has something to use: a loaded track or a learned one. */
    bool hasReference() const;
    /** The reference slot the target is built from: the active one, unless it is marked as my earlier version. -1: none. */
    int targetSlot() const;
    void learnReference (const wwma::Snapshot& s);

    wwma::Window getWindow() const;
    void setWindow (wwma::Window w);
    wwma::Lang getLang() const;
    void setLang (wwma::Lang l);
    bool getAutoReset() const { return autoReset.load(); }
    void setAutoReset (bool on);
    wwma::Stage getStage() const;
    void setStage (wwma::Stage s);

    /** Track mode: WWMA on a single kick, bass or kick + bass bus instead of the master.
        TrackRole::mix is the normal mode. The last track kind is remembered for the TRACK button. */
    wwma::TrackRole getTrackRole() const;
    void setTrackRole (wwma::TrackRole r);
    wwma::TrackRole getLastTrackKind() const;
    /** This instance on the shared track bus (other WWMA instances see its low end). */
    const int busId = wwma::TrackBus::shared().join();

    /** Loudness target: 0 = the style's own, then streaming, club, loud, Apple Music. */
    int getTarget() const;
    void setTarget (int t);
    static juce::StringArray targetNames (wwma::Lang lang);

    /** Snapshots (score and key metrics) for the progress chart and before/after, kept with the project. */
    std::vector<wwma::SavedSnapshot> getSnapshots() const;
    void addSnapshot (const wwma::SavedSnapshot& s);
    void clearScoreSnapshots();
    /** The user confirmed saving a new version of the DAW project for the newest snapshot. */
    void confirmLatestSnapshot();
    bool latestSnapshotNeedsConfirmation() const;

    /** Lesson mode: on/off, which lesson is open, and which ones are done. Kept with the project. */
    bool getShowDifference() const;
    void setShowDifference (bool on);
    bool getLearnMode() const;
    void setLearnMode (bool on);
    int getLessonIndex() const;
    void setLessonIndex (int i);
    juce::StringArray getLessonsDone() const;
    void markLessonDone (const juce::String& id);

    /** Where the last report was saved, so the next one goes next to it and can compare with it. */
    juce::File getReportFolder() const;
    void setReportFolder (const juce::File& f);

    // ---- mastering EQ and limiter (the user sets them, WWMA only suggests)
    /** Measures the input, before the EQ and limiter: the suggestions are based on it.
        It only runs while a stage is on or the editor asks for it (the panel is open):
        with both stages off the input is the output, and the main analyser already has it. */
    wwma::Analyzer inputAnalyzer;
    std::atomic<bool> inputAnalysisWanted { false };

    /** Standalone app only: nobody plays through a microphone there, so the audio interface input
        is ignored and "your mix" is a file loaded into the app (slot 0, with its own play button).
        Plugins in a DAW never use it. */
    wwma::ReferenceTracks mixTrack;
    juce::File getMixFile() const;
    void loadMixFile (const juce::File& file);

    /** The knobs as the user set them. */
    wwma::ChainSettings getChainSettings() const;
    /** Largest limiter gain reduction since the last call, dB. */
    float takeGainReduction() { return chain.takeGainReduction(); }
    /** Level-matched bypass: plays the input without the EQ and limiter, at the loudness the
        chain produces, so you judge the sound and not the volume. Never on unless the user turns it on. */
    std::atomic<bool> listenWithoutChain { false };
    std::atomic<float> withoutChainGainDb { 0.0f };
    bool getShowSuggestions() const;
    void setShowSuggestions (bool on);
    bool getChainOpen() const;
    void setChainOpen (bool on);

    // ---- reference tracks
    wwma::ReferenceTracks refs;
    /** A/B: true plays the active reference track instead of the mix. Never on unless the user turns it on. */
    std::atomic<bool> listenToReference { false };
    /** Gain for the reference so it plays as loud as the mix (set by the editor). */
    std::atomic<float> referenceGainDb { 0.0f };
    void loadReference (int slot, const juce::File& file);
    void removeReference (int slot);
    /** Message thread: keeps the saved state in step with the slots, and finishes restoring them. */
    void updateReferenceState();

    static constexpr const char* profileParamId = "profile";
    struct ChainIds
    {
        static constexpr const char* eqOn = "eqOn";
        static constexpr const char* freq[4] = { "eqLowFreq", "eqLowMidFreq", "eqHighMidFreq", "eqHighFreq" };
        static constexpr const char* gain[4] = { "eqLowGain", "eqLowMidGain", "eqHighMidGain", "eqHighGain" };
        static constexpr const char* q[4] = { "", "eqLowMidQ", "eqHighMidQ", "" };
        static constexpr const char* limOn = "limOn";
        static constexpr const char* limGain = "limGain";
        static constexpr const char* limCeiling = "limCeiling";
        static constexpr const char* limRelease = "limRelease";
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void restoreReference();
    void restoreReferenceTracks();

    std::optional<wwma::Profile> reference;
    std::atomic<bool> autoReset { true };
    bool wasPlaying = false;

    wwma::MasterChain chain;
    int chainBlock = 512;
    bool inputRunning = false;
    float withoutMix = 0.0f;
    struct ChainParams
    {
        std::atomic<float>* eqOn = nullptr;
        std::array<std::atomic<float>*, 4> freq {}, gain {}, q {};
        std::atomic<float>* limOn = nullptr;
        std::atomic<float>* limGain = nullptr;
        std::atomic<float>* limCeiling = nullptr;
        std::atomic<float>* limRelease = nullptr;
    } chainParams;

    juce::AudioBuffer<float> refScratch;
    float abMix = 0.0f, abStep = 0.0f;
    int wantedActiveRef = -1, savedRefGeneration = -1;
    std::array<std::pair<double, double>, wwma::ReferenceTracks::numSlots> wantedLoops {}; // loops saved with the project, set once the file is loaded
    std::array<bool, wwma::ReferenceTracks::numSlots> wantedEarlier {};                     // "my earlier version" marks, the same way

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WwmaAudioProcessor)
};
