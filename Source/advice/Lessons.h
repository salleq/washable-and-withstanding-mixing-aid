#pragma once

#include "Advisor.h"

namespace wwma
{
/** Lesson mode: short hands-on tasks ("duck the bass under the kick"). Each lesson
    watches the last 3 seconds of the mix, so you can loop a part, tweak, and see
    when you've made it. The plugin never touches the sound: you do the work. */
struct LessonText
{
    juce::String title, why, steps, doneNote;
};

struct LessonCheck
{
    bool ready = false;       // the music needed for this lesson is playing
    juce::String waitingFor;  // what to play when not ready
    float progress = 0.0f;    // 0-1, how close to the goal
    bool passed = false;
    juce::String now, goal;   // "Kick above the bass: 4.1 dB", "goal 7.0 dB or more"
};

int lessonCount();
std::string lessonId (int index);
LessonText lessonText (int index, Lang lang);
/** a = an assessment of the NOW window. */
LessonCheck checkLesson (int index, const Assessment& a, const Profile& p, Stage stage, Lang lang);
} // namespace wwma
