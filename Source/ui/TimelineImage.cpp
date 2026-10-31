#include "TimelineImage.h"
#include "Theme.h"
#include <cmath>

namespace wwma::ui
{
namespace
{
    constexpr int width = 1600, height = 1000;
    constexpr float left = 78.0f, right = 1580.0f;

    /** Dark purple to red to yellow to white: easy to read for people and for image models. */
    juce::Colour heat (float x)
    {
        struct Stop { float at; juce::uint32 c; };
        static const Stop stops[] = { { 0.0f, 0xff07050f }, { 0.22f, 0xff3b0f6f }, { 0.45f, 0xff8c2981 },
                                      { 0.65f, 0xffde4968 }, { 0.82f, 0xfffe9f6d }, { 1.0f, 0xfffcfdbf } };
        x = juce::jlimit (0.0f, 1.0f, x);
        for (int i = 1; i < 6; ++i)
            if (x <= stops[i].at)
                return juce::Colour (stops[i - 1].c).interpolatedWith (juce::Colour (stops[i].c), (x - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
        return juce::Colour (stops[5].c);
    }

    float freqToY (double hz, float top, float bottom)
    {
        const double lo = Bands::lower (0), hi = Bands::upper (Bands::count - 1);
        const double f = std::log (juce::jlimit (lo, hi, hz) / lo) / std::log (hi / lo);
        return bottom - (float) f * (bottom - top);
    }

    juce::String hzText (double hz) { return hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String ((int) hz); }
} // namespace

juce::Image renderTimelineImage (const Timeline& t, const std::vector<SongSection>& sections, const juce::String& title, Lang lang)
{
    juce::Image img (juce::Image::RGB, width, height, true);
    juce::Graphics g (img);
    g.fillAll (colours::background);

    const int n = (int) t.points.size();
    g.setColour (colours::text);
    g.setFont (fonts::bodyBold (24.0f));
    g.drawText (title, juce::Rectangle<float> (left, 10.0f, right - left, 32.0f), juce::Justification::centredLeft);
    if (n == 0)
    {
        g.setFont (fonts::body (20.0f));
        g.drawText (tr (lang, "No timeline yet: play the song first.", "Aikajanaa ei vielä ole: soita kappale ensin."),
                    img.getBounds().toFloat(), juce::Justification::centred);
        return img;
    }

    const float plotW = right - left;
    auto xOf = [&] (float point) { return left + plotW * point / (float) n; };
    const float specTop = 78.0f, specBottom = 600.0f;
    const float loudTop = 630.0f, loudBottom = 800.0f;
    const float wideTop = 830.0f, wideBottom = 930.0f;

    // ---- spectrogram: mid + side per third-octave band, tilted +3 dB per octave so a balanced mix looks even
    std::vector<float> cells ((size_t) n * Bands::count);
    float top = -300.0f;
    for (int i = 0; i < n; ++i)
        for (int b = 0; b < Bands::count; ++b)
        {
            const auto& p = t.points[(size_t) i];
            const double power = std::pow (10.0, p.mid[(size_t) b] / 10.0) + std::pow (10.0, p.side[(size_t) b] / 10.0);
            const float v = (float) (10.0 * std::log10 (power + 1.0e-30) + 3.0 * std::log2 (Bands::centre (b) / 1000.0));
            cells[(size_t) (i * Bands::count + b)] = v;
            top = juce::jmax (top, v);
        }
    constexpr float range = 48.0f;
    for (int i = 0; i < n; ++i)
    {
        const float x0 = std::floor (xOf ((float) i)), x1 = std::ceil (xOf ((float) (i + 1)));
        for (int b = 0; b < Bands::count; ++b)
        {
            const float y0 = freqToY (Bands::upper (b), specTop, specBottom), y1 = freqToY (Bands::lower (b), specTop, specBottom);
            g.setColour (heat ((cells[(size_t) (i * Bands::count + b)] - (top - range)) / range));
            g.fillRect (juce::Rectangle<float> (x0, std::floor (y0), x1 - x0, std::ceil (y1) - std::floor (y0)));
        }
    }
    g.setFont (fonts::num (15.0f));
    for (double hz : { 30.0, 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
    {
        const float y = freqToY (hz, specTop, specBottom);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) y, left, right);
        g.setColour (colours::dim);
        g.drawText (hzText (hz), juce::Rectangle<float> (4.0f, y - 9.0f, left - 10.0f, 18.0f), juce::Justification::centredRight);
    }
    g.setColour (colours::dim);
    g.setFont (fonts::label (15.0f));
    g.drawText ("HZ", juce::Rectangle<float> (4.0f, specTop - 22.0f, left - 10.0f, 18.0f), juce::Justification::centredRight);

    // ---- loudness and peaks
    auto grid = [&] (float y0, float y1, float lo, float hi, float stepDb, const juce::String& unit)
    {
        g.setFont (fonts::num (15.0f));
        for (float v = lo; v <= hi + 0.01f; v += stepDb)
        {
            const float y = juce::jmap (v, lo, hi, y1, y0);
            g.setColour (colours::line);
            g.drawHorizontalLine ((int) y, left, right);
            g.setColour (colours::dim);
            g.drawText (juce::String ((int) v), juce::Rectangle<float> (4.0f, y - 9.0f, left - 10.0f, 18.0f), juce::Justification::centredRight);
        }
        g.setFont (fonts::label (15.0f));
        g.drawText (unit, juce::Rectangle<float> (left, y0 - 24.0f, right - left, 18.0f), juce::Justification::centredLeft);
    };
    grid (loudTop, loudBottom, -36.0f, 0.0f, 6.0f, tr (lang, "LOUDNESS LUFS (LINE) AND TRUE PEAK DBTP (DOTS)", "ÄÄNEKKYYS LUFS (VIIVA) JA TRUE PEAK DBTP (PISTEET)"));
    auto loudY = [&] (float v) { return juce::jmap (juce::jlimit (-36.0f, 0.0f, v), -36.0f, 0.0f, loudBottom, loudTop); };
    g.setColour (colours::bad.withAlpha (0.8f));
    for (int i = 0; i < n; ++i)
        g.fillEllipse (xOf (i + 0.5f) - 2.0f, loudY (t.points[(size_t) i].peak) - 2.0f, 4.0f, 4.0f);
    juce::Path loud;
    for (int i = 0; i < n; ++i)
    {
        const float x = xOf (i + 0.5f), y = loudY (t.points[(size_t) i].loudness);
        if (i == 0) loud.startNewSubPath (x, y); else loud.lineTo (x, y);
    }
    g.setColour (colours::accent);
    g.strokePath (loud, juce::PathStrokeType (2.5f));

    // ---- width
    grid (wideTop, wideBottom, -30.0f, 0.0f, 10.0f, tr (lang, "STEREO WIDTH ABOVE 140 HZ, SIDE VS MID DB (HIGHER = WIDER)", "STEREOLEVEYS 140 HZ:N YLÄPUOLELLA, SIDE VS MID DB (YLEMPÄNÄ = LEVEÄMPI)"));
    juce::Path wide;
    for (int i = 0; i < n; ++i)
    {
        const auto lv = levelsOf (t, i, i);
        const float x = xOf (i + 0.5f), y = juce::jmap (juce::jlimit (-30.0f, 0.0f, lv.width), -30.0f, 0.0f, wideBottom, wideTop);
        if (i == 0) wide.startNewSubPath (x, y); else wide.lineTo (x, y);
    }
    g.setColour (colours::ref);
    g.strokePath (wide, juce::PathStrokeType (2.0f));

    // ---- parts of the song
    for (const auto& s : sections)
    {
        const float x0 = xOf ((float) s.first), x1 = xOf ((float) (s.last + 1));
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        if (s.first > 0)
            for (auto [y0, y1] : { std::pair { specTop - 26.0f, specBottom }, std::pair { loudTop, loudBottom }, std::pair { wideTop, wideBottom } })
                g.drawLine (x0, y0, x0, y1, 1.5f);
        g.setColour (colours::text);
        g.setFont (fonts::bodyBold (18.0f));
        const auto label = juce::String::charToString ((juce::juce_wchar) s.letter) + " " + sectionKindName (s.kind, lang);
        g.drawFittedText (label, juce::Rectangle<float> (x0 + 4.0f, specTop - 26.0f, x1 - x0 - 6.0f, 22.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }

    // ---- time axis (music heard; the parts table has the project positions)
    g.setFont (fonts::num (15.0f));
    g.setColour (colours::dim);
    const float total = t.length();
    const float tick = total <= 120.0f ? 10.0f : (total <= 420.0f ? 30.0f : 60.0f);
    for (float s = 0.0f; s <= total; s += tick)
    {
        const float x = left + plotW * s / juce::jmax (1.0f, total);
        g.drawVerticalLine ((int) x, wideBottom, wideBottom + 6.0f);
        g.drawText (clockText (s), juce::Rectangle<float> (x - 40.0f, wideBottom + 8.0f, 80.0f, 18.0f), juce::Justification::centred);
    }
    g.setFont (fonts::label (15.0f));
    g.drawText (tr (lang, "TIME HEARD (MIN:S), SILENCE LEFT OUT", "KUULTU AIKA (MIN:S), HILJAISUUS POIS"),
                juce::Rectangle<float> (left, wideBottom + 34.0f, plotW, 18.0f), juce::Justification::centred);
    return img;
}

bool saveTimelineImage (const juce::File& file, const Timeline& t, const std::vector<SongSection>& sections, const juce::String& title, Lang lang)
{
    if (t.empty())
        return false;
    const auto img = renderTimelineImage (t, sections, title, lang);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (img, out);
}
} // namespace wwma::ui
