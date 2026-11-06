#include "AboutView.h"
#include "BinaryData.h"

#ifndef WWMA_VERSION
 #define WWMA_VERSION ""
#endif

namespace wwma::ui
{
juce::Image badgeImage()
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* name = BinaryData::namedResourceList[i];
        if (juce::String (BinaryData::getNamedResourceOriginalFilename (name)) == "wwma-badge.png")
        {
            int size = 0;
            if (const char* data = BinaryData::getNamedResource (name, size))
                return juce::ImageCache::getFromMemory (data, size);
        }
    }
    jassertfalse;
    return {};
}

void LogoButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (! highlighted && ! down)
        return;
    g.setColour (colours::text.withAlpha (down ? 0.10f : 0.05f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f);
}

AboutView::AboutView (const ViewState& s) : state (s), badge (badgeImage())
{
    setWantsKeyboardFocus (true);
    link.setURL (juce::URL ("https://github.com/salleq/washable-and-withstanding-mixing-aid"));
    link.setFont (fonts::body (13.0f), false);
    link.setColour (juce::HyperlinkButton::textColourId, colours::accent);
    addAndMakeVisible (link);
    updateTexts();
}

void AboutView::updateTexts()
{
    link.setButtonText ("github.com/salleq/washable-and-withstanding-mixing-aid");
    repaint();
}

void AboutView::resized()
{
    auto r = getLocalBounds().toFloat();
    const float h = juce::jmin (440.0f, r.getHeight() - 200.0f);
    const float w = badge.isValid() ? h * (float) badge.getWidth() / (float) badge.getHeight() : h;
    badgeArea = juce::Rectangle<float> (w, h).withCentre ({ r.getCentreX(), r.getY() + 40.0f + h * 0.5f });
    link.setBounds (juce::Rectangle<int> (420, 24).withCentre ({ getWidth() / 2, (int) badgeArea.getBottom() + 104 }));
}

void AboutView::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    g.fillAll (colours::background.withAlpha (0.96f));

    if (badge.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (badge, badgeArea, juce::RectanglePlacement::centred);
    }

    auto line = [&g, this] (float y, const juce::String& text, const juce::Font& f, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (f);
        g.drawText (text, juce::Rectangle<float> (0.0f, y, (float) getWidth(), 22.0f), juce::Justification::centred);
    };
    const float y = badgeArea.getBottom() + 18.0f;
    const juce::String version (WWMA_VERSION);
    line (y, upper (tr (lang, "Version ", "Versio ")) + version, fonts::labelBold (15.0f), colours::accent);
    line (y + 28.0f, tr (lang, "Tells you what is too much or too little in your mix. Washable and withstanding.",
                               "Kertoo, mitä miksissäsi on liikaa tai liian vähän. Kestävä ja pestävä."),
          fonts::body (14.0f), colours::soft);
    line (y + 52.0f, tr (lang, "Numbers are advice, not rules. The ear decides.", "Luvut ovat neuvoja, eivät sääntöjä. Korva ratkaisee."),
          fonts::body (12.5f), colours::dim);
    line ((float) getHeight() - 34.0f, tr (lang, "Click anywhere to close", "Sulje klikkaamalla mihin tahansa"), fonts::body (12.0f), colours::faint);
}

void AboutView::mouseUp (const juce::MouseEvent&)
{
    if (onClose)
        onClose();
}

bool AboutView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}
} // namespace wwma::ui
