#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/ChainPanel.h"
#include "ui/VersionsPanel.h"
#include "ui/AiPanel.h"
#include "ai/AiClient.h"
#include "ai/AiPrompt.h"
#include "advice/ListeningReport.h"
#include "ui/Theme.h"
#include "advice/StyleGuess.h"
#include <map>

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
    /** "<report> AI.md" and "<report> AI.png" next to the report: the song part by part, for an AI assistant. */
    void saveListeningData (const juce::File& markdownFile, const wwma::ReportInput& report, const wwma::Timeline* subjectTimeline = nullptr);
    /** A report of one reference slot alone (e.g. an older version of the song), saved like the mix report. */
    void saveReferenceReport (int slot);
    /** Writes the report of one reference slot alone (no sharing); the Markdown file, or none. */
    juce::File writeReferenceReport (int slot);
    /** The reference's own play/pause (plays it without the DAW's transport, loop included). */
    void toggleReferencePlay (int slot);
    /** The big reference view, where a loop is chosen. */
    void openReferenceDetail (int slot);
    void closeReferenceDetail();
    bool isReferenceDetailOpen() const noexcept { return refDetail.isVisible(); }
    int getReferenceDetailSlot() const noexcept { return refDetail.getSlot(); }
    /** A/B: true plays the reference instead of the mix. */
    void setListening (bool b);
    /** True plays the "before" recording (the mix as it was at the last snapshot) instead of the mix. */
    void setListeningBefore (bool b);
    /** The saved before versions and the other sessions (shown over the findings). */
    void setVersionsOpen (bool open);
    bool isVersionsOpen() const noexcept { return versionsOpen; }
    /** WWMA was just added: offers earlier sessions from WWMA's own folder (also used by the self test). */
    void offerSessions();
    bool isSessionOfferShown() const noexcept { return sessionOffer.isVisible(); }
    /** Rereads everything from the processor, after a session was restored from disk. */
    void reloadFromProcessor();
    void referencesChanged() { refGeneration = -1; readReferences(); reassess(); }
    /** The mastering EQ + limiter panel (shown over the findings). */
    void setChainOpen (bool open);
    bool isChainOpen() const noexcept { return state.chainOpen; }
    const wwma::ChainAdvice& getChainAdvice() const noexcept { return state.chainAdvice; }
    void setShowSuggestions (bool on);
    /** Level-matched bypass of the EQ and limiter. */
    void setListeningWithout (bool on);
    /** Track mode: WWMA on a kick, bass or kick + bass bus (TrackRole::mix = the normal mode). */
    void setTrackRole (wwma::TrackRole r);
    const wwma::Assessment& getAssessment() const noexcept { return state.assessment; }
    /** "Try the top fix": starts a trial on the most important finding, or ends the current one. */
    void toggleTrial();
    /** The ears' answer after a trial: +1 the mix now is better, -1 BEFORE was better, 2 no difference. */
    void answerEar (int ear);
    /** Forgets what the ears overruled, so those findings count again. */
    void clearEarDecisions();
    const wwma::TrialResult& getTrialResult() const noexcept { return state.trialResult; }
    const wwma::Trial& getTrial() const noexcept { return state.trial; }
    const wwma::PassVerdict& getPassVerdict() const noexcept { return state.passVerdict; }
    bool isPassComparable() const noexcept { return state.passComparable; }

    // ---- the AI assistant (Claude, Gemini, ChatGPT)
    void setAiOpen (bool open);
    bool isAiOpen() const noexcept { return aiPanel.isVisible(); }
    void showAiSettings (bool show) { setAiOpen (true); aiPanel.showSettings (show); }
    /** Saves a report and gives it to the chosen assistant (or opens it in the browser). */
    void askAi();
    /** The same about one reference track alone (e.g. an older version of the song), from its big view. */
    void askAiAboutReference (int slot) { askAiAboutReference (slot, false); }
    /** An answer from the assistant (or pasted back from the browser): shown, saved next to the
        report, and its suggestions become cards to try. */
    void showAiAnswer (const juce::String& answer);
    const std::vector<wwma::ai::Suggestion>& getAiSuggestions() const noexcept { return aiSession.suggestions; }
    /** "Try": the user makes the change; WWMA judges the next pass and asks the ears. */
    bool tryAiSuggestion (int index);
    /** "Try on master": sets WWMA's own EQ / limiter to the suggestion (kept small), then the same as Try. */
    bool tryAiSuggestionOnMaster (int index);
    /** "Use suggested" in the EQ + limiter panel: WWMA's own suggested values, with one click. */
    bool useSuggestedChain (bool eq, bool limiter);
    /** Keep or undo a change WWMA made to the EQ / limiter. */
    void keepChainChange();
    void undoChainChange();
    bool isChainChangePending() const noexcept { return proc.chainUndo.has_value(); }
    /** Master changes are offered when mastering (or when the user already uses the EQ), never in track mode. */
    bool masterChangesAllowed() const;

    static constexpr int baseWidth = 1140, baseHeight = 800;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (WwmaAudioProcessorEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
    private:
        WwmaAudioProcessorEditor& editor;
    };

    /** While the host renders an export it can keep the message thread busy, so the timer never
        runs and the window would freeze. This thread then reads the measurements itself and has
        the OpenGL context redraw the window, which does not need the message thread. */
    struct ExportPulse : public juce::Thread
    {
        explicit ExportPulse (WwmaAudioProcessorEditor& e) : juce::Thread ("WWMA export display"), editor (e) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (400);
                editor.exportTick();
            }
        }
        WwmaAudioProcessorEditor& editor;
    };
    void exportTick();
    juce::OpenGLContext glContext;
    std::atomic<juce::uint32> lastTimerMs { 0 };
    ExportPulse exportPulse { *this };

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
    void updatePanels();
    /** Reads the before take's state for the BEFORE button and its strip. */
    void readBefore();
    int beforeGenerationShown = -1;
    void refreshVersions();
    void confirm (const juce::String& title, const juce::String& message, const juce::String& ok, std::function<void()> then);
    void askName (const juce::String& title, const juce::String& current, std::function<void (const juce::String&)> then);
    bool versionsOpen = false, offerDone = false;
    int versionsGenerationShown = -1, diskJobsShown = 0;

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
    wwma::ui::ChainPanel chainPanel { state, proc.apvts };
    wwma::ui::VersionsPanel versionsPanel;
    wwma::ui::SessionOffer sessionOffer;
    juce::TextButton learnButton, reportButton, chainButton;
    int footerTextRight = 420; // where the footer's buttons start (the text on the left stops there)
    float lessonSeconds = -1.0f; // music time when the lesson goal was last checked
    void updateLesson();
    void chooseReportFile();
    /** Copies the report files to the clipboard (Windows) and shows them in the file manager.
        Returns true when the files are on the clipboard. */
    bool shareReportFiles (const juce::File& markdownFile);
    /** The listening data of the newest report (for the AI question). */
    wwma::ListeningInput lastListening;

    // ---- the AI assistant
    wwma::ui::AiPanel aiPanel { state };
    juce::TextButton aiButton;
    wwma::ai::Settings aiSettings;
    wwma::ai::Worker aiWorker;
    struct AiSession
    {
        juce::File reportFile, promptFile, answerFile;
        juce::String key;                   // "mix", or "ref:" + the reference file's path
        juce::String subject;               // a reference track's name when the report is about it alone
        wwma::ai::Provider provider = wwma::ai::Provider::claude;
        wwma::ai::Mode mode = wwma::ai::Mode::browser;
        juce::String prompt;
        wwma::ai::ApiRequest request;       // API mode: the conversation, for follow-up questions
        std::vector<wwma::ai::Suggestion> suggestions;
        juce::String suggestionsFrom;       // the answer the suggestions were read from (saved, read again on load)
        juce::String answerShown;
        wwma::ui::AiPanel::View::Status status = wwma::ui::AiPanel::View::Status::idle;
        juce::String headline, message;
    } aiSession;
    /** Earlier answers, by AiSession::key: going back to a track shows its answer again
        instead of asking (and paying) for a new one. A reference track's answer is also saved
        to disk, so the same file gets it back next time WWMA is opened. */
    std::map<juce::String, AiSession> aiStash;
    void stashAiSession();
    /** Makes the stashed (or saved) session for `key` the current one; false when there is none (or a question is running). */
    bool restoreAiSession (const juce::String& key);
    /** The file's path, size and time: a new bounce under the same name is a new track. */
    juce::String referenceAiKey (int slot) const;
    void saveAiSessionToDisk() const;
    bool loadAiSessionFromDisk (const juce::String& key);
    /** "Ask again" on a remembered reference answer: a new question about the same track. */
    void askAiAgain();
    void askAiAboutReference (int slot, bool fresh);
    /** The AI panel shows a reference's answer and another reference was chosen: shows that
        one's answer, or a page to ask about it. Never sends anything by itself. */
    void followAiReference();
    /** The loaded reference slot the current answer is about, or -1. */
    int aiSessionSlot() const;
    /** A part letter or a time clicked in the answer: plays that reference from there. */
    void jumpAiTrack (char part, float seconds);
    std::unique_ptr<juce::FileChooser> aiChooser;
    void setAiStatus (wwma::ui::AiPanel::View::Status status, const juce::String& headline, const juce::String& message);
    void refreshAiView();
    void aiReplied (const wwma::ai::Reply& reply);
    void sendAiRequest();
    /** Asks about the report just written (the mix's, or a reference's when `referenceSlot` >= 0). */
    void askAiAbout (const juce::File& reportFile, int referenceSlot);
    /** Checks consent and key before anything is written or sent; false: the settings are shown instead. */
    bool aiReady();
    void aiFollowUp (const juce::String& question);
    void testAiConnection();
    void saveAiAnswerFile();
    /** Starts "Try" on a change: judged on the next pass of the same part, then the ears. */
    bool startTrial (const std::string& findingId, const juce::String& title, const juce::String& step, const juce::String& undoText);
    bool applyChainChange (const wwma::ai::ChainChange& change, const wwma::ai::ChainLimits& limits, const juce::String& title,
                           const std::string& findingId = {});
    void setChainParameters (const wwma::ChainSettings& s);
    void syncChainChangeState();
    std::unique_ptr<juce::FileChooser> chooser;
    int refGeneration = -1;

    juce::ComboBox profileBox, targetBox;
    juce::ComboBox trackBox; // shown instead of the loudness target in track mode
    juce::TextButton trackButton;
    void fillTrackBox();
    juce::TextButton mixButton, masterButton, nowButton, songButton, resetButton, langButton, mixAButton, beforeButton, refBButton;
    juce::ToggleButton autoResetToggle;
    juce::TextButton mixLoadButton, mixPlayButton; // standalone app only: your own song

    /** Thin progress bar under the mix buttons: shows where the song is, click or drag to jump. */
    struct MixSeekBar : public juce::Component
    {
        explicit MixSeekBar (WwmaAudioProcessor& p) : proc (p) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override { jump (e); }
        void mouseDrag (const juce::MouseEvent& e) override { jump (e); }
        WwmaAudioProcessor& proc;
        double shown = 0.0;
    private:
        void jump (const juce::MouseEvent& e);
    } mixSeek { proc };
    int mixStatusShown = -1;
    void browseMix();
    void loadMix (const juce::File& file);

    // style guess: only ever a suggestion, the preset changes when the user clicks it
    juce::TextButton styleButton;
    wwma::StyleGuess styleGuess;
    bool styleSuggests = false; // false: the chip only confirms the chosen preset
    void updateStyleButton();
    void updatePasses();
    int passSeen = 0;
    float secondsAtPassChange = -1.0f; // the analyser hasn't restarted yet for the new pass while seconds stay at least this
    bool mixWasPlaying = false;        // standalone: your mix's own play button starts a pass
    juce::TooltipWindow tooltips { this, 600 };

    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WwmaAudioProcessorEditor)
};
