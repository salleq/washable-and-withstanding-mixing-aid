#pragma once

#include "Theme.h"
#include "advice/Advisor.h"
#include <functional>
#include <array>
#include <map>
#include <vector>

namespace wwma::ui
{
struct ScorePoint { juce::Time time; float score; };

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

    float displayScore = 0.0f;           // smoothed for the meter
    std::vector<float> scoreHistory;     // one score every 0.5 s, last 60 s
    float trend = 0.0f;                  // score change over the last ~15 s
    std::vector<float> shortTermHistory; // one value every 0.5 s, last 3 minutes

    std::vector<ScorePoint> snapshots;   // saved scores (progress)

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
    void mouseUp (const juce::MouseEvent&) override;
    /** Which slot a point falls on (for drag and drop), -1 for none / the drop zone. */
    int slotAt (juce::Point<int>) const;

    std::function<void (int slot)> onBrowse;   // slot -1: the first free one
    std::function<void (int slot)> onSelect, onRemove;
    std::function<void (int slot, double fraction)> onCue;

private:
    juce::Rectangle<int> slotBounds (int i) const;
    juce::Rectangle<int> dropBounds() const;
    juce::Rectangle<int> waveBounds (int i) const;
    juce::Rectangle<int> closeBounds (int i) const;
    const ViewState& state;
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
private:
    const ViewState& state;
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

    std::function<void()> onSave, onClear;

private:
    const ViewState& state;
    juce::TextButton saveButton, clearButton;
};

/** Small helpers shared with the editor. */
void drawBox (juce::Graphics&, juce::Rectangle<float>);
void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Colour = colours::dim,
                juce::Justification = juce::Justification::centredLeft, float size = 11.5f);
void drawEarIcon (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
} // namespace wwma::ui
