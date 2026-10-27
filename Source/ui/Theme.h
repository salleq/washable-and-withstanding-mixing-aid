#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "advice/Advisor.h"

namespace wwma::ui
{
namespace colours
{
    const juce::Colour background { 0xff14191c };
    const juce::Colour panel      { 0xff1b2226 };
    const juce::Colour raised     { 0xff232c31 };
    const juce::Colour line       { 0xff2f3a40 };
    const juce::Colour text       { 0xffdfe7e9 };
    const juce::Colour soft       { 0xffc4cfd2 };
    const juce::Colour dim        { 0xff8b9aa0 };
    const juce::Colour faint      { 0xff56656b };
    const juce::Colour accent     { 0xff7fd1c7 };
    const juce::Colour bad        { 0xffe2574c };
    const juce::Colour warn       { 0xffe8a33a };
    const juce::Colour mid        { 0xffd6c949 };
    const juce::Colour good       { 0xff5cc17a };
    const juce::Colour great      { 0xff3fd29a };
    const juce::Colour info       { 0xff6f8fb3 };
    const juce::Colour ref        { 0xffb79cf0 }; // everything about the reference track
    const juce::Colour suggest    { 0xfff2c94c }; // WWMA's suggested knob values
}

inline juce::Colour severityColour (Severity s)
{
    switch (s)
    {
        case Severity::fix:   return colours::bad;
        case Severity::watch: return colours::warn;
        case Severity::good:  return colours::good;
        case Severity::info:  break;
    }
    return colours::info;
}

/** The score gradient: red, orange, yellow, green, bright green. */
inline juce::Colour scoreColour (float score)
{
    struct Stop { float at; juce::Colour c; };
    const Stop stops[] = { { 0.0f, colours::bad }, { 32.0f, colours::warn }, { 58.0f, colours::mid },
                           { 80.0f, colours::good }, { 100.0f, colours::great } };
    score = juce::jlimit (0.0f, 100.0f, score);
    for (int i = 1; i < 5; ++i)
        if (score <= stops[i].at)
            return stops[i - 1].c.interpolatedWith (stops[i].c, (score - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
    return colours::great;
}

/** Barlow for text, Barlow Condensed for labels, IBM Plex Mono for numbers (all SIL OFL, compiled in). */
namespace fonts
{
    juce::Font body (float size);
    juce::Font bodyBold (float size);
    juce::Font label (float size);      // condensed semibold, use with upper case
    juce::Font labelBold (float size);
    juce::Font num (float size);
    juce::Font numBold (float size);
    juce::Typeface::Ptr bodyTypeface();
}

/** Upper case that also handles the Finnish letters, whatever the C locale is. */
juce::String upper (const juce::String& s);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};
} // namespace wwma::ui
