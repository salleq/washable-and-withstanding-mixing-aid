#pragma once

#include "advice/Advisor.h"
#include "advice/ChainAdvice.h"
#include "dsp/MasterChain.h"
#include <optional>
#include <string>
#include <vector>

namespace wwma::ai
{
/** A change to WWMA's own mastering EQ and limiter, as an AI assistant (or WWMA's own
    suggestions) asked for it. Only the fields that are set change. */
struct ChainChange
{
    std::array<std::optional<float>, ChainSettings::numBands> freq, gain, q;
    std::optional<float> limiterGain, ceiling, release;

    bool touchesEq() const;
    bool touchesLimiter() const;
    bool empty() const { return ! touchesEq() && ! touchesLimiter(); }
};

/** One thing the assistant suggests, read from the JSON block at the end of its answer. */
struct Suggestion
{
    std::string findingId;      // a WWMA finding it is about, empty when none
    juce::String part, at;      // the part of the song ("B") and where ("1:05"), may be empty
    juce::String title, step, listenFor;
    ChainChange chain;          // empty unless the fix can be made on the master with WWMA's own EQ / limiter
};

/** What the question needs to know about the mix right now. */
struct PromptInput
{
    juce::String question;      // listeningQuestion(): the same one the listening file asks to paste
    Lang lang = Lang::english;
    Stage stage = Stage::mastering;
    bool masterChanges = false; // may suggest changes to WWMA's mastering EQ / limiter
    std::vector<std::pair<std::string, juce::String>> findings; // WWMA's findings now: id, title
    ChainSettings chain;        // the knobs as they are
};

/** The question plus what WWMA needs back: a short JSON block it can turn into "Try" buttons. */
juce::String buildPrompt (const PromptInput& in);

/** Reads the suggestions from an answer. Tolerates a missing or broken block (then none).
    `answerText` gets the answer without the block, for showing. Never more than 3. */
std::vector<Suggestion> parseSuggestions (const juce::String& answer, juce::String* answerText = nullptr);

/** How far a change may go in one step. */
struct ChainLimits
{
    float maxEqStepDb = 0.0f;     // > 0: each band moves at most this much from where it is
    float maxLimiterStepDb = 0.0f;
    float maxCeiling = -0.3f;     // dBTP
};
/** The assistant's changes are kept small: a master EQ is no place for big moves. */
inline ChainLimits aiLimits() { return { 3.0f, 6.0f, -0.3f }; }
/** WWMA's own suggested values already are what it thinks is right. */
inline ChainLimits ownLimits() { return { 0.0f, 0.0f, -0.3f }; }

/** The knobs after the change, kept within the knobs' ranges and the limits. A stage
    that gets changed is turned on. `notes` gets a line for every value that had to be limited. */
ChainSettings applyChange (const ChainSettings& current, const ChainChange& change, const ChainLimits& limits,
                           juce::StringArray* notes, Lang lang);

/** "Low shelf 100 Hz: 0.0 → +2.0 dB", one line per knob that moves. */
juce::StringArray describeChange (const ChainSettings& before, const ChainSettings& after, Lang lang);

/** WWMA's own suggested values for the EQ and/or the limiter, as a change. */
ChainChange changeFromAdvice (const ChainAdvice& advice, bool eq, bool limiter);

/** The knobs' ranges (the same as the plugin's parameters). */
juce::Range<float> freqRange (int band);
} // namespace wwma::ai
