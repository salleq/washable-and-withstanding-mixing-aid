#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "advice/MixReport.h"
#include "analysis/Analyzer.h"
#include "advice/Advisor.h"
#include "advice/Profiles.h"
#include "reference/ReferenceTracks.h"
#include <atomic>
#include <optional>

/** WWMA never changes the sound: the output is always the input, bit for bit.
    It only measures and explains. */
class WwmaAudioProcessor : public juce::AudioProcessor
{
public:
    WwmaAudioProcessor();
    ~WwmaAudioProcessor() override = default;

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
    void learnReference (const wwma::Snapshot& s);

    wwma::Window getWindow() const;
    void setWindow (wwma::Window w);
    wwma::Lang getLang() const;
    void setLang (wwma::Lang l);
    bool getAutoReset() const { return autoReset.load(); }
    void setAutoReset (bool on);
    wwma::Stage getStage() const;
    void setStage (wwma::Stage s);

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void restoreReference();
    void restoreReferenceTracks();

    std::optional<wwma::Profile> reference;
    std::atomic<bool> autoReset { true };
    bool wasPlaying = false;

    juce::AudioBuffer<float> refScratch;
    float abMix = 0.0f, abStep = 0.0f;
    int wantedActiveRef = -1, savedRefGeneration = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WwmaAudioProcessor)
};
