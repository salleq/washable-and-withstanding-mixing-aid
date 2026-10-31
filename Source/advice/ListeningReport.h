#pragma once

#include "MixReport.h"
#include "analysis/Timeline.h"
#include <array>
#include <vector>

namespace wwma
{
/** What a part of the song looks like it is, from its loudness and low end compared
    with the rest of the song. A guess: the user knows their arrangement better. */
enum class SectionKind { intro, build, drop, breakdown, groove, outro };

juce::String sectionKindName (SectionKind k, Lang lang);
const char* sectionKindId (SectionKind k);

/** The levels of one stretch of the timeline. */
struct SectionLevels
{
    float loudness = -200.0f;    // LUFS, mean power
    float peak = -200.0f;        // dBTP, highest
    float correlation = 1.0f;
    float width = -100.0f;       // side vs mid above 140 Hz, dB
    std::array<float, numRanges> shape {}; // each range vs the stretch's own total, dB
};

SectionLevels levelsOf (const Timeline& t, int first, int last);

/** One part of the song, found where loudness, low end, top end or width change. */
struct SongSection
{
    int first = 0, last = 0;     // timeline points, inclusive
    char letter = 'A';
    SectionKind kind = SectionKind::groove;
    SectionLevels levels;
    std::array<float, numRanges> tone {}; // shape vs the song's average shape, dB (+ = more of it here)
    float slope = 0.0f;          // loudness change, LU per second (builds rise)
    juce::String at;             // where it starts: the project / file position when known, else time heard
};

/** Splits the timeline into parts of at least ~8 seconds. Never empty when the timeline has points. */
std::vector<SongSection> findSections (const Timeline& t);

/** Seconds of music heard before this point (the time axis of the timeline). */
inline float heardTime (const Timeline& t, int i) { return t.step * (float) i; }
/** "1:05". */
juce::String clockText (float seconds);
/** The DAW / file position of a point, "1:05", or empty when unknown. */
juce::String songTimeText (const Timeline& t, int i);

/** Things WWMA noticed by comparing the sections with each other (and with the reference's). */
std::vector<juce::String> sectionNotes (const std::vector<SongSection>& sections, const std::vector<SongSection>& refSections,
                                        Stage stage, Lang lang);

/** Everything for the "ask an AI" file. */
struct ListeningInput
{
    ReportInput report;
    Timeline timeline;
    Timeline referenceTimeline;      // empty when no reference (or it has no timeline)
    juce::String imageName;          // the spectrogram picture saved next to it, empty if none
    juce::String referenceImageName;
};

/** A Markdown file made to be given to an AI assistant (or read by a person): a ready
    question to paste, the song's parts with their levels, what changes between them,
    the timeline in steps, and how to read the numbers. Plain text: nothing is sent anywhere. */
juce::String listeningMarkdown (const ListeningInput& in, Lang lang);
} // namespace wwma
