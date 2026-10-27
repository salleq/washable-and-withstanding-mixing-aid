#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/Theme.h"

class WwmaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer
{
public:
    explicit WwmaAudioProcessorEditor (WwmaAudioProcessor&);
    ~WwmaAudioProcessorEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // drop a reference track anywhere on the window
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Reads the newest measurements and redraws everything at once (also used by the screenshot tool). */
    void refresh (bool reassess = true);
    /** Skips the smoothing of the gauge (screenshot tool). */
    void settleDisplay() { state.displayScore = state.assessment.score; }
    /** Saves the current score: the cards then show what got better or worse since. */
    void saveSnapshot();
    /** Forgets the saved snapshots and the comparison point. */
    void clearSnapshots();
    /** Feeds the 3-minute loudness history (screenshot tool fills it without waiting). */
    void pushHistory();
    /** Re-reads the reference slots (screenshot tool, after loading synchronously). */
    /** A/B: true plays the reference instead of the mix. */
    void setListening (bool b);
    void referencesChanged() { refGeneration = -1; readReferences(); reassess(); }

    static constexpr int baseWidth = 1140, baseHeight = 800;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (WwmaAudioProcessorEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        WwmaAudioProcessorEditor& editor;
    };

    void timerCallback() override;
    void reassess();
    void updateTexts();
    void fillProfileBox();
    void fillTargetBox();
    void updateBaselineDiff();
    void loadSnapshots();
    void setBaselineFromCurrent();
    void browseReference (int slot);
    void loadReference (int slot, const juce::File& file);
    void readReferences();

    WwmaAudioProcessor& proc;
    wwma::ui::LookAndFeel lnf;
    wwma::ui::ViewState state;
    std::map<std::string, juce::String> baselineTitles;

    Content content { *this };
    wwma::ui::LoudnessBox loudness { state };
    wwma::ui::LevelMeters meters { state };
    wwma::ui::HistoryBox history { state };
    wwma::ui::TonalView tonal { state };
    wwma::ui::FindingsPanel findings { state };
    wwma::ui::ScoreBox score { state };
    wwma::ui::TranslationBox translation { state };
    wwma::ui::ProgressBox progress { state };
    wwma::ui::ReferenceBox refBox { state };
    wwma::ui::CompareBox compare { state };
    std::unique_ptr<juce::FileChooser> chooser;
    int refGeneration = -1;

    juce::ComboBox profileBox, targetBox;
    juce::TextButton mixButton, masterButton, nowButton, songButton, resetButton, langButton, mixAButton, refBButton;
    juce::ToggleButton autoResetToggle;
    juce::TooltipWindow tooltips { this, 600 };

    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WwmaAudioProcessorEditor)
};
