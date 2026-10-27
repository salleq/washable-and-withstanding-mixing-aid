#include "Components.h"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <limits>

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }

    const juce::String dash = juce::String (juce::CharPointer_UTF8 ("–"));
    const juce::String minus = juce::String (juce::CharPointer_UTF8 ("−"));
    const juce::String ellipsis = juce::String (juce::CharPointer_UTF8 ("…"));
    const juce::String dot = juce::String (juce::CharPointer_UTF8 (" · "));

    /** Number with a real minus sign; Finnish gets a decimal comma. */
    juce::String fmt (Lang l, float v, int decimals = 1, bool sign = false)
    {
        if (v <= -100.0f || ! std::isfinite (v))
            return dash;
        auto s = decimals == 0 ? juce::String (juce::roundToInt (std::abs (v))) : juce::String (std::abs (v), decimals);
        if (v < 0.0f && s.containsAnyOf ("123456789"))
            s = minus + s;
        else if (sign)
            s = "+" + s;
        return l == Lang::finnish ? s.replaceCharacter ('.', ',') : s;
    }

    juce::TextLayout layoutText (const juce::AttributedString& a, float width)
    {
        juce::TextLayout layout;
        layout.createLayout (a, width);
        return layout;
    }

    void arrow (juce::Graphics& g, juce::Point<float> c, float size, bool up, juce::Colour col)
    {
        juce::Path p;
        const float h = size * 0.5f;
        if (up) p.addTriangle (c.x - h, c.y + h * 0.7f, c.x + h, c.y + h * 0.7f, c.x, c.y - h * 0.8f);
        else    p.addTriangle (c.x - h, c.y - h * 0.7f, c.x + h, c.y - h * 0.7f, c.x, c.y + h * 0.8f);
        g.setColour (col);
        g.fillPath (p);
    }

    juce::Colour outsideColour (float over, float scale)
    {
        if (over <= 0.0f) return colours::good;
        return over > scale ? colours::bad : colours::warn;
    }

    juce::Rectangle<int> content (const juce::Component& c) { return c.getLocalBounds().reduced (12, 10); }

    /** Number in Plex Mono with a small unit after it. */
    void numberWithUnit (juce::Graphics& g, const juce::String& value, const juce::String& unit, juce::Rectangle<int> r,
                         float size, juce::Colour col)
    {
        g.setColour (col);
        g.setFont (fonts::num (size));
        g.drawText (value, r, juce::Justification::centredLeft);
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), value);
        g.setColour (colours::dim);
        g.setFont (fonts::body (juce::jmax (11.0f, size * 0.55f)));
        g.drawText (unit, r.withTrimmedLeft (w + 4), juce::Justification::centredLeft);
    }

    float tolFor (const Profile& p, int range)
    {
        return p.tolerance * ((Range) range == Range::sub || (Range) range == Range::air ? 1.3f : 1.0f);
    }
}

void drawBox (juce::Graphics& g, juce::Rectangle<float> r)
{
    r = r.reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c, juce::Justification j, float size)
{
    g.setColour (c);
    g.setFont (fonts::label (size).withExtraKerningFactor (0.08f));
    g.drawText (upper (text), r, j, true);
}

void drawEarIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const float s = r.getWidth() / 24.0f;
    auto p = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * s, r.getY() + y * s); };
    juce::Path outer;
    outer.startNewSubPath (p (7, 9));
    outer.cubicTo (p (7, 2.5f), p (17, 2.5f), p (17, 9));
    outer.cubicTo (p (17, 12), p (14, 13), p (14, 16));
    outer.cubicTo (p (14, 20), p (8, 20), p (8, 16));
    juce::Path inner;
    inner.startNewSubPath (p (10, 10));
    inner.cubicTo (p (10, 7.3f), p (14, 7.3f), p (14, 10));
    g.setColour (c);
    g.strokePath (outer, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.strokePath (inner, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

float ViewState::changeSinceBaseline (const Finding& f) const
{
    if (! hasBaseline)
        return std::numeric_limits<float>::quiet_NaN();
    const auto it = baselineExcess.find (f.id);
    const float before = it != baselineExcess.end() ? it->second : 0.0f;
    return before - f.excess;
}

// =====================================================================================
void LoudnessBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    const auto& s = state.snap;
    const auto& p = state.profile;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);

    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Integrated", "Integroitu"), head);
    const int secs = (int) s.secondsAnalysed;
    drawLabel (g, juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + t (lang, " analysed", " analysoitu"),
               head, colours::faint, juce::Justification::centredRight);

    numberWithUnit (g, fmt (lang, s.integrated), "LUFS", r.removeFromTop (48), 40.0f, colours::text);

    // target line
    {
        auto row = r.removeFromTop (18);
        const float v = s.integrated;
        juce::String status;
        juce::Colour col = colours::dim;
        if (v > -100.0f)
        {
            if (v > p.lufsMax + 0.5f)      { status = t (lang, "too loud", "liian kova"); col = outsideColour (v - p.lufsMax - 0.5f, 2.5f); }
            else if (v < p.lufsMin - 0.5f) { status = t (lang, "quieter", "hiljaisempi"); col = colours::warn; }
            else                           { status = t (lang, "on target", "kohdallaan"); col = colours::good; }
        }
        const auto text = t (lang, "Target ", "Tavoite ") + fmt (lang, p.lufsMin) + " " + ellipsis + " " + fmt (lang, p.lufsMax) + (status.isNotEmpty() ? dot : juce::String());
        g.setFont (fonts::body (12.5f));
        g.setColour (colours::dim);
        g.drawText (text, row, juce::Justification::centredLeft);
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text);
        g.setColour (col);
        g.setFont (fonts::bodyBold (12.5f));
        g.drawText (status, row.withTrimmedLeft (w), juce::Justification::centredLeft);
    }
    r.removeFromTop (8);

    const float plr = s.integrated > -100.0f && s.truePeakMax > -100.0f ? s.truePeakMax - s.integrated : -200.0f;
    juce::Colour plrCol = colours::text, tpCol = colours::text;
    if (plr > -100.0f)
        plrCol = plr < p.plrMin ? outsideColour (p.plrMin - plr, 2.0f) : outsideColour (plr - p.plrMax, 3.0f);
    if (s.truePeakMax > -100.0f)
        tpCol = outsideColour (s.truePeakMax - p.truePeakMax, 1.0f);
    const float corr = state.window == Window::now ? s.correlationNow : s.correlationSong;

    struct Stat { juce::String label, value, unit; juce::Colour col; };
    const Stat stats[] = {
        { t (lang, "Short-term", "Lyhytaik."), fmt (lang, s.shortTerm), "", colours::text },
        { t (lang, "Momentary", "Hetkellinen"), fmt (lang, s.momentary), "", colours::text },
        { "True peak", fmt (lang, s.truePeakMax), "dBTP", tpCol },
        { "LRA", fmt (lang, s.loudnessRange), "LU", colours::text },
        { "PLR", fmt (lang, plr), "LU", plrCol },
        { t (lang, "Correlation", "Korrelaatio"), fmt (lang, corr, 2, true), "", corr < 0.0f ? colours::bad : (corr < 0.3f ? colours::warn : colours::text) },
    };
    const int colW = r.getWidth() / 2;
    for (int i = 0; i < 6; ++i)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (i % 2) * colW, r.getY() + (i / 2) * 38, colW - 6, 38);
        drawLabel (g, stats[i].label, cell.removeFromTop (14), colours::dim, juce::Justification::centredLeft, 10.5f);
        numberWithUnit (g, stats[i].value, stats[i].unit, cell.removeFromTop (22), 17.0f, stats[i].col);
    }
}

// =====================================================================================
void LevelMeters::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    const auto& s = state.snap;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    {
        auto head = r.removeFromTop (16);
        drawLabel (g, t (lang, "Level meters", "Tasomittarit"), head);
        if (state.activeRefView() != nullptr)
            drawLabel (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb8 ref")), head, colours::ref, juce::Justification::centredRight);
    }
    r.removeFromTop (6);

    auto labels = r.removeFromBottom (16);
    auto plot = r.toFloat();
    const float scaleW = 26.0f;
    auto bars = plot.withTrimmedLeft (scaleW + 6.0f);
    const float lo = -36.0f;
    auto yFor = [&] (float db) { return bars.getY() + bars.getHeight() * juce::jlimit (0.0f, 1.0f, db / lo); };

    g.setFont (fonts::num (9.5f));
    for (int db = 0; db >= -35; db -= 5)
    {
        const float y = yFor ((float) db);
        g.setColour (colours::line);
        g.drawHorizontalLine ((int) y, bars.getX(), bars.getRight());
        g.setColour (colours::faint);
        g.drawText (db == 0 ? juce::String ("0") : minus + juce::String (-db), juce::Rectangle<float> (plot.getX(), y - 6.0f, scaleW, 12.0f), juce::Justification::centredRight);
    }

    // target zone of the loudness bars
    const float w = bars.getWidth();
    const float xs[] = { 0.03f, 0.21f, 0.39f, 0.63f, 0.81f };
    const float bw = w * 0.16f;
    g.setColour (colours::good.withAlpha (0.18f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bars.getX(), yFor (state.profile.lufsMax), bars.getX() + w * xs[2] + bw + 4.0f, yFor (state.profile.lufsMin)));

    struct Bar { juce::String name; float value; juce::Colour col; bool peak; };
    const Bar list[] = {
        { "M", s.momentary, colours::accent, false },
        { "S", s.shortTerm, juce::Colour (0xff5fa8a0), false },
        { "I", s.integrated, juce::Colour (0xff3f7e78), false },
        { "TP L", s.peakNowL, colours::good, true },
        { "R", s.peakNowR, colours::good, true },
    };
    for (int i = 0; i < 5; ++i)
    {
        const auto& b = list[i];
        auto col = juce::Rectangle<float> (bars.getX() + w * xs[i], bars.getY(), bw, bars.getHeight());
        g.setColour (colours::raised.withAlpha (0.85f));
        g.fillRect (col);
        if (b.value > lo)
        {
            auto fill = col.withTop (yFor (b.value));
            if (b.peak)
            {
                juce::ColourGradient grad (colours::good, 0.0f, col.getBottom(), colours::bad, 0.0f, col.getY(), false);
                grad.addColour (0.78, colours::mid);
                g.setGradientFill (grad);
            }
            else
                g.setColour (b.col);
            g.fillRect (fill);
        }
        g.setColour (colours::dim);
        g.setFont (fonts::label (11.0f));
        g.drawText (b.name, juce::Rectangle<float> (col.getX() - 10.0f, (float) labels.getY() + 2.0f, col.getWidth() + 20.0f, 14.0f), juce::Justification::centred);
    }
    if (s.truePeakMax > lo)
    {
        const float y = yFor (s.truePeakMax);
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (bars.getX() + w * xs[3], y - 0.75f, w * (xs[4] - xs[3]) + bw, 1.5f));
    }
    if (const auto* ref = state.activeRefView(); ref != nullptr && ref->snap.integrated > lo)
    {
        // where the reference's integrated loudness sits, next to the I bar
        const float y = yFor (ref->snap.integrated);
        const float x0 = bars.getX() + w * xs[2] - 4.0f;
        g.setColour (colours::ref);
        g.fillRect (juce::Rectangle<float> (x0, y - 1.0f, bw + 8.0f, 2.0f));
        g.setFont (fonts::num (8.5f));
        g.drawText ("ref " + fmt (lang, ref->snap.integrated), juce::Rectangle<float> (x0 - 20.0f, y - 12.0f, bw + 28.0f, 10.0f), juce::Justification::centredRight);
    }
}

// =====================================================================================
void HistoryBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    drawLabel (g, t (lang, "Short-term", "Lyhytaikainen") + dot + "3 min", r.removeFromTop (16));
    r.removeFromTop (6);
    auto plot = r.toFloat();

    const auto& p = state.profile;
    const float top = p.lufsMax + 4.0f, bottom = p.lufsMin - 10.0f;
    auto yFor = [&] (float v) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (top - v) / (top - bottom)); };

    g.setColour (colours::good.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yFor (p.lufsMax), plot.getRight(), yFor (p.lufsMin)));
    g.setFont (fonts::num (8.5f));
    g.setColour (colours::faint);
    g.drawText (fmt (lang, p.lufsMax, 0), juce::Rectangle<float> (plot.getRight() - 30.0f, yFor (p.lufsMax) - 11.0f, 30.0f, 10.0f), juce::Justification::centredRight);
    g.drawText (fmt (lang, p.lufsMin, 0), juce::Rectangle<float> (plot.getRight() - 30.0f, yFor (p.lufsMin) + 1.0f, 30.0f, 10.0f), juce::Justification::centredRight);

    const auto& h = state.shortTermHistory;
    if (h.size() < 2)
        return;
    const size_t cap = 360;
    juce::Path path;
    bool started = false;
    for (size_t i = 0; i < h.size(); ++i)
    {
        if (h[i] < -60.0f) { started = false; continue; }
        const float x = plot.getRight() - plot.getWidth() * (float) (h.size() - 1 - i) / (float) (cap - 1);
        const float y = yFor (h[i]);
        if (! started) { path.startNewSubPath (x, y); started = true; }
        else path.lineTo (x, y);
    }
    g.setColour (colours::accent);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

// =====================================================================================
void TonalView::mouseUp (const juce::MouseEvent& e)
{
    if (! differenceToggle.contains (e.position))
        return;
    setShowDifference (! showDifference);
    if (onShowDifferenceChanged)
        onShowDifferenceChanged (showDifference);
}

void TonalView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (differenceToggle.contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TonalView::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    const auto& m = state.assessment.measures;
    const auto& target = state.profile.target;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);

    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Tonal balance vs. ", "Taajuustasapaino vs. ") + state.profileName, head);
    const auto* refView = state.activeRefView();
    const SavedSnapshot* ghost = ! state.snapshots.empty() && state.snapshots.back().spectrum.size() == (size_t) Bands::count
                                     ? &state.snapshots.back() : nullptr;
    const auto ghostColour = colours::accent.withAlpha (0.45f);
    const auto diffColour = colours::info;
    if (refView != nullptr)
        drawLabel (g, t (lang, "ref level-matched ", "ref tasattu ") + fmt (lang, state.refGainDb, 1, true) + " dB", head, colours::ref, juce::Justification::centredRight);
    else if (! state.styleChipShown)
        drawLabel (g, t (lang, "green band = the style's normal range", "vihreä kaista = tyylin normaali alue"), head, colours::faint, juce::Justification::centredRight);
    r.removeFromTop (6);

    auto plot = r.toFloat().withTrimmedLeft (28.0f).withTrimmedBottom (34.0f).withTrimmedRight (4.0f);
    const float top = 9.0f, bottom = -36.0f, tol = 3.0f;
    auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yFor = [&] (float db) { return plot.getY() + plot.getHeight() * (top - db) / (top - bottom); };

    float ref = -100.0f; // the target's loudest band sits at 0 dB
    for (auto v : target) ref = juce::jmax (ref, v);

    g.setFont (fonts::num (9.0f));
    for (int db = 0; db >= -30; db -= 6)
    {
        g.setColour (juce::Colour (0xff2a343a));
        g.drawHorizontalLine ((int) yFor ((float) db), plot.getX(), plot.getRight());
        g.setColour (colours::faint);
        g.drawText (db == 0 ? juce::String ("0") : minus + juce::String (-db), juce::Rectangle<float> (plot.getX() - 30.0f, yFor ((float) db) - 6.0f, 24.0f, 12.0f),
                    juce::Justification::centredRight);
    }
    for (auto f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xFor (f);
        g.setColour (juce::Colour (0xff2a343a));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (colours::faint);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centred);
    }

    for (int i = 0; i < numRanges; ++i)
    {
        const auto info = rangeBands ((Range) i);
        const float x0 = xFor (juce::jmax (20.0, Bands::lower (info.firstBand))), x1 = xFor (juce::jmin (20000.0, Bands::upper (info.lastBand)));
        juce::Colour col = colours::dim;
        if (m.valid)
        {
            const float d = std::abs (m.rangeDeviation[(size_t) i]), tl = tolFor (state.profile, i);
            if (d > tl)
                col = d > 2.0f * tl ? colours::bad : colours::warn;
        }
        drawLabel (g, rangeNickname ((Range) i, lang), juce::Rectangle<float> (x0, plot.getBottom() + 18.0f, x1 - x0, 14.0f).toNearestInt(),
                   col, juce::Justification::centred, 11.0f);
        if (i > 0)
        {
            g.setColour (colours::line);
            g.drawVerticalLine ((int) x0, plot.getBottom() + 17.0f, plot.getBottom() + 33.0f);
        }
    }

    auto pointsOf = [&] (const std::array<float, Bands::count>& v, float shift)
    {
        std::vector<juce::Point<float>> pts;
        for (int b = 0; b < Bands::count; ++b)
            pts.push_back ({ xFor (Bands::centre (b)), yFor (juce::jlimit (bottom - 4.0f, top + 4.0f, v[(size_t) b] - ref + shift)) });
        return pts;
    };
    auto strokeOf = [] (const std::vector<juce::Point<float>>& pts)
    {
        juce::Path p;
        p.startNewSubPath (pts.front());
        for (auto& pt : pts) p.lineTo (pt);
        return p;
    };

    const auto upperEdge = pointsOf (target, tol), lowerEdge = pointsOf (target, -tol);
    {
        juce::Path corridor = strokeOf (upperEdge);
        for (auto it = lowerEdge.rbegin(); it != lowerEdge.rend(); ++it) corridor.lineTo (*it);
        corridor.closeSubPath();
        g.setColour (colours::good.withAlpha (0.14f));
        g.fillPath (corridor);
        g.setColour (colours::good.withAlpha (0.35f));
        g.strokePath (corridor, juce::PathStrokeType (1.0f));
    }

    // legend
    {
        auto lg = juce::Rectangle<float> (plot.getRight() - 150.0f, plot.getY() + 4.0f, 150.0f, 80.0f);
        g.setColour (colours::text);
        g.fillRect (lg.getX(), lg.getY() + 5.0f, 18.0f, 2.0f);
        drawLabel (g, t (lang, "Your mix", "Sinun miksi"), lg.removeFromTop (12.0f).withTrimmedLeft (24.0f).toNearestInt(), colours::dim, juce::Justification::centredLeft, 10.5f);
        lg.removeFromTop (4.0f);
        if (ghost != nullptr)
        {
            g.setColour (ghostColour);
            g.fillRect (lg.getX(), lg.getY() + 4.5f, 18.0f, 3.0f);
            drawLabel (g, t (lang, "Snapshot ", "Snapshot ") + juce::String ((int) state.snapshots.size()) + " (" + ghost->time.formatted ("%H:%M") + ")",
                       lg.removeFromTop (12.0f).withTrimmedLeft (24.0f).toNearestInt(), colours::dim, juce::Justification::centredLeft, 10.5f);
            lg.removeFromTop (4.0f);
            // a small check box: fill the difference between then and now
            auto row = lg.removeFromTop (12.0f);
            differenceToggle = row.expanded (2.0f, 2.0f);
            auto box = juce::Rectangle<float> (row.getX() + 4.0f, row.getY() + 1.0f, 10.0f, 10.0f);
            g.setColour (diffColour.withAlpha (showDifference ? 0.6f : 0.0f));
            g.fillRect (box);
            g.setColour (colours::dim);
            g.drawRect (box, 1.0f);
            drawLabel (g, t (lang, "Difference", "Ero"), row.withTrimmedLeft (24.0f).toNearestInt(),
                       showDifference ? colours::soft : colours::faint, juce::Justification::centredLeft, 10.5f);
            lg.removeFromTop (4.0f);
        }
        else
            differenceToggle = {};
        if (refView != nullptr)
        {
            for (float x = 0.0f; x < 18.0f; x += 7.0f)
            {
                g.setColour (colours::ref);
                g.fillRect (lg.getX() + x, lg.getY() + 5.0f, 4.5f, 2.0f);
            }
            drawLabel (g, state.refLabel() + t (lang, " (level-matched)", " (tasattu)"), lg.removeFromTop (12.0f).withTrimmedLeft (24.0f).toNearestInt(),
                       colours::ref, juce::Justification::centredLeft, 10.5f);
            lg.removeFromTop (4.0f);
        }
        g.setColour (colours::good.withAlpha (0.3f));
        g.fillRect (lg.getX(), lg.getY() + 2.0f, 18.0f, 8.0f);
        drawLabel (g, t (lang, "Target", "Tavoite") + juce::String (juce::CharPointer_UTF8 (" ±" "3 dB")), lg.removeFromTop (12.0f).withTrimmedLeft (24.0f).toNearestInt(),
                   colours::dim, juce::Justification::centredLeft, 10.5f);
    }

    if (! m.valid)
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (15.0f));
        g.drawText (t (lang, "Play some music...", "Soita musiikkia..."), plot, juce::Justification::centred);
        return;
    }

    g.saveState();
    g.reduceClipRegion (plot.expanded (0.0f, 3.0f).toNearestInt());

    const auto mix = pointsOf (m.spectrum, 0.0f);
    const auto side = pointsOf (m.side, 0.0f);

    // shade where the mix leaves the corridor: red above, orange below
    for (int b = 0; b + 1 < Bands::count; ++b)
    {
        for (int above = 0; above < 2; ++above)
        {
            const auto& edge = above == 0 ? lowerEdge : upperEdge;
            auto outside = [&] (int i) { return above == 1 ? mix[(size_t) i].y < edge[(size_t) i].y : mix[(size_t) i].y > edge[(size_t) i].y; };
            if (! outside (b) && ! outside (b + 1))
                continue;
            juce::Path quad;
            quad.startNewSubPath (mix[(size_t) b]);
            quad.lineTo (mix[(size_t) b + 1]);
            quad.lineTo (edge[(size_t) b + 1]);
            quad.lineTo (edge[(size_t) b]);
            quad.closeSubPath();
            g.setColour ((above == 1 ? colours::bad : colours::warn).withAlpha (0.55f));
            g.fillPath (quad);
        }
    }
    {
        // corridor edges back on top of the shading
        juce::Path edges = strokeOf (upperEdge);
        edges.addPath (strokeOf (lowerEdge));
        g.setColour (colours::good.withAlpha (0.5f));
        g.strokePath (edges, juce::PathStrokeType (1.0f));
    }

    {
        if (ghost != nullptr)
        {
            // the newest snapshot as a faint "ghost": how far the tone has moved since then
            std::array<float, Bands::count> ghostSpec {};
            std::copy (ghost->spectrum.begin(), ghost->spectrum.end(), ghostSpec.begin());
            const auto ghostPts = pointsOf (ghostSpec, 0.0f);
            if (showDifference)
            {
                // the gap between then and now
                juce::Path gap;
                gap.startNewSubPath (mix.front());
                for (size_t b = 1; b < mix.size(); ++b)
                    gap.lineTo (mix[b]);
                for (size_t b = ghostPts.size(); b-- > 0;)
                    gap.lineTo (ghostPts[b]);
                gap.closeSubPath();
                gap.setUsingNonZeroWinding (false);
                g.setColour (diffColour.withAlpha (0.35f));
                g.fillPath (gap);
            }
            g.setColour (ghostColour);
            g.strokePath (strokeOf (ghostPts), juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, strokeOf (side), dashes, 2);
        g.setColour (colours::faint);
        g.fillPath (dashed);
        if (refView != nullptr)
        {
            juce::Path refDashed;
            const float refDashes[] = { 5.0f, 3.0f };
            juce::PathStrokeType (1.8f).createDashedStroke (refDashed, strokeOf (pointsOf (refView->assessment.measures.spectrum, 0.0f)), refDashes, 2);
            g.setColour (colours::ref);
            g.fillPath (refDashed);
        }
        g.setColour (colours::text);
        g.strokePath (strokeOf (mix), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // call-outs on ranges that are off
    std::vector<juce::Rectangle<float>> placed;
    for (int i = 0; i < numRanges; ++i)
    {
        const float d = m.rangeDeviation[(size_t) i], tl = tolFor (state.profile, i);
        if (std::abs (d) <= tl)
            continue;
        const auto info = rangeBands ((Range) i);
        int worst = info.firstBand;
        for (int b = info.firstBand; b <= info.lastBand; ++b)
            if (std::abs (m.spectrum[(size_t) b] - target[(size_t) b]) > std::abs (m.spectrum[(size_t) worst] - target[(size_t) worst]))
                worst = b;
        const auto pt = mix[(size_t) worst];
        const auto col = std::abs (d) > 2.0f * tl ? colours::bad : colours::warn;
        g.setColour (col);
        g.fillEllipse (pt.x - 4.0f, pt.y - 4.0f, 8.0f, 8.0f);
        const auto text = fmt (lang, d, 1, true) + " dB " + (d > 0.0f ? t (lang, "too much", "liikaa") : t (lang, "too little", "liian vähän"));
        float ty = d > 0.0f ? pt.y - 20.0f : pt.y + 8.0f;
        if (ty < plot.getY())
            ty = pt.y + 8.0f; // no room above: put it under the point
        auto box = juce::Rectangle<float> (pt.x - 70.0f, juce::jlimit (plot.getY(), plot.getBottom() - 14.0f, ty), 140.0f, 13.0f);
        const float textW = 1.12f * (float) juce::GlyphArrangement::getStringWidthInt (fonts::label (11.5f), upper (text)) + 8.0f;
        auto ink = box.withSizeKeepingCentre (textW, box.getHeight());
        for (int tries = 0; tries < 6 && std::any_of (placed.begin(), placed.end(), [&] (auto& p) { return p.intersects (ink); }); ++tries)
        {
            box.translate (0.0f, 15.0f);
            ink.translate (0.0f, 15.0f);
        }
        placed.push_back (ink);
        drawLabel (g, text, box.toNearestInt(), col, juce::Justification::centred, 11.5f);
    }
    g.restoreState();
}

// =====================================================================================
FindingsPanel::FindingsPanel (const ViewState& s) : state (s)
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (7);
    addAndMakeVisible (viewport);
}

void FindingsPanel::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    auto head = getLocalBounds().removeFromTop (18);
    drawLabel (g, t (lang, "Findings", "Havainnot") + dot + t (lang, "most important first", "tärkein ensin"), head);
    int toFix = 0;
    for (const auto& f : state.assessment.findings)
        if (f.severity == Severity::fix || f.severity == Severity::watch)
            ++toFix;
    if (state.assessment.enoughData)
        drawLabel (g, juce::String (toFix) + t (lang, " to fix", " korjattavaa") + dot + juce::String ((int) state.assessment.passed.size())
                       + t (lang, " fine", " kunnossa"), head, colours::faint, juce::Justification::centredRight);
}

void FindingsPanel::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (24));
    refresh();
}

void FindingsPanel::refresh()
{
    const auto& f = state.assessment.findings;
    const bool exists = std::any_of (f.begin(), f.end(), [this] (const Finding& x) { return x.id == expanded; });
    if (! exists || ! userPicked)
    {
        expanded = f.empty() ? std::string() : f.front().id;
        userPicked = userPicked && exists;
    }
    const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
    list.setSize (juce::jmax (10, w), juce::jmax (10, list.layout (w)));
    list.repaint();
    repaint();
}

namespace
{
    const float cardPad = 12.0f;

    juce::String chipText (const Finding& f, Lang lang)
    {
        if (f.severity == Severity::good) return t (lang, "Fine", "Kunnossa");
        if (f.severity == Severity::info) return t (lang, "Info", "Info");
        if (f.direction > 0) return t (lang, "Too much", "Liikaa");
        if (f.direction < 0) return t (lang, "Too little", "Liian vähän");
        return t (lang, "Check", "Tarkista");
    }

    juce::Font chipFont() { return fonts::labelBold (11.5f).withExtraKerningFactor (0.08f); }

    float chipWidth (const Finding& f, Lang lang)
    {
        return (float) juce::GlyphArrangement::getStringWidthInt (chipFont(), upper (chipText (f, lang))) + 14.0f;
    }

    /** "1. Do this.\n2. Then that." -> "Do this." */
    juce::String firstStep (const juce::String& howTo)
    {
        auto line = howTo.upToFirstOccurrenceOf ("\n", false, false).trim();
        if (line.length() > 3 && juce::CharacterFunctions::isDigit (line[0]) && line[1] == '.')
            line = line.substring (2).trim();
        return line;
    }

    /** Title and value; the first line starts after the chip (padded with spaces). */
    juce::AttributedString headText (const Finding& f, Lang lang, const juce::String& refNote = {})
    {
        const auto titleFont = fonts::bodyBold (14.5f);
        const float spaceW = juce::jmax (1.0f, juce::GlyphArrangement::getStringWidth (titleFont, "          ") / 10.0f);
        juce::String pad;
        for (int k = 0; k < (int) std::ceil ((chipWidth (f, lang) + 8.0f) / spaceW); ++k)
            pad << " ";
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.setLineSpacing (2.0f);
        a.append (pad + f.title + "   ", titleFont, colours::text);
        if (f.value.isNotEmpty())
            a.append (f.value, fonts::num (11.5f), colours::dim);
        if (refNote.isNotEmpty())
            a.append ((f.value.isNotEmpty() ? dot : juce::String()) + refNote, fonts::num (11.5f), colours::ref);
        return a;
    }

    /** How this finding compares with the active reference track, e.g. "+3.9 dB vs ref". */
    juce::String refNoteFor (const Finding& f, const ViewState& st)
    {
        const auto* ref = st.activeRefView();
        if (ref == nullptr || ! st.assessment.measures.valid)
            return {};
        const auto lang = st.lang;
        const auto& m = st.assessment.measures;
        const auto& rm = ref->assessment.measures;
        const auto& id = f.id;
        const juce::String vsRef = t (lang, " vs ref", " vs ref");
        if (id.rfind ("range", 0) == 0 && id.size() >= 6)
        {
            const int i = id[5] - '0';
            if (i >= 0 && i < numRanges)
                return fmt (lang, m.rangeDeviation[(size_t) i] - rm.rangeDeviation[(size_t) i], 1, true) + " dB" + vsRef;
        }
        if (id == "phonebass") return fmt (lang, m.phoneBass - rm.phoneBass, 1, true) + " dB" + vsRef;
        if (id == "loud" || id == "quiet") return "ref " + fmt (lang, ref->snap.integrated) + " LUFS";
        if (id == "peak") return "ref " + fmt (lang, rm.truePeak) + " dBTP";
        if (id == "squashed" || id == "dynamic") return "ref PLR " + fmt (lang, rm.plr);
        if (id == "lra") return "ref LRA " + fmt (lang, rm.lra);
        if (id == "kickbass" && rm.kickValid) return "ref " + fmt (lang, rm.lowContrast) + " dB";
        if ((id == "flatdrums" || id == "spikydrums") && rm.contrastValid) return "ref " + fmt (lang, rm.highContrast) + " dB";
        if (id == "lowwidth") return "ref " + fmt (lang, rm.lowWidth) + " dB";
        if (id == "wide" || id == "narrow") return "ref " + fmt (lang, rm.width) + " dB";
        return {};
    }

    juce::AttributedString bodyText (const Finding& f, Lang lang, bool open)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.setLineSpacing (2.0f);
        if (! open)
        {
            const auto step = firstStep (f.howTo);
            if (step.isNotEmpty())
            {
                a.append (t (lang, "Try: ", "Kokeile: "), fonts::bodyBold (13.0f), colours::accent);
                a.append (step, fonts::body (13.0f), colours::soft);
            }
            else if (f.why.isNotEmpty())
                a.append (f.why, fonts::body (13.0f), colours::soft);
            return a;
        }
        if (f.why.isNotEmpty())
            a.append (f.why + "\n", fonts::body (13.0f), colours::soft);
        if (f.howTo.isNotEmpty())
        {
            a.append (t (lang, "How to fix it\n", "Näin korjaat\n"), fonts::bodyBold (13.0f), colours::accent);
            // steps in plain text, the common trap highlighted
            auto lines = juce::StringArray::fromLines (f.howTo);
            for (int i = 0; i < lines.size(); ++i)
            {
                const auto& line = lines[i];
                const auto nl = i + 1 < lines.size() ? juce::String ("\n") : juce::String();
                const int colon = line.indexOfChar (':');
                if (line.startsWith ("Trap:") || line.startsWith ("Sudenkuoppa:"))
                {
                    a.append (line.substring (0, colon + 1), fonts::bodyBold (13.0f), colours::warn);
                    a.append (line.substring (colon + 1) + nl, fonts::body (13.0f), colours::soft);
                }
                else
                    a.append (line + nl, fonts::body (13.0f), colours::text);
            }
        }
        return a;
    }
}

int FindingsPanel::List::layout (int width)
{
    const auto& st = owner.state;
    const auto lang = st.lang;
    cards.clear();
    shown.clear();

    // at most four things to fix at a time, so the screen doesn't shout; then some that are fine
    int problems = 0;
    for (const auto& f : st.assessment.findings)
    {
        const bool problem = f.severity == Severity::fix || f.severity == Severity::watch;
        if (problem && ++problems > 4)
            continue;
        shown.push_back (f);
    }
    for (size_t i = 0; i < st.assessment.passed.size() && i < 3; ++i)
        shown.push_back (st.assessment.passed[i]);

    int y = st.hasBaseline && ! st.fixedSinceBaseline.isEmpty() ? 22 : 0;
    const float inner = (float) width - 6.0f - 6.0f - 2.0f * cardPad;
    for (auto& f : shown)
    {
        const bool compact = f.severity == Severity::good;
        const bool open = f.id == owner.expanded && ! compact;
        int h = (int) std::ceil (layoutText (headText (f, lang, refNoteFor (f, st)), inner).getHeight());
        if (! compact)
            h += 6 + (int) std::ceil (layoutText (bodyText (f, lang, open), inner).getHeight());
        h += compact ? 14 : 20;
        cards.push_back ({ { 0, y, width - 6, h }, &f, compact });
        y += h + 8;
    }
    return y;
}

void FindingsPanel::List::paint (juce::Graphics& g)
{
    const auto& st = owner.state;
    const auto lang = st.lang;

    if (st.hasBaseline && ! st.fixedSinceBaseline.isEmpty())
    {
        g.setColour (colours::good);
        g.setFont (fonts::bodyBold (13.0f));
        g.drawText (t (lang, "Fixed since the snapshot: ", "Korjattu snapshotin jälkeen: ") + st.fixedSinceBaseline.joinIntoString (", "),
                    0, 0, getWidth() - 6, 18, juce::Justification::centredLeft, true);
    }

    for (const auto& card : cards)
    {
        const auto& f = *card.finding;
        const bool open = f.id == owner.expanded && ! card.compact;
        auto r = card.bounds.toFloat();
        const auto col = severityColour (f.severity);

        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (open ? colours::line.brighter (0.3f) : colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        {
            juce::Path stripe;
            stripe.addRoundedRectangle (r.getX(), r.getY(), 6.0f, r.getHeight(), 7.0f, 7.0f, true, false, true, false);
            g.setColour (col);
            g.fillPath (stripe);
        }

        auto c = r.withTrimmedLeft (6.0f).reduced (cardPad, card.compact ? 7.0f : 10.0f);

        auto chipR = juce::Rectangle<float> (c.getX(), c.getY() + 1.0f, chipWidth (f, lang), 17.0f);
        g.setColour (col);
        g.fillRoundedRectangle (chipR, 4.0f);
        g.setColour (juce::Colour (0xff101416));
        g.setFont (chipFont());
        g.drawText (upper (chipText (f, lang)), chipR, juce::Justification::centred);

        auto headLayout = layoutText (headText (f, lang, refNoteFor (f, st)), c.getWidth());
        headLayout.draw (g, c.removeFromTop (std::ceil (headLayout.getHeight())));

        const float change = st.changeSinceBaseline (f);
        if (! card.compact && ! std::isnan (change) && std::abs (change) >= 0.15f)
        {
            const bool better = change > 0.0f;
            const bool isNew = st.baselineExcess.count (f.id) == 0;
            const auto cc = better ? colours::good : colours::bad;
            const auto txt = better ? t (lang, "better", "parempi") : (isNew ? t (lang, "new", "uusi") : t (lang, "worse", "huonompi"));
            g.setFont (fonts::bodyBold (12.0f));
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt);
            auto cr = juce::Rectangle<float> (r.getRight() - tw - 30.0f, r.getY() - 1.0f, tw + 22.0f, 16.0f);
            g.setColour (colours::background);
            g.fillRoundedRectangle (cr, 4.0f);
            arrow (g, { cr.getX() + 7.0f, cr.getCentreY() }, 8.0f, better, cc);
            g.setColour (cc);
            g.drawText (txt, cr.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
        }

        if (! card.compact)
        {
            c.removeFromTop (6.0f);
            auto body = layoutText (bodyText (f, lang, open), c.getWidth());
            body.draw (g, c.removeFromTop (std::ceil (body.getHeight())));
        }
    }
}

void FindingsPanel::List::mouseUp (const juce::MouseEvent& e)
{
    for (const auto& c : cards)
    {
        if (c.bounds.contains (e.getPosition()) && ! c.compact)
        {
            const auto id = c.finding->id;
            owner.expanded = owner.expanded == id ? std::string() : id;
            owner.userPicked = true;
            owner.refresh();
            return;
        }
    }
}

// =====================================================================================
void ScoreBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    drawLabel (g, t (lang, "How close to a good mix", "Kuinka lähellä hyvää miksiä"), r.removeFromTop (16));

    const bool valid = state.assessment.enoughData;
    const float score = state.displayScore;
    const auto col = valid ? scoreColour (score) : colours::faint;

    auto row = r.removeFromTop (52);
    g.setColour (col);
    g.setFont (fonts::numBold (44.0f));
    g.drawText (valid ? juce::String (juce::roundToInt (score)) : dash + dash, row.removeFromLeft (90), juce::Justification::centredLeft);
    g.setFont (fonts::labelBold (20.0f));
    g.drawFittedText (valid ? verdictFor (score, lang) : t (lang, "Listening...", "Kuuntelen..."), row, juce::Justification::centredRight, 2);

    auto bar = r.removeFromTop (30).toFloat().withSizeKeepingCentre ((float) r.getWidth(), 14.0f);
    {
        juce::ColourGradient grad (colours::bad, bar.getX(), 0.0f, colours::great, bar.getRight(), 0.0f, false);
        grad.addColour (0.32, colours::warn);
        grad.addColour (0.58, colours::mid);
        grad.addColour (0.80, colours::good);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, 7.0f);
        g.setColour (colours::background.withAlpha (0.7f));
        for (float z : { 40.0f, 70.0f, 90.0f })
            g.fillRect (bar.getX() + bar.getWidth() * z / 100.0f - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        if (state.hasBaseline && valid)
        {
            const float bx = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, state.baselineScore / 100.0f);
            g.setColour (colours::background);
            g.fillRect (bx - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
        }
        if (const auto* ref = state.activeRefView())
        {
            const float rx = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, ref->assessment.score / 100.0f);
            g.setColour (colours::background);
            g.fillEllipse (rx - 6.5f, bar.getCentreY() - 6.5f, 13.0f, 13.0f);
            g.setColour (colours::ref);
            g.fillEllipse (rx - 5.0f, bar.getCentreY() - 5.0f, 10.0f, 10.0f);
        }
        if (valid)
        {
            const float x = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, score / 100.0f);
            auto mk = juce::Rectangle<float> (x - 2.0f, bar.getY() - 5.0f, 4.0f, bar.getHeight() + 10.0f);
            g.setColour (juce::Colours::black);
            g.fillRoundedRectangle (mk.expanded (2.0f), 3.0f);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (mk, 2.0f);
        }
    }

    {
        auto zones = r.removeFromTop (28);
        const float widths[] = { 0.40f, 0.30f, 0.20f, 0.10f };
        const float scores[] = { 20.0f, 55.0f, 80.0f, 95.0f };
        float x = (float) zones.getX();
        const float lineH = zones.getHeight() * 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            const float w = zones.getWidth() * widths[i];
            auto box = juce::Rectangle<float> (x, (float) zones.getY(), w - 3.0f, (float) zones.getHeight());
            int lines = 2;
            // the two right-hand zones are narrow: they share their space on two lines,
            // "close enough" on top from its zone's start, "looks perfect" below, right-aligned
            if (i >= 2)
            {
                const float start = (float) zones.getX() + zones.getWidth() * (widths[0] + widths[1]);
                box = juce::Rectangle<float> (start, (float) zones.getY() + (i == 3 ? lineH : 0.0f), (float) zones.getRight() - start, lineH);
                lines = 1;
            }
            const bool current = valid && verdictFor (score, lang) == verdictFor (scores[i], lang);
            g.setColour (current ? col : colours::dim);
            g.setFont ((current ? fonts::labelBold (11.0f) : fonts::label (10.5f)).withExtraKerningFactor (0.04f));
            g.drawFittedText (upper (verdictFor (scores[i], lang)), box.toNearestInt(),
                              i == 3 ? juce::Justification::topRight : juce::Justification::topLeft, lines, 0.7f);
            x += w;
        }
    }

    juce::AttributedString a;
    a.setWordWrap (juce::AttributedString::byWord);
    a.append (state.assessment.nextStep, fonts::body (12.5f), colours::soft);
    if (valid && state.scoreHistory.size() > 36 && std::abs (state.trend) >= 3.0f)
        a.append (state.trend > 0.0f ? t (lang, "  Getting better.", "  Paranee.") : t (lang, "  Getting worse.", "  Huononee."),
                  fonts::bodyBold (12.5f), state.trend > 0.0f ? colours::good : colours::bad);
    if (const auto* ref = state.activeRefView())
        a.append (" " + state.refLabel() + t (lang, " scores ", " saa ") + juce::String (juce::roundToInt (ref->assessment.score))
                      + t (lang, ".", " pistettä."), fonts::body (12.5f), colours::ref);
    layoutText (a, (float) r.getWidth()).draw (g, r.withTrimmedTop (4).toFloat());
}

// =====================================================================================
namespace
{
    void playbackIcon (juce::Graphics& g, Playback p, juce::Rectangle<float> r)
    {
        const float s = r.getWidth() / 24.0f;
        auto pt = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * s, r.getY() + y * s); };
        juce::Path path;
        switch (p)
        {
            case Playback::phone:
                path.addRoundedRectangle (pt (6, 2).x, pt (6, 2).y, 12 * s, 20 * s, 2 * s);
                path.startNewSubPath (pt (10, 18)); path.lineTo (pt (14, 18));
                break;
            case Playback::car:
                path.startNewSubPath (pt (3, 15)); path.lineTo (pt (5, 9)); path.lineTo (pt (19, 9)); path.lineTo (pt (21, 15));
                path.lineTo (pt (21, 19)); path.lineTo (pt (3, 19)); path.closeSubPath();
                path.addEllipse (pt (5.5f, 15.5f).x, pt (5.5f, 15.5f).y, 3 * s, 3 * s);
                path.addEllipse (pt (15.5f, 15.5f).x, pt (15.5f, 15.5f).y, 3 * s, 3 * s);
                break;
            case Playback::headphones:
                path.startNewSubPath (pt (4, 14)); path.lineTo (pt (4, 12));
                path.cubicTo (pt (4, 1.5f), pt (20, 1.5f), pt (20, 12)); path.lineTo (pt (20, 14));
                path.addRoundedRectangle (pt (3, 14).x, pt (3, 14).y, 4 * s, 6 * s, 1 * s);
                path.addRoundedRectangle (pt (17, 14).x, pt (17, 14).y, 4 * s, 6 * s, 1 * s);
                break;
            case Playback::club:
                path.addRoundedRectangle (pt (5, 2).x, pt (5, 2).y, 14 * s, 20 * s, 2 * s);
                path.addEllipse (pt (8, 10).x, pt (8, 10).y, 8 * s, 8 * s);
                path.addEllipse (pt (10.5f, 4.5f).x, pt (10.5f, 4.5f).y, 3 * s, 3 * s);
                break;
            case Playback::count: break;
        }
        g.setColour (colours::dim);
        g.strokePath (path, juce::PathStrokeType (1.8f * s));
    }
}

void TranslationBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Translation", "Käännettävyys"), head);
    const auto* ref = state.activeRefView();
    drawLabel (g, t (lang, "estimated", "arvio") + (ref != nullptr ? juce::String (" | ref") : juce::String()), head,
               ref != nullptr ? colours::ref : colours::faint, juce::Justification::centredRight);
    r.removeFromTop (8);

    const bool valid = state.assessment.enoughData;
    const int rowH = r.getHeight() / (int) Playback::count;
    for (int i = 0; i < (int) Playback::count; ++i)
    {
        auto row = r.removeFromTop (rowH);
        const auto& tr = state.assessment.translation[(size_t) i];
        auto line = row.removeFromTop (20);
        playbackIcon (g, (Playback) i, line.removeFromLeft (20).toFloat().withSizeKeepingCentre (18.0f, 18.0f));
        line.removeFromLeft (8);
        g.setColour (colours::text);
        g.setFont (fonts::bodyBold (13.5f));
        g.drawText (playbackName ((Playback) i, lang), line.removeFromLeft (78), juce::Justification::centredLeft);
        auto num = line.removeFromRight (30);
        const auto col = valid ? scoreColour (tr.score) : colours::faint;
        g.setColour (col);
        g.setFont (fonts::num (13.0f));
        g.drawText (valid ? juce::String (juce::roundToInt (tr.score)) : dash, num, juce::Justification::centredRight);
        auto track = line.reduced (0, 6).toFloat().withTrimmedRight (8.0f);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (track, 3.5f);
        if (valid)
        {
            g.setColour (col);
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * juce::jlimit (0.02f, 1.0f, tr.score / 100.0f)), 3.5f);
            if (ref != nullptr)
            {
                const float rx = track.getX() + track.getWidth() * juce::jlimit (0.0f, 1.0f, ref->assessment.translation[(size_t) i].score / 100.0f);
                g.setColour (colours::ref);
                g.fillRoundedRectangle (rx - 1.0f, track.getY() - 3.0f, 2.0f, track.getHeight() + 6.0f, 1.0f);
            }
            g.setColour (colours::dim);
            g.setFont (fonts::body (11.5f));
            g.drawFittedText (tr.note, row.withTrimmedLeft (28).withHeight (juce::jmin (row.getHeight(), 30)), juce::Justification::topLeft, 2, 1.0f);
        }
    }
}

// =====================================================================================
ProgressBox::ProgressBox (const ViewState& s) : state (s)
{
    saveButton.getProperties().set ("primary", true);
    confirmButton.getProperties().set ("primary", true);
    saveButton.onClick = [this] { if (onSave) onSave(); };
    clearButton.onClick = [this] { if (onClear) onClear(); };
    compareButton.onClick = [this] { if (onCompare) onCompare(); };
    confirmButton.onClick = [this] { if (onConfirm) onConfirm(); };
    compareButton.setClickingTogglesState (false);
    for (auto* b : { &saveButton, &compareButton, &clearButton })
        addAndMakeVisible (b);
    addChildComponent (confirmButton);
    updateTexts();
}

void ProgressBox::syncButtons()
{
    confirmButton.setVisible (state.awaitingSaveConfirm);
    compareButton.setEnabled (! state.snapshots.empty());
    compareButton.setToggleState (state.compareOpen, juce::dontSendNotification);
}

juce::Rectangle<int> ProgressBox::plotArea() const
{
    auto r = content (*this);
    r.removeFromTop (16);
    r.removeFromBottom (32);
    return r;
}

void ProgressBox::updateTexts()
{
    const auto lang = state.lang;
    saveButton.setButtonText (upper (t (lang, "Save snapshot", "Tallenna snapshot")));
    saveButton.setTooltip (t (lang, "Save the current score. The cards then show what got better or worse since, and the chart shows your progress. Then listen: did it really get better?",
                                    "Tallenna nykyiset pisteet. Kortit näyttävät sen jälkeen, mikä parani tai huononi, ja kaavio näyttää edistymisen. Kuuntele sitten: parani oikeasti?"));
    clearButton.setButtonText (upper (t (lang, "Clear", "Tyhjennä")));
    clearButton.setTooltip (t (lang, "Forget the saved snapshots.", "Unohda tallennetut snapshotit."));
    saveButton.setButtonText (upper (t (lang, "Snapshot", "Snapshot")));
    compareButton.setButtonText (upper (t (lang, "Before/after", "Ennen/jälkeen")));
    compareButton.setTooltip (t (lang, "Compare the newest snapshot with now: which numbers got better, which got worse.",
                                       "Vertaa uusinta snapshotia nykyhetkeen: mitkä luvut paranivat, mitkä huononivat."));
    confirmButton.setButtonText (upper (t (lang, "I saved it", "Tallensin")));
    confirmButton.setTooltip (t (lang, "Click after you have saved the DAW project under a new name.",
                                       "Klikkaa, kun olet tallentanut DAW-projektin uudella nimellä."));
}

void ProgressBox::resized()
{
    auto r = content (*this);
    auto buttons = r.removeFromBottom (26);
    saveButton.setBounds (buttons.removeFromLeft (82));
    buttons.removeFromLeft (6);
    compareButton.setBounds (buttons.removeFromLeft (98));
    buttons.removeFromLeft (6);
    clearButton.setBounds (buttons);
    const auto plot = plotArea();
    confirmButton.setBounds (plot.getCentreX() - 60, plot.getBottom() - 30, 120, 26);
}

void ProgressBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Progress", "Edistyminen"), head);

    const auto& snaps = state.snapshots;
    const bool live = state.assessment.enoughData;
    if (! snaps.empty() && live)
    {
        const float d = state.displayScore - snaps.back().score;
        g.setFont (fonts::num (12.0f));
        g.setColour (d >= 1.0f ? colours::good : (d <= -1.0f ? colours::bad : colours::dim));
        g.drawText (fmt (lang, d, 0, true) + t (lang, " since snapshot", " snapshotista"), head, juce::Justification::centredRight);
    }

    r.removeFromBottom (32);
    if (state.awaitingSaveConfirm)
    {
        // until the user confirms: save the DAW project as a new version, so this point can be found again
        auto banner = r.toFloat().reduced (0.0f, 4.0f);
        g.setColour (colours::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (banner, 6.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (banner, 6.0f, 1.2f);
        auto text = banner.reduced (10.0f, 8.0f).withTrimmedBottom (34.0f).toNearestInt();
        g.setFont (fonts::bodyBold (13.0f));
        g.drawFittedText (t (lang, "Snapshot saved. Now save your DAW project as a new version",
                                   "Snapshot tallennettu. Tallenna nyt DAW-projekti uutena versiona"),
                          text.removeFromTop (34), juce::Justification::topLeft, 2);
        g.setColour (colours::soft);
        g.setFont (fonts::body (12.0f));
        const int n = (int) state.snapshots.size();
        g.drawFittedText (t (lang, "File > Save As..., e.g. \"My song v", "Tiedosto > Tallenna nimellä..., esim. \"Biisi v") + juce::String (n)
                              + t (lang, "\". Then you can go back to this point if the next change makes it worse.",
                                         "\". Silloin pääset palaamaan tähän, jos seuraava muutos huonontaa.") ,
                          text, juce::Justification::topLeft, 4);
        return;
    }
    auto plot = r.toFloat().withTrimmedLeft (22.0f).withTrimmedBottom (14.0f).withTrimmedTop (6.0f);
    auto yFor = [&] (float v) { return plot.getY() + plot.getHeight() * (100.0f - v) / 100.0f; };

    struct Zone { float lo, hi; juce::Colour c; };
    for (const auto& z : { Zone { 0, 40, colours::bad }, Zone { 40, 70, colours::mid }, Zone { 70, 90, colours::good }, Zone { 90, 100, colours::great } })
    {
        g.setColour (z.c.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yFor (z.hi), plot.getRight(), yFor (z.lo)));
    }
    g.setFont (fonts::num (8.5f));
    for (float v : { 40.0f, 70.0f, 90.0f })
    {
        const float dashes[] = { 2.0f, 3.0f };
        g.setColour (colours::line);
        g.drawDashedLine ({ plot.getX(), yFor (v), plot.getRight(), yFor (v) }, dashes, 2, 1.0f);
        g.setColour (colours::faint);
        g.drawText (juce::String ((int) v), juce::Rectangle<float> (plot.getX() - 22.0f, yFor (v) - 6.0f, 18.0f, 12.0f), juce::Justification::centredRight);
    }

    if (const auto* ref = state.activeRefView())
    {
        const float y = yFor (ref->assessment.score);
        const float refDashes[] = { 5.0f, 3.0f };
        g.setColour (colours::ref);
        g.drawDashedLine ({ plot.getX(), y, plot.getRight(), y }, refDashes, 2, 1.2f);
        g.setFont (fonts::labelBold (10.0f));
        g.drawText (upper (state.refLabel()) + dot + juce::String (juce::roundToInt (ref->assessment.score)),
                    juce::Rectangle<float> (plot.getX() + 4.0f, y - 13.0f, 120.0f, 11.0f), juce::Justification::centredLeft);
    }

    // saved snapshots, then the live score as the last point
    std::vector<std::pair<juce::String, float>> pts;
    const size_t first = snaps.size() > 7 ? snaps.size() - 7 : 0;
    for (size_t i = first; i < snaps.size(); ++i)
        pts.push_back ({ (snaps[i].confirmed ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")) : juce::String()) + snaps[i].time.formatted ("%H:%M"), snaps[i].score });
    if (live)
        pts.push_back ({ t (lang, "now", "nyt"), state.displayScore });

    if (pts.empty())
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (12.5f));
        g.drawFittedText (t (lang, "Save a snapshot before you change something, then watch the score.",
                                   "Tallenna snapshot ennen muutosta ja seuraa sitten pisteitä."),
                          plot.toNearestInt().reduced (10, 0), juce::Justification::centred, 3);
        return;
    }

    if (snaps.empty())
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (11.5f));
        g.drawFittedText (t (lang, "Save a snapshot before a change, then watch the score.",
                                   "Tallenna snapshot ennen muutosta ja seuraa pisteitä."),
                          plot.toNearestInt().reduced (8, 4).removeFromTop (34), juce::Justification::centredTop, 2);
    }

    auto xFor = [&] (size_t i) { return pts.size() == 1 ? plot.getCentreX() : plot.getX() + 8.0f + (plot.getWidth() - 16.0f) * (float) i / (float) (pts.size() - 1); };
    juce::Path line, area;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const juce::Point<float> p (xFor (i), yFor (pts[i].second));
        if (i == 0) { line.startNewSubPath (p); area.startNewSubPath (p.x, yFor (0.0f)); }
        else line.lineTo (p);
        area.lineTo (p);
    }
    area.lineTo (xFor (pts.size() - 1), yFor (0.0f));
    area.closeSubPath();
    g.setColour (colours::accent.withAlpha (0.12f));
    g.fillPath (area);
    g.setColour (colours::accent);
    g.strokePath (line, juce::PathStrokeType (2.0f));

    for (size_t i = 0; i < pts.size(); ++i)
    {
        const bool last = i == pts.size() - 1;
        const juce::Point<float> p (xFor (i), yFor (pts[i].second));
        const float rad = last ? 4.5f : 2.5f;
        g.setColour (last ? scoreColour (pts[i].second) : colours::accent);
        g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
        if (last)
        {
            g.setColour (colours::background);
            g.drawEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f, 1.5f);
        }
        g.setColour (colours::faint);
        g.setFont (fonts::num (8.5f));
        g.drawText (pts[i].first, juce::Rectangle<float> (p.x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 11.0f), juce::Justification::centred);
    }
}

// =====================================================================================
namespace
{
    juce::String duration (double secs)
    {
        const int s = (int) std::round (secs);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

juce::Rectangle<int> ReferenceBox::slotBounds (int i) const
{
    auto r = content (*this).withTrimmedTop (22);
    const int dropW = 150, gapW = 8;
    const int w = (r.getWidth() - dropW - 2 * gapW) / 2;
    return { r.getX() + i * (w + gapW), r.getY(), w, r.getHeight() };
}

juce::Rectangle<int> ReferenceBox::dropBounds() const
{
    auto r = content (*this).withTrimmedTop (22);
    return r.removeFromRight (150);
}

juce::Rectangle<int> ReferenceBox::waveBounds (int i) const
{
    return slotBounds (i).reduced (10, 0).withTrimmedBottom (7).removeFromBottom (18);
}

juce::Rectangle<int> ReferenceBox::closeBounds (int i) const
{
    auto s = slotBounds (i);
    return { s.getRight() - 24, s.getY() + 4, 20, 18 };
}

int ReferenceBox::slotAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) state.refs.size(); ++i)
        if (slotBounds (i).contains (p))
            return i;
    return -1;
}

void ReferenceBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Reference tracks", "Referenssiraidat"), head);
    drawLabel (g, t (lang, "drag audio here", "raahaa ääni tähän") + dot + "WAV" + dot + "AIFF" + dot + "FLAC" + dot + "MP3",
               head, colours::faint, juce::Justification::centredRight);

    for (int i = 0; i < (int) state.refs.size(); ++i)
    {
        const auto& ref = state.refs[(size_t) i];
        const bool act = i == state.activeRef && ref.ready();
        auto sb = slotBounds (i).toFloat();
        g.setColour (colours::raised);
        g.fillRoundedRectangle (sb, 7.0f);
        g.setColour (act ? colours::ref : colours::line);
        g.drawRoundedRectangle (sb.reduced (0.5f), 7.0f, act ? 2.0f : 1.0f);

        auto c = slotBounds (i).reduced (10, 7);
        auto line1 = c.removeFromTop (18);
        auto line2 = c.removeFromTop (15);
        const auto slotName = "Ref " + juce::String (i + 1);

        if (ref.status == RefView::Status::empty)
        {
            g.setColour (colours::faint);
            g.setFont (fonts::bodyBold (13.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b ")) + slotName + dot + t (lang, "empty", "tyhjä"), line1, juce::Justification::centredLeft);
            g.setFont (fonts::body (12.0f));
            g.drawText (t (lang, "Drop a track or click to browse", "Pudota raita tai klikkaa"), line2, juce::Justification::centredLeft);
            continue;
        }

        juce::String title = juce::String (juce::CharPointer_UTF8 (act ? "\xe2\x97\x8f " : "\xe2\x97\x8b ")) + slotName;
        if (ref.name.isNotEmpty())
            title << dot << ref.name;
        g.setColour (act ? colours::text : colours::soft);
        g.setFont (fonts::bodyBold (13.0f));
        g.drawText (title, line1.withTrimmedRight (22), juce::Justification::centredLeft, true);

        // remove
        auto cb = closeBounds (i).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (colours::dim);
        g.drawLine ({ cb.getTopLeft(), cb.getBottomRight() }, 1.5f);
        g.drawLine ({ cb.getBottomLeft(), cb.getTopRight() }, 1.5f);

        g.setFont (fonts::num (11.0f));
        if (ref.status == RefView::Status::loading)
        {
            g.setColour (colours::warn);
            g.drawText (t (lang, "Analysing...", "Analysoidaan..."), line2, juce::Justification::centredLeft);
            continue;
        }
        if (ref.status == RefView::Status::failed)
        {
            g.setColour (colours::bad);
            g.drawText (ref.error.isNotEmpty() ? ref.error : t (lang, "Could not read the file", "Tiedostoa ei voitu lukea"), line2, juce::Justification::centredLeft);
            continue;
        }

        g.setColour (colours::dim);
        g.drawText (fmt (lang, ref.snap.integrated) + " LUFS" + dot + duration (ref.seconds) + dot + t (lang, "analysed", "analysoitu"),
                    line2, juce::Justification::centredLeft);

        // waveform
        auto wb = waveBounds (i).toFloat();
        const auto n = ref.wave.size();
        if (n > 0)
        {
            const float colW = wb.getWidth() / (float) n;
            g.setColour (act ? colours::ref : colours::faint);
            for (size_t k = 0; k < n; ++k)
            {
                const float h = juce::jmax (1.0f, ref.wave[k] * wb.getHeight());
                g.fillRect (wb.getX() + colW * (float) k, wb.getCentreY() - h * 0.5f, juce::jmax (0.8f, colW * 0.7f), h);
            }
            if (act && state.listening)
            {
                const float x = wb.getX() + wb.getWidth() * (float) juce::jlimit (0.0, 1.0, state.playPosition);
                g.setColour (juce::Colours::white);
                g.fillRect (x - 0.6f, wb.getY() - 2.0f, 1.2f, wb.getHeight() + 4.0f);
            }
        }
    }

    // drop zone
    auto db = dropBounds().toFloat();
    {
        juce::Path outline;
        outline.addRoundedRectangle (db.reduced (1.0f), 7.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (state.dragOver ? colours::ref : colours::faint);
        g.fillPath (dashed);
        if (state.dragOver)
        {
            g.setColour (colours::ref.withAlpha (0.12f));
            g.fillRoundedRectangle (db.reduced (1.0f), 7.0f);
        }
    }
    auto dc = db.toNearestInt().withSizeKeepingCentre (db.toNearestInt().getWidth() - 12, 34);
    g.setColour (colours::text);
    g.setFont (fonts::labelBold (13.0f));
    g.drawText (upper (t (lang, "+ Drop track", "+ Pudota raita")), dc.removeFromTop (17), juce::Justification::centred);
    g.setColour (colours::dim);
    g.setFont (fonts::body (12.0f));
    g.drawText (t (lang, "or click to browse", "tai klikkaa ja valitse"), dc, juce::Justification::centred);
}

void ReferenceBox::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (dropBounds().contains (p))
    {
        if (onBrowse) onBrowse (-1);
        return;
    }
    for (int i = 0; i < (int) state.refs.size(); ++i)
    {
        if (! slotBounds (i).contains (p))
            continue;
        const auto& ref = state.refs[(size_t) i];
        if (ref.status == RefView::Status::empty)
        {
            if (onBrowse) onBrowse (i);
        }
        else if (closeBounds (i).contains (p))
        {
            if (onRemove) onRemove (i);
        }
        else if (ref.ready())
        {
            if (onSelect) onSelect (i);
            const auto wb = waveBounds (i).expanded (0, 4);
            if (wb.contains (p) && onCue)
                onCue (i, (double) (p.x - wb.getX()) / (double) juce::jmax (1, wb.getWidth()));
        }
        else if (ref.status == RefView::Status::failed)
        {
            if (onBrowse) onBrowse (i);
        }
        return;
    }
}

// =====================================================================================
void CompareBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Mix vs reference", "Miksi vs referenssi"), head);
    drawLabel (g, t (lang, "live", "livenä"), head, colours::faint, juce::Justification::centredRight);
    r.removeFromTop (4);

    const auto* ref = state.activeRefView();
    const auto& m = state.assessment.measures;
    if (ref == nullptr || ! m.valid)
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (12.5f));
        g.drawFittedText (ref == nullptr ? t (lang, "Load a reference track to compare.", "Lataa referenssiraita vertailuun.")
                                         : t (lang, "Play your mix...", "Soita miksiä..."),
                          r, juce::Justification::centred, 3);
        return;
    }
    const auto& rm = ref->assessment.measures;

    const int c1 = r.getX() + (int) (r.getWidth() * 0.47f), c2 = r.getX() + (int) (r.getWidth() * 0.66f), c3 = r.getX() + (int) (r.getWidth() * 0.84f);
    auto header = r.removeFromTop (16);
    g.setFont (fonts::label (10.5f));
    auto cell = [&] (juce::Rectangle<int> row, int from, int to, const juce::String& txt, juce::Colour c)
    {
        g.setColour (c);
        g.drawText (txt, juce::Rectangle<int> (from, row.getY(), to - from, row.getHeight()), juce::Justification::centredRight);
    };
    g.setColour (colours::dim);
    g.drawText (upper (t (lang, "Metric", "Mittari")), header, juce::Justification::centredLeft);
    cell (header, c1 - 40, c1, upper (t (lang, "Mix", "Miksi")), colours::dim);
    cell (header, c2 - 40, c2, "REF", colours::ref);
    cell (header, c3 - 40, r.getRight(), juce::String (juce::CharPointer_UTF8 ("\xce\x94")), colours::dim);

    struct Row { juce::String name, mix, ref, delta; juce::Colour deltaColour; bool matched; };
    std::vector<Row> rows;
    auto num = [&] (float mix, float refV, float warnAt, float badAt, int decimals = 1)
    {
        const float d = mix - refV;
        const auto col = std::abs (d) > badAt ? colours::bad : (std::abs (d) > warnAt ? colours::warn : colours::text);
        return std::make_tuple (fmt (lang, mix, decimals), fmt (lang, refV, decimals), fmt (lang, d, decimals, true), col);
    };
    auto add = [&] (const juce::String& name, float mix, float refV, float warnAt, float badAt)
    {
        auto [a, b, c, col] = num (mix, refV, warnAt, badAt);
        rows.push_back ({ name, a, b, c, col, false });
    };
    add (t (lang, "Integrated", "Integroitu"), m.loudness, ref->snap.integrated, 1.0f, 3.0f);
    add ("True peak", m.truePeak, rm.truePeak, 1.0f, 2.0f);
    add ("PLR", m.plr, rm.plr, 1.5f, 3.0f);
    add ("LRA", m.lra, rm.lra, 2.0f, 4.0f);
    add (t (lang, "Width S/M", "Leveys S/M"), m.width, rm.width, 3.0f, 6.0f);

    // the three ranges that differ most, level-matched
    std::array<int, numRanges> order {};
    for (int i = 0; i < numRanges; ++i) order[(size_t) i] = i;
    auto diff = [&] (int i) { return m.rangeDeviation[(size_t) i] - rm.rangeDeviation[(size_t) i]; };
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return std::abs (diff (a)) > std::abs (diff (b)); });
    for (int k = 0; k < 3; ++k)
    {
        const int i = order[(size_t) k];
        const float d = diff (i), tl = tolFor (state.profile, i);
        const auto col = std::abs (d) > 2.0f * tl ? colours::bad : (std::abs (d) > tl * 0.6f ? colours::warn : colours::text);
        rows.push_back ({ rangeNickname ((Range) i, lang), {}, {}, fmt (lang, d, 1, true), col, true });
    }

    const int rowH = juce::jmin (19, r.getHeight() / (int) rows.size());
    for (const auto& row : rows)
    {
        auto rr = r.removeFromTop (rowH);
        g.setColour (colours::line);
        g.drawHorizontalLine (rr.getY(), (float) rr.getX(), (float) rr.getRight());
        g.setColour (colours::soft);
        g.setFont (fonts::body (12.5f));
        g.drawText (row.name, rr, juce::Justification::centredLeft);
        g.setFont (fonts::num (11.5f));
        if (row.matched)
        {
            g.setColour (colours::dim);
            g.setFont (fonts::body (11.5f));
            g.drawText (t (lang, "level-matched", "tasot tasattu"), juce::Rectangle<int> (c1 - 60, rr.getY(), c2 - c1 + 60, rr.getHeight()), juce::Justification::centredRight);
            g.setFont (fonts::num (11.5f));
        }
        else
        {
            cell (rr, c1 - 50, c1, row.mix, colours::text);
            cell (rr, c2 - 50, c2, row.ref, colours::ref);
        }
        cell (rr, c3 - 40, r.getRight(), row.delta, row.deltaColour);
    }
}

// =====================================================================================
BeforeAfterBox::BeforeAfterBox (const ViewState& s) : state (s)
{
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    updateTexts();
}

void BeforeAfterBox::updateTexts()
{
    closeButton.setButtonText (upper (t (state.lang, "Close", "Sulje")));
}

void BeforeAfterBox::resized()
{
    const auto r = content (*this);
    closeButton.setBounds (r.getRight() - 70, r.getY() - 2, 70, 22);
}

void BeforeAfterBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    auto head = r.removeFromTop (18);
    if (state.snapshots.empty())
    {
        drawLabel (g, t (lang, "Before / after", "Ennen / jälkeen"), head);
        return;
    }
    const auto& snap = state.snapshots.back();
    drawLabel (g, t (lang, "Before / after", "Ennen / jälkeen") + dot + t (lang, "snapshot ", "snapshot ") + juce::String ((int) state.snapshots.size())
                      + " (" + snap.time.formatted ("%H:%M") + ")" + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + t (lang, "now", "nyt"), head);
    head.removeFromRight (78);
    drawLabel (g, snap.confirmed ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 ")) + t (lang, "DAW version saved", "DAW-versio tallennettu")
                                 : t (lang, "DAW version not confirmed", "DAW-versiota ei kuitattu"),
              head, snap.confirmed ? colours::good : colours::warn, juce::Justification::centredRight);
    r.removeFromTop (8);

    // the score, big
    auto scoreRow = r.removeFromTop (34);
    const float now = state.assessment.enoughData ? state.assessment.score : -1.0f;
    g.setFont (fonts::numBold (26.0f));
    g.setColour (scoreColour (snap.score));
    g.drawText (juce::String (juce::roundToInt (snap.score)), scoreRow.removeFromLeft (50), juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (fonts::body (18.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), scoreRow.removeFromLeft (30), juce::Justification::centred);
    if (now >= 0.0f)
    {
        g.setFont (fonts::numBold (26.0f));
        g.setColour (scoreColour (now));
        g.drawText (juce::String (juce::roundToInt (now)), scoreRow.removeFromLeft (50), juce::Justification::centredLeft);
        const float d = now - snap.score;
        g.setFont (fonts::bodyBold (14.0f));
        g.setColour (d >= 1.0f ? colours::good : (d <= -1.0f ? colours::bad : colours::dim));
        g.drawText (fmt (lang, d, 0, true) + t (lang, " points", " pistettä"), scoreRow.removeFromLeft (110), juce::Justification::centredLeft);
    }
    int better = 0, worse = 0;
    for (const auto& c : state.changes) { better += c.verdict > 0; worse += c.verdict < 0; }
    g.setFont (fonts::body (12.5f));
    g.setColour (colours::soft);
    g.drawText (juce::String (better) + t (lang, " better, ", " parempaa, ") + juce::String (worse) + t (lang, " worse", " huonompaa"),
                scoreRow, juce::Justification::centredRight);
    r.removeFromTop (6);

    if (state.changes.empty())
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (13.0f));
        g.drawFittedText (t (lang, "Play the song to compare it with the snapshot.", "Soita kappaletta, niin vertaan sitä snapshotiin."),
                          r.removeFromTop (40), juce::Justification::centredLeft, 2);
        return;
    }

    // table: better first, then worse, then unchanged
    auto footer = r.removeFromBottom (18);
    const int rowH = 19;
    const int cols[] = { 0, 230, 340, 450 };
    auto header = r.removeFromTop (16);
    g.setFont (fonts::label (10.5f));
    g.setColour (colours::faint);
    g.drawText (upper (t (lang, "Measure", "Mittari")), header.withX (header.getX() + cols[0]).withWidth (220), juce::Justification::centredLeft);
    g.drawText (upper (t (lang, "Before", "Ennen")), header.withX (header.getX() + cols[1]).withWidth (100), juce::Justification::centredLeft);
    g.drawText (upper (t (lang, "Now", "Nyt")), header.withX (header.getX() + cols[2]).withWidth (100), juce::Justification::centredLeft);
    int shown = 0, hidden = 0;
    for (const auto& c : state.changes)
    {
        if (r.getHeight() < rowH)
        {
            ++hidden;
            continue;
        }
        auto row = r.removeFromTop (rowH);
        if (shown++ % 2 == 0)
        {
            g.setColour (colours::raised.withAlpha (0.5f));
            g.fillRect (row);
        }
        const auto colour = c.verdict > 0 ? colours::good : (c.verdict < 0 ? colours::bad : colours::dim);
        g.setFont (fonts::body (12.5f));
        g.setColour (c.verdict == 0 ? colours::dim : colours::text);
        g.drawText (metricName (c.id, lang), row.withX (row.getX() + cols[0] + 4).withWidth (220), juce::Justification::centredLeft);
        g.setFont (fonts::num (11.5f));
        g.setColour (colours::dim);
        g.drawText (metricValueText (c.id, c.before, lang), row.withX (row.getX() + cols[1]).withWidth (105), juce::Justification::centredLeft);
        g.setColour (c.verdict == 0 ? colours::dim : colours::text);
        g.drawText (metricValueText (c.id, c.now, lang), row.withX (row.getX() + cols[2]).withWidth (105), juce::Justification::centredLeft);
        g.setFont (fonts::labelBold (10.5f));
        g.setColour (colour);
        g.drawText (c.verdict > 0 ? upper (t (lang, "Better", "Parempi")) : (c.verdict < 0 ? upper (t (lang, "Worse", "Huonompi")) : juce::String ("=")),
                    row.withX (row.getX() + cols[3]).withRight (row.getRight() - 4), juce::Justification::centredLeft);
    }
    g.setFont (fonts::body (11.5f));
    g.setColour (colours::faint);
    g.drawFittedText ((hidden > 0 ? juce::String (hidden) + t (lang, " more unchanged. ", " muuta ennallaan. ") : juce::String())
                          + t (lang, "Numbers are advice: open your saved DAW versions and listen to both.",
                                     "Luvut ovat neuvoja: avaa tallennetut DAW-versiot ja kuuntele molemmat."),
                      footer, juce::Justification::centredLeft, 1);
}

// =====================================================================================
LessonBox::LessonBox (const ViewState& s) : state (s)
{
    prevButton.onClick = [this] { if (onPrev) onPrev(); };
    nextButton.onClick = [this] { if (onNext) onNext(); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    updateTexts();
}

void LessonBox::updateTexts()
{
    prevButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80 ")) + upper (t (state.lang, "Previous", "Edellinen")));
    nextButton.setButtonText (upper (t (state.lang, "Next", "Seuraava")) + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xb6")));
}

void LessonBox::resized()
{
    auto r = content (*this);
    auto buttons = r.removeFromBottom (26);
    nextButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    prevButton.setBounds (buttons.removeFromRight (110));
}

void LessonBox::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = content (*this);
    r.removeFromBottom (30);
    const int count = lessonCount(), index = juce::jlimit (0, count - 1, state.lessonIndex);
    const auto text = lessonText (index, lang);
    const auto id = juce::String (lessonId (index));
    const bool done = state.lessonsDone.contains (id);

    auto head = r.removeFromTop (16);
    drawLabel (g, t (lang, "Lesson ", "Oppitunti ") + juce::String (index + 1) + " / " + juce::String (count), head);
    // one dot per lesson: filled = done, ring = the open one
    for (int i = count - 1; i >= 0; --i)
    {
        auto d = head.removeFromRight (14).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        const bool isDone = state.lessonsDone.contains (juce::String (lessonId (i)));
        g.setColour (isDone ? colours::good : colours::line);
        g.fillEllipse (d);
        if (i == index)
        {
            g.setColour (colours::accent);
            g.drawEllipse (d.expanded (2.0f), 1.2f);
        }
    }
    r.removeFromTop (4);

    g.setColour (colours::text);
    g.setFont (fonts::bodyBold (17.0f));
    g.drawFittedText (text.title, r.removeFromTop (24), juce::Justification::centredLeft, 1);
    g.setColour (colours::soft);
    g.setFont (fonts::body (12.5f));
    g.drawFittedText (text.why, r.removeFromTop (34), juce::Justification::topLeft, 2);
    r.removeFromTop (2);
    g.setColour (colours::text);
    g.drawFittedText (text.steps, r.removeFromTop (82), juce::Justification::topLeft, 5, 0.9f);
    r.removeFromTop (6);

    // the live meter towards the goal
    const auto& c = state.lesson;
    auto meter = r.removeFromTop (40);
    auto labels = meter.removeFromTop (16);
    if (c.ready)
    {
        const auto font = fonts::num (11.5f);
        g.setFont (font);
        // the goal keeps its room on the right; the current value squeezes in the rest
        const int goalW = juce::jmin (labels.getWidth() / 2, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, c.goal)) + 2);
        g.setColour (colours::dim);
        g.drawFittedText (c.goal, labels.removeFromRight (goalW), juce::Justification::centredRight, 1, 0.8f);
        labels.removeFromRight (10);
        g.setColour (colours::text);
        g.drawFittedText (c.now, labels, juce::Justification::centredLeft, 1, 0.8f);
    }
    auto bar = meter.removeFromTop (14).toFloat();
    g.setColour (colours::raised);
    g.fillRoundedRectangle (bar, 4.0f);
    if (c.ready)
    {
        const auto fill = c.passed ? colours::good : colours::warn.interpolatedWith (colours::good, c.progress * 0.6f);
        g.setColour (fill);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * c.progress)), 4.0f);
    }
    r.removeFromTop (4);

    auto status = r.removeFromTop (22);
    if (done || (c.passed && state.lessonHeld >= 3.0f))
    {
        g.setColour (colours::good);
        g.setFont (fonts::bodyBold (13.0f));
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 ")) + t (lang, "Done! ", "Onnistui! ") + text.doneNote,
                          status, juce::Justification::centredLeft, 2, 0.85f);
    }
    else if (! c.ready)
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (12.5f));
        g.drawFittedText (c.waitingFor, status, juce::Justification::centredLeft, 1);
    }
    else if (c.passed)
    {
        g.setColour (colours::good);
        g.setFont (fonts::body (12.5f));
        g.drawFittedText (t (lang, "That's it, keep it there for a few seconds...", "Juuri noin, pidä se siinä muutama sekunti..."),
                          status, juce::Justification::centredLeft, 1);
    }
    else
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (12.5f));
        g.drawFittedText (t (lang, "Loop a part, change one thing at a time and watch the bar.", "Looppaa kohtaa, muuta yhtä asiaa kerrallaan ja seuraa palkkia."),
                          status, juce::Justification::centredLeft, 1);
    }
}

} // namespace wwma::ui
