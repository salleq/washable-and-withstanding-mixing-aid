#pragma once

#include "analysis/Snapshot.h"
#include <array>
#include <string>
#include <vector>

namespace wwma
{
/** What a finished master in one style usually looks like. The tonal target is
    a smooth curve (tilt, low end lift, sub and top roll-off), the other numbers
    are typical ranges. These are educated guesses from common practice, not
    measurements of particular records: use "Learn reference" to match a
    record you like. */
struct Profile
{
    std::string name;
    bool electronic = true;

    float lufsMin = -10.0f, lufsMax = -7.0f; // integrated loudness of the finished master
    float truePeakMax = -1.0f;               // dBTP ceiling
    float plrMin = 7.0f, plrMax = 11.0f;     // peak to loudness ratio, LU
    float lraMax = 8.0f;                     // loudness range, LU

    // tonal target
    float tilt = -3.0f;          // dB per octave around 1 kHz
    float lowShelf = 0.0f;       // extra dB below ~120 Hz
    float subCorner = 32.0f;     // 12 dB/oct roll-off below this
    float highCorner = 12000.0f; // 6 dB/oct roll-off above this
    float tolerance = 2.5f;      // dB, per broad range

    // stereo
    float lowWidthMax = -18.0f;  // side vs mid below 140 Hz, dB
    float widthMax = -5.0f;      // side vs mid overall, dB
    float widthMin = -20.0f;

    // drums vs bass (5 ms envelope contrast, dB)
    float lowContrastMin = 8.0f;   // kick should stand out from the bass by at least this much
    float highContrastMin = 7.0f;  // drum hits should poke out of the 1.5-6 kHz region
    float highContrastMax = 22.0f;

    // typical tempo, for guessing the style from the music (0 = not a style, never suggested)
    float bpmMin = 0.0f, bpmMax = 0.0f;
    std::string family;          // styles that sound alike ("House", "Techno"...), for the style guess

    /** Target third-octave levels, dB, relative to the target's own total. */
    std::array<float, Bands::count> target {};

    void computeTarget();
};

/** Built-in profiles, electronic styles first. */
const std::vector<Profile>& builtInProfiles();

/** Index of the default profile (House). */
int defaultProfileIndex();

/** Turns a measured song into a profile ("Learn reference"). */
Profile profileFromSnapshot (const Snapshot& s, const std::string& name);

/** Third-octave levels relative to their total power, dB. */
std::array<float, Bands::count> relativeToTotal (const std::array<float, Bands::count>& db);

/** Total power of a range of bands (dB in, dB out). */
float sumBands (const std::array<float, Bands::count>& db, int first, int last);
} // namespace wwma
