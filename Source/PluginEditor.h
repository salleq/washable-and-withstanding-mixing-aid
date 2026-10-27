#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "advice/StyleGuess.h"

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
    /** The current style guess, and what clicking its suggestion does (also used by the self test). */
    const wwma::StyleGuess& getStyleGuess() const noexcept { return styleGuess; }
    bool isStyleSuggestionShown() const noexcept { return styleButton.isVisible() && styleSuggests; }
    void acceptStyleSuggestion() { styleButton.onClick(); }
    /** The user confirms the DAW project was saved as a new version for the newest snapshot. */
    void confirmSnapshotSaved();
    bool isAwaitingSaveConfirmation() const noexcept { return state.awaitingSaveConfirm; }
    /** Before/after panel (newest snapshot vs now). */
    void setCompareOpen (bool open);
    const std::vector<wwma::MetricChange>& getChanges() const noexcept { return state.changes; }
    /** Lesson mode. */
    void setLearnMode (bool on);
    /** The snapshot's ghost curve in the tonal view, and the filled difference to it. */
    void setShowDifference (bool b) { tonal.setShowDifference (b); proc.setShowDifference (b); }
    bool getShowDifference() const noexcept { return tonal.getShowDifference(); }
    void showLesson (int index);
    const wwma::LessonCheck& getLessonCheck() const noexcept { return state.lesson; }
    /** Writes the report (Markdown) and its JSON next to it, comparing with the newest earlier
        report in the same folder. Returns the earlier report it compared with, if any. */
    juce::File saveReport (const juce::File& markdownFile);
    /** The reference's own play/pause (plays it without the DAW's transport, loop included). */
    void toggleReferencePlay (int slot);
    /** The big reference view, where a loop is chosen. */
    void openReferenceDetail (int slot);
    void closeReferenceDetail();
    bool isReferenceDetailOpen() const noexcept { return refDetail.isVisible(); }
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
    wwma::ui::BeforeAfterBox beforeAfter { state };
    wwma::ui::LessonBox lessonBox { state };
    wwma::ui::RefDetailBox refDetail { state };
    juce::TextButton learnButton, reportButton;
    float lessonSeconds = -1.0f; // music time when the lesson goal was last checked
    void updateLesson();
    void chooseReportFile();
    std::unique_ptr<juce::FileChooser> chooser;
    int refGeneration = -1;

    juce::ComboBox profileBox, targetBox;
    juce::TextButton mixButton, masterButton, nowButton, songButton, resetButton, langButton, mixAButton, refBButton;
    juce::ToggleButton autoResetToggle;

    // style guess: only ever a suggestion, the preset changes when the user clicks it
    juce::TextButton styleButton;
    wwma::StyleGuess styleGuess;
    bool styleSuggests = false; // false: the chip only confirms the chosen preset
    void updateStyleButton();
    juce::TooltipWindow tooltips { this, 600 };

    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WwmaAudioProcessorEditor)
};
