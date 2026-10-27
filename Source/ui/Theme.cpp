#include "Theme.h"
#include "BinaryData.h"

namespace wwma::ui
{
namespace
{
    juce::Typeface::Ptr load (const char* filename)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const char* name = BinaryData::namedResourceList[i];
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (name)) == filename)
            {
                int size = 0;
                if (const char* data = BinaryData::getNamedResource (name, size))
                    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            }
        }
        jassertfalse;
        return {};
    }

    struct Faces
    {
        juce::Typeface::Ptr body = load ("Barlow-Regular.ttf");
        juce::Typeface::Ptr bodyBold = load ("Barlow-SemiBold.ttf");
        juce::Typeface::Ptr label = load ("BarlowCondensed-SemiBold.ttf");
        juce::Typeface::Ptr labelBold = load ("BarlowCondensed-Bold.ttf");
        juce::Typeface::Ptr num = load ("IBMPlexMono-Medium.ttf");
        juce::Typeface::Ptr numBold = load ("IBMPlexMono-SemiBold.ttf");
    };

    const Faces& faces()
    {
        static const Faces f;
        return f;
    }

    juce::Font make (const juce::Typeface::Ptr& t, float size)
    {
        if (t == nullptr)
            return juce::Font (juce::FontOptions (size));
        return juce::Font (juce::FontOptions (t).withHeight (size));
    }
}

namespace fonts
{
    juce::Font body (float size)      { return make (faces().body, size); }
    juce::Font bodyBold (float size)  { return make (faces().bodyBold, size); }
    juce::Font label (float size)     { return make (faces().label, size); }
    juce::Font labelBold (float size) { return make (faces().labelBold, size); }
    juce::Font num (float size)       { return make (faces().num, size); }
    juce::Font numBold (float size)   { return make (faces().numBold, size); }
    juce::Typeface::Ptr bodyTypeface() { return faces().body; }
}

juce::String upper (const juce::String& s)
{
    return s.toUpperCase()
            .replace (juce::CharPointer_UTF8 ("\xc3\xa4"), juce::CharPointer_UTF8 ("\xc3\x84"))
            .replace (juce::CharPointer_UTF8 ("\xc3\xb6"), juce::CharPointer_UTF8 ("\xc3\x96"))
            .replace (juce::CharPointer_UTF8 ("\xc3\xa5"), juce::CharPointer_UTF8 ("\xc3\x85"));
}

LookAndFeel::LookAndFeel()
{
    setColourScheme ({ colours::panel, colours::background, colours::raised, colours::line,
                       colours::text, colours::accent, colours::text, colours::accent, colours::text });
    setColour (juce::ComboBox::backgroundColourId, colours::raised);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::arrowColourId, colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::headerTextColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::raised);
    setColour (juce::TextButton::buttonOnColourId, colours::accent);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0d1a18));
    setColour (juce::ToggleButton::textColourId, colours::dim);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, colours::faint);
    setColour (juce::ScrollBar::thumbColourId, colours::line);
    setColour (juce::TooltipWindow::backgroundColourId, colours::raised);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        if (auto t = fonts::bodyTypeface())
            return t;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::label (juce::jmin (14.0f, buttonHeight * 0.52f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return fonts::bodyBold (14.0f); }
juce::Font LookAndFeel::getPopupMenuFont() { return fonts::body (15.0f); }

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto onColour = b.getProperties().contains ("onColour")
                              ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onColour"]) : colours::accent;
    auto fill = on ? onColour : base;
    if (highlighted && ! on) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);

    juce::Path p;
    const float c = 6.0f;
    const bool left = b.isConnectedOnLeft(), right = b.isConnectedOnRight();
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), c, c, ! left, ! right, ! left, ! right);
    g.setColour (fill);
    g.fillPath (p);
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    g.setColour (primary && ! on ? colours::accent : colours::line);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    const float box = 16.0f;
    drawTickBox (g, b, 2.0f, ((float) b.getHeight() - box) * 0.5f, box, box, b.getToggleState(), b.isEnabled(), highlighted, down);
    g.setColour (b.findColour (juce::ToggleButton::textColourId));
    g.setFont (fonts::body (13.0f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) box + 8), juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (colours::raised);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // small caption (e.g. "PRESET") on the left
    const auto caption = box.getProperties().getWithDefault ("caption", juce::String()).toString();
    if (caption.isNotEmpty())
    {
        g.setColour (colours::faint);
        g.setFont (fonts::label (11.5f));
        g.drawText (upper (caption), juce::Rectangle<int> (10, 0, 70, height), juce::Justification::centredLeft);
    }

    juce::Path arrow;
    const float ax = (float) width - 16.0f, ay = height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (colours::dim);
    g.fillPath (arrow);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto caption = box.getProperties().getWithDefault ("caption", juce::String()).toString();
    const int left = caption.isNotEmpty() ? 10 + juce::GlyphArrangement::getStringWidthInt (fonts::label (11.5f), upper (caption)) + 6 : 8;
    label.setBounds (left, 1, box.getWidth() - left - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}
} // namespace wwma::ui
