#pragma once

#include "Components.h"
#include "VersionsPanel.h"
#include "ai/AiSettings.h"
#include <memory>
#include <vector>

namespace wwma::ui
{
/** "Ask an AI": hands the report to Claude, Gemini or ChatGPT and shows the answer, with its
    suggestions as cards to try (and, for the mastering EQ / limiter, to try on the master with
    one click). The settings page picks the service and how WWMA reaches it.
    Shown over the spectrum and the findings. Nothing is sent before the user clicks Ask. */
class AiPanel : public juce::Component
{
public:
    explicit AiPanel (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();

    // ---- settings page
    void setSettings (const ai::Settings& s);
    const ai::Settings& getSettings() const noexcept { return settings; }
    void showSettings (bool show);
    bool isShowingSettings() const noexcept { return settingsShown; }
    void setTestStatus (const juce::String& text, bool ok);
    void setModels (const juce::StringArray& models);
    /** Where the app was found, or empty when it was not. */
    void setCliFound (const juce::File& exe);

    // ---- answer page
    struct Card
    {
        juce::String title, detail, tooltip;
        bool canTry = true;          // "Try": WWMA judges the next pass and asks the ears
        bool canTryMaster = false;   // "Try on master": the change goes to WWMA's own EQ / limiter
        juce::String masterTooltip;
    };
    struct View
    {
        enum class Status { idle, working, done, error, browser };
        Status status = Status::idle;
        juce::String headline, message;
        juce::String answer;
        std::vector<Card> cards;
        bool canFollowUp = false;
        bool hasFile = false;
        bool canPaste = false;       // the browser mode: paste the chat's answer back
    };
    void setView (View v);
    /** The user wrote a follow-up question (cleared once sent). */
    void clearFollowUp() { followUp.clear(); }

    std::function<void()> onAsk, onCancel, onCopyQuestion, onPasteAnswer, onOpenFile, onClose,
                          onSettingsChanged, onTest, onBrowseCli, onKeepChange, onUndoChange;
    std::function<void (const juce::String&)> onFollowUp;
    std::function<void (int)> onTry, onTryMaster;

private:
    const ViewState& state;
    ai::Settings settings;
    bool settingsShown = false;
    View view;
    juce::String testStatus;
    bool testOk = false;
    juce::File cliFound;
    bool cliChecked = false;

    // header
    juce::TextButton settingsButton, closeButton;
    // answer page
    juce::TextButton askButton, cancelButton, copyButton, pasteButton, openButton, sendButton, keepButton, undoButton;
    juce::TextEditor answerText, followUp;
    std::vector<std::unique_ptr<ListRow>> cards;
    // settings page
    std::array<juce::TextButton, ai::numProviders> providerButtons;
    std::array<juce::TextButton, 3> modeButtons;
    juce::TextEditor keyEditor, modelEditor, cliEditor;
    juce::ToggleButton showKey, consent;
    juce::TextButton modelsButton, testButton, browseCliButton;
    juce::PopupMenu modelMenu;
    juce::StringArray models;

    void layoutAnswer (juce::Rectangle<int> r);
    void layoutSettings (juce::Rectangle<int> r);
    void syncSettingsControls();
    void settingsEdited();
    void rebuildCards();
    juce::Rectangle<int> body() const;
    void paintAnswer (juce::Graphics&, juce::Rectangle<int> r);
    void paintSettings (juce::Graphics&, juce::Rectangle<int> r);
    ai::Mode mode() const { return settings.current().mode; }

    juce::Rectangle<int> statusArea, modeTextArea, keyLabelArea, modelLabelArea, cliLabelArea, cliStatusArea, testStatusArea, privacyArea, pendingArea;
};

/** The answer as plain text for the panel: Markdown marks taken out, headings kept as lines. */
juce::String plainAnswer (const juce::String& markdown);
} // namespace wwma::ui
