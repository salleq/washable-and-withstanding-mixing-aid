#pragma once

#include "Advisor.h"
#include "dsp/MasterChain.h"

namespace wwma
{
/** A suggested value for one knob. It is only shown: WWMA never turns a knob itself. */
struct Suggestion
{
    bool shown = false;      // false: nothing to suggest yet (or nothing to suggest for this knob)
    float value = 0.0f;
    bool close = false;      // the knob is already close enough to the suggestion
    juce::String text;       // short, for next to the knob ("+2.0 dB")
    juce::String why;        // the reason, for the tooltip
};

/** Suggested values for the mastering EQ and the limiter, worked out from the input
    (before the EQ and limiter), the style profile and the loudness target. */
struct ChainAdvice
{
    bool ready = false;
    std::array<Suggestion, ChainSettings::numBands> freq, gain, q;
    Suggestion limiterGain, ceiling, release;
    juce::String eqNote, limiterNote;    // one teaching line for each stage
    float expectedReduction = 0.0f;      // dB of peak limiting the suggested gain should need
    bool eqTooMuch = false;              // a band needs more than a mastering EQ should do
};

/** Works out the suggestions. `input` is the analysis of the signal before the EQ and limiter,
    `current` the knobs as they are now (only used to tell when a knob is close enough). */
ChainAdvice adviseChain (const Snapshot& input, const Profile& p, Window w, Lang lang, Stage stage, const ChainSettings& current);

/** How much the given EQ settings change the loudness of a signal with this spectrum, dB. */
float eqLoudnessChange (const Snapshot& input, Window w, const ChainSettings& s);

/** Band names for the panel ("Low shelf", "Low mids"...). */
juce::String eqBandName (int band, Lang lang);
} // namespace wwma
