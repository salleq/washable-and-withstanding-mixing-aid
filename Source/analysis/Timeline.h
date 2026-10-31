#pragma once

#include "Snapshot.h"
#include <array>
#include <vector>

namespace wwma
{
/** One step of the song's timeline: what the music did during about one second
    (longer steps for long sessions, see Timeline::step). Silence is left out. */
struct TimelinePoint
{
    float seconds = 0.0f;       // music heard since the reset, at the start of this step
    float songSeconds = -1.0f;  // where that is in the DAW project / file, -1 when unknown
    float loudness = -200.0f;   // LUFS (K-weighted mean power) over the step
    float peak = -200.0f;       // dBTP, highest in the step
    float correlation = 1.0f;   // left/right correlation over the step
    std::array<float, Bands::count> mid {}, side {}; // third-octave levels, dB
};

/** The timeline of a song: one point per step, oldest first. */
struct Timeline
{
    std::vector<TimelinePoint> points;
    float step = 1.0f; // seconds of music per point
    bool empty() const { return points.empty(); }
    /** Seconds of music it covers. */
    float length() const { return step * (float) points.size(); }
};
} // namespace wwma
