#pragma once

#include "Components.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace wwma::ui
{
/** The mastering EQ and limiter: knobs the user turns by hand, each with WWMA's
    suggested value next to it (and a marker on the knob). The suggestions can be hidden.
    Nothing here ever moves a knob by itself. Shown over the findings. */
class ChainPanel : public juce::Component
{
public:
    ChainPanel (const ViewState& s, juce::AudioProcessorValueTreeState& apvts);
    ~ChainPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();
    /** Moves the suggestion markers and tooltips to the newest advice. */
    void refresh();

    std::function<void (bool)> onShowSuggestions, onListenWithout;
    std::function<void()> onClose;

private:
    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        const Suggestion* suggestion = nullptr;
        enum class Kind { gain, freq, q, ceiling, ms } kind = Kind::gain;
        juce::String paramName;
        bool limiter = false;
    };

    juce::String valueText (const Knob&, float v) const;
    juce::Rectangle<int> eqArea() const;
    juce::Rectangle<int> limiterArea() const;
    void paintCurve (juce::Graphics&, juce::Rectangle<float>) const;

    const ViewState& state;
    std::array<Knob, 4> freq, gain;
    std::array<Knob, 2> q;           // the two bells
    Knob limGain, ceiling, release;
    std::vector<Knob*> all;
    juce::TextButton eqOn, limOn, withoutButton, closeButton;
    juce::ToggleButton showSuggestions;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAttachment, limOnAttachment;
};
} // namespace wwma::ui
