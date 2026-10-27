#pragma once

#include "Theme.h"
#include "advice/Advisor.h"
#include "advice/Lessons.h"
#include "advice/MixReport.h"
#include <functional>
#include <array>
#include <map>
#include <vector>

namespace wwma::ui
{
/** One reference track slot as the panels see it. */
struct RefView
{
    enum class Status { empty, loading, ready, failed };
    Status status = Status::empty;
    juce::String name, error;
    double seconds = 0.0;
    Snapshot snap;
    Assessment assessment;          // the track measured against the current profile
    std::vector<float> wave;
    double loopStart = 0.0, loopEnd = 0.0;   // 0..1, end <= start: no loop
    bool hasLoop() const { return loopEnd > loopStart; }
    bool ready() const { return status == Status::ready; }
};

/** Everything the panels draw, refreshed by the editor's timer. */
struct ViewState
{
    Snapshot snap;
    Assessment assessment;
    Profile profile;
    juce::String profileName;
    Lang lang = Lang::english;
    Window window = Window::song;
    Stage stage = Stage::mastering;

    bool styleChipShown = false;         // the style suggestion sits in the tonal panel's header
    float displayScore = 0.0f;           // smoothed for the meter
    std::vector<float> scoreHistory;     // one score every 0.5 s, last 60 s
    float trend = 0.0f;                  // score change over the last ~15 s
    std::vector<float> shortTermHistory; // one value every 0.5 s, last 3 minutes

    std::vector<SavedSnapshot> snapshots; // saved snapshots (progress, before/after)
    bool awaitingSaveConfirm = false;     // newest snapshot: DAW project not yet confirmed saved as a new version
    bool compareOpen = false;             // the before/after panel is shown
    std::vector<MetricChange> changes;    // newest snapshot vs now

    // lesson mode
    bool learnMode = false;
    int lessonIndex = 0;
    LessonCheck lesson;
    float lessonHeld = 0.0f;              // seconds of music the goal has been met
    juce::StringArray lessonsDone;

    // the last saved snapshot, for "better / worse" on the cards
    bool hasBaseline = false;
    float baselineScore = 0.0f;
    std::map<std::string, float> baselineExcess;
    juce::StringArray fixedSinceBaseline;

    // reference tracks
    std::array<RefView, 2> refs;
    int activeRef = -1;
    bool listening = false;          // A/B is on B: the reference is playing
    float refGainDb = 0.0f;          // level matching applied to the reference
    double playPosition = 0.0;
    bool refPlaying = false;         // WWMA's own play button runs the reference (not the DAW)
    bool dragOver = false;

    /** The active, analysed reference, or nullptr. */
    const RefView* activeRefView() const
    {
        return juce::isPositiveAndBelow (activeRef, (int) refs.size()) && refs[(size_t) activeRef].ready()
                   && refs[(size_t) activeRef].assessment.measures.valid ? &refs[(size_t) activeRef] : nullptr;
    }
    juce::String refLabel() const { return "Ref " + juce::String (activeRef + 1); }

    /** For a finding: positive = better than at the baseline. NaN when unknown. */
    float changeSinceBaseline (const Finding& f) const;
};

/** Integrated loudness, target, and the other loudness numbers. */
class LoudnessBox : public juce::Component
{
public:
    explicit LoudnessBox (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
private:
    const ViewState& state;
};

/** Momentary / short-term / integrated bars with the target zone, true peak per channel. */
class LevelMeters : public juce::Component
{
public:
    explicit LevelMeters (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
private:
    const ViewState& state;
};

/** Short-term loudness over the last 3 minutes. */
class HistoryBox : public juce::Component
{
public:
    explicit HistoryBox (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
private:
    const ViewState& state;
};

/** Two reference track slots and a drop zone. */
class ReferenceBox : public juce::Component
{
public:
    explicit ReferenceBox (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    /** Which slot a point falls on (for drag and drop), -1 for none / the drop zone. */
    int slotAt (juce::Point<int>) const;

    std::function<void (int slot)> onBrowse;   // slot -1: the first free one
    std::function<void (int slot)> onSelect, onRemove;
    std::function<void (int slot, double fraction)> onCue;
    std::function<void (int slot)> onPlayPause, onOpenDetail;

    juce::Rectangle<int> playBounds (int i) const;
    juce::Rectangle<int> waveBounds (int i) const;
    juce::Rectangle<int> infoBounds (int i) const;   // name and numbers: click for the big view

private:
    juce::Rectangle<int> slotBounds (int i) const;
    juce::Rectangle<int> dropBounds() const;
    juce::Rectangle<int> closeBounds (int i) const;
    int seekingSlot = -1;
    const ViewState& state;
};

/** A bigger view of one reference track: jump anywhere, choose a loop, play and pause.
    The loop stays when the view is closed, and the small strip's play button plays it. */
class RefDetailBox : public juce::Component
{
public:
    explicit RefDetailBox (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void setSlot (int s) { slot = s; repaint(); }
    int getSlot() const noexcept { return slot; }
    juce::Rectangle<int> waveArea() const;

    std::function<void()> onClose, onPlayPause, onClearLoop, onPlayLoop;
    std::function<void (double fraction)> onCue;
    std::function<void (double start, double end)> onLoop;

private:
    const RefView* view() const;
    double fractionAt (float x) const;
    const ViewState& state;
    int slot = 0;
    juce::TextButton closeButton, playButton, loopButton, clearButton;
    enum class Drag { none, pending, select, startEdge, endEdge } drag = Drag::none;
    double dragFrom = 0.0, selA = 0.0, selB = 0.0;
};

/** Mix against the active reference: loudness, peaks, dynamics, width and the biggest tonal differences. */
class CompareBox : public juce::Component
{
public:
    explicit CompareBox (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
private:
    const ViewState& state;
};

/** Third-octave spectrum against the style's corridor, with too much / too little shaded. */
class TonalView : public juce::Component
{
public:
    explicit TonalView (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    /** Fill the gap between the snapshot's ghost curve and the mix (click the legend to toggle). */
    void setShowDifference (bool b) { showDifference = b; repaint(); }
    bool getShowDifference() const noexcept { return showDifference; }
    std::function<void (bool)> onShowDifferenceChanged;
private:
    const ViewState& state;
    bool showDifference = true;
    juce::Rectangle<float> differenceToggle; // where the legend's toggle was drawn
};

/** The findings, most important first. Click a card for why it matters and every step. */
class FindingsPanel : public juce::Component
{
public:
    explicit FindingsPanel (const ViewState& s);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class List : public juce::Component
    {
    public:
        explicit List (FindingsPanel& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int layout (int width);
    private:
        struct Card { juce::Rectangle<int> bounds; const Finding* finding; bool compact; };
        FindingsPanel& owner;
        std::vector<Card> cards;
        std::vector<Finding> shown;
    };

    const ViewState& state;
    juce::Viewport viewport;
    List list { *this };
    std::string expanded;
    bool userPicked = false;
};

/** "How close to a good mix": score, verdict, red-to-green bar and the next step. */
class ScoreBox : public juce::Component
{
public:
    explicit ScoreBox (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
private:
    const ViewState& state;
};

/** Phone, car, headphones and club: how well the mix should hold up on each. */
class TranslationBox : public juce::Component
{
public:
    explicit TranslationBox (const ViewState& s) : state (s) {}
    void paint (juce::Graphics&) override;
private:
    const ViewState& state;
};

/** Saved scores over the session, so you can see the mix getting better. */
class ProgressBox : public juce::Component
{
public:
    explicit ProgressBox (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();

    std::function<void()> onSave, onClear, onCompare, onConfirm;
    /** Shows the confirm button while a snapshot waits for its DAW save, and enables Before/after. */
    void syncButtons();

private:
    const ViewState& state;
    juce::TextButton saveButton, compareButton, clearButton, confirmButton;
    juce::Rectangle<int> plotArea() const;
};

/** Newest snapshot vs now: which numbers got better or worse. Shown over the findings. */
class BeforeAfterBox : public juce::Component
{
public:
    explicit BeforeAfterBox (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();
    std::function<void()> onClose;
private:
    const ViewState& state;
    juce::TextButton closeButton;
};

/** Lesson mode: one hands-on task at a time, with a live meter towards its goal. */
class LessonBox : public juce::Component
{
public:
    explicit LessonBox (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();
    std::function<void()> onPrev, onNext;
private:
    const ViewState& state;
    juce::TextButton prevButton, nextButton;
};

/** Small helpers shared with the editor. */
void drawBox (juce::Graphics&, juce::Rectangle<float>);
void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Colour = colours::dim,
                juce::Justification = juce::Justification::centredLeft, float size = 11.5f);
void drawEarIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
} // namespace wwma::ui
