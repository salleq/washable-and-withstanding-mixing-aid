#pragma once

#include "Components.h"
#include "VersionsPanel.h"
#include "ai/AiSettings.h"
#include <memory>
#include <vector>

namespace wwma::ui
{
/** Song times ("1:05") up to `length` seconds, and parts ("part G", "osa G", "G-kohta", "(G)")
    among `parts`, in an answer: where they are in the text. */
struct TextLink
{
    juce::Range<int> range;
    char part = 0;
    float seconds = -1.0f;
};
std::vector<TextLink> findTextLinks (const juce::String& text, const juce::String& parts, float length);

/** "Ask an AI": hands the report to Claude, Gemini or ChatGPT and shows the answer, with its
    suggestions as cards to try (and, for the mastering EQ / limiter, to try on the master with
    one click). The service and how WWMA reaches it are chosen on WWMA's settings page.
    Shown over the spectrum and the findings. Nothing is sent before the user clicks Ask. */
class AiPanel : public juce::Component
{
public:
    explicit AiPanel (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();

    /** The chosen service (shown in the header and on the Ask button); edited on the settings page. */
    void setSettings (const ai::Settings& s);

    // ---- answer page
    struct Card
    {
        juce::String title, detail, tooltip;
        bool canTry = true;          // "Try": WWMA judges the next pass and asks the ears
        bool canTryMaster = false;   // "Try on master": the change goes to WWMA's own EQ / limiter
        juce::String masterTooltip;
        char jumpPart = 0;           // > 0 or jumpSeconds >= 0: a "▶ part / time" button plays the track there
        float jumpSeconds = -1.0f;
        juce::String jumpLabel;
    };
    struct View
    {
        enum class Status { idle, working, done, error, browser };
        Status status = Status::idle;
        juce::String headline, message;
        juce::String answer;
        std::vector<Card> cards;
        bool canFollowUp = false;
        bool hasFile = false;        // the answer is saved: Open file / Show in folder
        juce::String savedFile;      // its full path, shown under the answer
        bool canPaste = false;       // the browser mode: paste the chat's answer back
        bool canAskAgain = false;    // a reference track's (saved) answer: a new one replaces it
        // the track the answer is about can be played: its times ("1:05") and parts ("part G")
        // in the answer become links that play it from there
        float jumpLength = 0.0f;     // seconds; 0: no links
        juce::String jumpParts;      // the part letters it has, e.g. "ABCDEFG"
    };
    void setView (View v);
    /** The user wrote a follow-up question (cleared once sent). */
    void clearFollowUp() { followUp.clear(); }

    std::function<void()> onAsk, onCancel, onCopyQuestion, onPasteAnswer, onOpenFile, onShowFile, onClose,
                          onOpenSettings, onKeepChange, onUndoChange, onAskAgain;
    std::function<void (const juce::String&)> onFollowUp;
    std::function<void (int)> onTry, onTryMaster;
    /** A link in the answer, or a card's play button: a part letter (0 when none) or a time in seconds (-1 when none). */
    std::function<void (char part, float seconds)> onJump;

private:
    /** The answer, with links that can be clicked (selecting text still works). */
    class LinkText : public juce::TextEditor
    {
    public:
        std::vector<TextLink> links;
        std::function<void (const TextLink&)> onLink;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        const TextLink* linkAt (juce::Point<int> p) const;
    };

    const ViewState& state;
    ai::Settings settings;
    View view;

    // header
    juce::TextButton settingsButton, closeButton;
    // answer page
    juce::TextButton askButton, askAgainButton, cancelButton, copyButton, pasteButton, openButton, showFileButton, sendButton, keepButton, undoButton;
    LinkText answerText;
    juce::TextEditor followUp;
    juce::String answerShownFor;   // the answer and links the text was built for
    void setAnswerText();
    std::vector<std::unique_ptr<ListRow>> cards;

    void layoutAnswer (juce::Rectangle<int> r);
    void rebuildCards();
    juce::Rectangle<int> body() const;
    void paintAnswer (juce::Graphics&, juce::Rectangle<int> r);

    juce::Rectangle<int> statusArea, pendingArea, savedArea;
};

/** The answer as plain text for the panel: Markdown marks taken out, headings kept as lines. */
juce::String plainAnswer (const juce::String& markdown);
} // namespace wwma::ui
