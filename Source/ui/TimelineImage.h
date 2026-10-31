#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "advice/ListeningReport.h"

namespace wwma::ui
{
/** A picture of the song for people and AI assistants: a spectrogram (time left to right,
    low notes at the bottom), loudness and peaks, stereo width, and the parts of the song. */
juce::Image renderTimelineImage (const Timeline& t, const std::vector<SongSection>& sections,
                                 const juce::String& title, Lang lang);

/** Saves it as a PNG. Returns false when there is nothing to draw or the file can't be written. */
bool saveTimelineImage (const juce::File& file, const Timeline& t, const std::vector<SongSection>& sections,
                        const juce::String& title, Lang lang);
} // namespace wwma::ui
