#pragma once

#include "MixReport.h"

namespace wwma
{
/** One play-through ("pass") of a part of the song: WWMA starts measuring again every time
    the DAW starts playing, so each pass is measured on its own. Comparing a pass with the
    previous pass of the same part tells whether the change made in between helped. */
struct PassRecord
{
    double start = -1.0;     // song position where the pass started, seconds (-1: unknown)
    float seconds = 0.0f;    // music measured
    float score = 0.0f;
    std::map<std::string, std::pair<float, float>> metrics; // keyMetrics: id -> (value, off)
    std::map<std::string, float> excess;                    // finding id -> how far off (1 = clearly)
    std::map<std::string, juce::String> titles;             // finding id -> title, for the result text
    bool valid() const { return seconds > 0.0f; }
};

PassRecord makePassRecord (const Assessment& a, const Profile& p, Stage stage, float seconds, double start);

/** The latest pass that played the same part (started within 2 s of `start`), or nullptr.
    With an unknown position, the latest pass. */
const PassRecord* matchingPass (const std::vector<PassRecord>& passes, double start);

/** How the current pass compares with an earlier one. */
struct PassVerdict
{
    bool ready = false;       // enough of the current pass measured to compare fairly
    float waitSeconds = 0.0f; // until ready
    int verdict = 0;          // +1 better, -1 worse, 0 about the same
    float scoreDelta = 0.0f;
    int better = 0, worse = 0;
    juce::StringArray betterNames, worseNames; // metric names, most important first
};

PassVerdict comparePasses (const PassRecord& before, const PassRecord& now, Lang lang);

/** "Try one fix": the top finding, one concrete step, and whether it helped on the next pass. */
struct Trial
{
    bool active = false;
    std::string findingId;
    juce::String title, step;  // the finding, and the first step of its fix
    PassRecord before;         // the pass the change is judged against
    int passAtStart = 0;       // the result comes from a later pass
};

struct TrialResult
{
    enum class Kind { waiting, helped, noChange, worse } kind = Kind::waiting;
    float excessBefore = 0.0f, excessAfter = 0.0f; // the finding itself (0 = gone)
    PassVerdict overall;
    juce::String headline, detail;
};

/** The first numbered step of a finding's "how to", without the number. */
juce::String firstStep (const juce::String& howTo);

/** Judges a trial once a new pass of the same part has played long enough. */
TrialResult judgeTrial (const Trial& t, const PassRecord& now, bool newPass, bool samePart, Lang lang);
} // namespace wwma
