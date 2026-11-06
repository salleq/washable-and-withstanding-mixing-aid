#pragma once

#include "Components.h"

namespace wwma::ui
{
/** The top-left logo as a button: opens the About view. Paints only a hover glow, the logo
    itself is drawn by the top bar under it. */
class LogoButton : public juce::Button
{
public:
    LogoButton() : juce::Button ("about") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** About WWMA: the big badge, the version and where to find it. A click anywhere closes it. */
class AboutView : public juce::Component
{
public:
    explicit AboutView (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void updateTexts();

    std::function<void()> onClose;

private:
    const ViewState& state;
    juce::Image badge;
    juce::HyperlinkButton link;
    juce::Rectangle<float> badgeArea;
};

/** The badge (Washable and Withstanding Mixing Aid), compiled in. */
juce::Image badgeImage();
} // namespace wwma::ui
