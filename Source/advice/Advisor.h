#pragma once

#include <juce_core/juce_core.h>
#include "Profiles.h"
#include "analysis/Snapshot.h"
#include <array>
#include <string>
#include <vector>

namespace wwma
{
enum class Lang { english, finnish };

/** Which measurements the advice uses: the last ~3 seconds (for working on a
    loop) or everything since reset (for a whole song). */
enum class Window { now, song };

enum class Severity { good, info, watch, fix };

/** Mixing: the mix is not limited yet, so loudness and peak advice is left out.
    Mastering: everything counts. */
enum class Stage { mixing, mastering };

/** Where a problem shows up. */
enum Tag
{
    tagPhone = 1, tagCar = 2, tagClub = 4, tagStreaming = 8, tagHeadphones = 16
};

/** One piece of advice. Nothing is ever applied to the audio: the plugin only
    explains what it sees and how you could fix it yourself. */
struct Finding
{
    std::string id;          // stable key, for comparing against a baseline
    Severity severity = Severity::info;
    int tags = 0;
    float excess = 0.0f;     // how far outside the target, 1 = "clearly"
    float penalty = 0.0f;    // points this costs from the score
    int direction = 0;       // +1 too much, -1 too little, 0 something else
    juce::String title, value, why, howTo;
};

/** How well the mix should hold up on one kind of playback. */
struct Translation
{
    float score = 0.0f;      // 0-100
    juce::String note;
};

enum class Playback { phone, car, headphones, club, count };

/** Numbers derived from a snapshot for one window, ready for display. */
struct Measures
{
    bool valid = false;
    float loudness = -200.0f;     // integrated (song) or short-term (now)
    float truePeak = -200.0f;
    float plr = 0.0f;
    float lra = 0.0f;
    float correlation = 1.0f;
    float width = -100.0f;        // side vs mid, dB
    float lowWidth = -100.0f;     // side vs mid below 140 Hz, dB
    float lowContrast = 0.0f, highContrast = 0.0f;
    bool contrastValid = false, kickValid = false;
    float phoneBass = 0.0f;       // dB vs target
    float balance = 0.0f;         // left vs right, dB
    float dcOffset = -200.0f;     // dBFS
    float samplePeak = -200.0f;   // dBFS
    int clipEvents = 0;
    std::array<float, numRanges> rangeDeviation {};       // dB vs target
    std::array<float, Bands::count> spectrum {};          // relative, aligned with the target
    std::array<float, Bands::count> side {};              // side part, same alignment
};

struct Assessment
{
    Measures measures;
    std::vector<Finding> findings;  // most important first
    std::vector<Finding> passed;    // checks that are fine (Severity::good)
    std::array<Translation, (size_t) Playback::count> translation {};
    juce::String nextStep;          // what fixing the top findings would get you
    float score = 0.0f;             // 0-100, how close to a good mix for this profile
    bool enoughData = false;
};

Assessment assess (const Snapshot& s, const Profile& p, Window w, Lang lang, Stage stage = Stage::mastering);

/** "Needs improvement" (0-40), "Not quite there yet" (40-70), "Close enough" (70-90), "Looks perfect" (90-100). */
juce::String verdictFor (float score, Lang lang);

juce::String rangeName (Range r, Lang lang);
/** Plain-language name: sub, bass, mud, mids, harsh, air. */
juce::String rangeNickname (Range r, Lang lang);
juce::String tagName (Tag t, Lang lang);
juce::String playbackName (Playback p, Lang lang);

/** Picks the English or the Finnish text (the Finnish one is UTF-8). */
juce::String tr (Lang lang, const char* english, const char* finnish);
} // namespace wwma
