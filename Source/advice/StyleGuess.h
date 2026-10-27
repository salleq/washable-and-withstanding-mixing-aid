#pragma once

#include "Profiles.h"
#include "analysis/Snapshot.h"

namespace wwma
{
/** A guess of which preset the music sounds closest to, from its tempo, tonal
    balance and dynamics. No machine learning: each preset gets a distance and the
    closest ones share the confidence. It is only a suggestion; the preset is never
    switched without the user accepting it. */
struct StyleGuess
{
    bool valid = false;         // enough music analysed and a clear enough answer
    bool tempoFound = false;
    float bpm = 0.0f;           // as the guessed style counts it (half or double time resolved)
    int best = -1;              // index into builtInProfiles()
    float confidence = 0.0f;    // 0-1, this preset alone
    float familyConfidence = 0.0f; // 0-1, this preset and its close relatives (House, Deep House...)
    int runnerUp = -1;
    float runnerUpConfidence = 0.0f;
};

/** How far one preset is from the measured music (0 = a perfect fit). */
float styleDistance (const Snapshot& s, const Profile& p);

StyleGuess guessStyle (const Snapshot& s);
} // namespace wwma
