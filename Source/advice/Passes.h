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
    std::string findingId;     // empty: a suggestion not tied to one of WWMA's findings (judged on the whole pass)
    juce::String title, step;  // the finding, and the first step of its fix
    juce::String undoText;     // set when WWMA itself made the change: how to undo it (instead of the DAW's undo)
    PassRecord before;         // the pass the change is judged against
    int passAtStart = 0;       // the result comes from a later pass
    /** What the ears said after A/B: 0 not yet, +1 the mix now is better, -1 BEFORE was better, 2 no difference. */
    int ear = 0;
    bool decided = false;      // the ears answered: the result no longer changes
};

struct TrialResult
{
    enum class Kind { waiting, helped, noChange, worse } kind = Kind::waiting;
    float excessBefore = 0.0f, excessAfter = 0.0f; // the finding itself (0 = gone)
    PassVerdict overall;
    juce::String headline, detail;
    bool askEar = false;       // the meter has its say; now the ears: BEFORE or MIX?
    bool overrule = false;     // the ears went against the meter: the finding stops counting
};

/** "Too much, too fast": many changes since the snapshot, judged only by the meter. Then the
    ears should check before going on. Never shown once BEFORE was listened to since the snapshot. */
struct DriftCheck
{
    bool warn = false;
    juce::String headline, detail;
    int rangesMoved = 0;          // tonal ranges that moved 2 dB or more
    float scoreGain = 0.0f;       // score since the snapshot
    float refDistanceChange = 0.0f; // dB: > 0 the mix moved away from the reference
};
DriftCheck overOptimised (const SavedSnapshot& snap, const Assessment& now, const Assessment* reference, Lang lang);

/** Once the meter has judged a trial, hand it to the ears: compare BEFORE and MIX.
    beforeReady: the BEFORE take has this part (otherwise the DAW's undo is the A/B). */
TrialResult askEars (TrialResult meter, const Trial& t, bool beforeReady, Lang lang);

/** The final word: the meter's result and what the ears said. The ears win.
    overrulesSoFar: how often the ears overruled the meter before, to suggest a reference. */
TrialResult judgeWithEars (const TrialResult& meter, const Trial& t, int overrulesSoFar, Lang lang);

/** The first numbered step of a finding's "how to", without the number. */
juce::String firstStep (const juce::String& howTo);

/** Judges a trial once a new pass of the same part has played long enough. */
TrialResult judgeTrial (const Trial& t, const PassRecord& now, bool newPass, bool samePart, Lang lang, bool showPoints = true);
} // namespace wwma
