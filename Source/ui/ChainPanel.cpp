#include "ChainPanel.h"
#include "PluginProcessor.h"
#include <cmath>

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }

    const juce::String minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    const juce::String arrowRight = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 "));
    const juce::String tick = juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 "));
    const juce::String dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));

    juce::String fmt (Lang l, float v, int decimals = 1, bool sign = false)
    {
        if (v <= -100.0f || ! std::isfinite (v))
            return dash;
        auto s = decimals == 0 ? juce::String (juce::roundToInt (std::abs (v))) : juce::String (std::abs (v), decimals);
        if (v < 0.0f && s.containsAnyOf ("123456789"))
            s = minus + s;
        else if (sign && s.containsAnyOf ("123456789"))
            s = "+" + s;
        return l == Lang::finnish ? s.replaceCharacter ('.', ',') : s;
    }

    constexpr int colW = 88, headH = 22, curveH = 40, namesH = 14, gainRowH = 40, rowH = 36;
    constexpr int knobGain = 36, knobSmall = 30;
}

ChainPanel::ChainPanel (const ViewState& s, juce::AudioProcessorValueTreeState& apvts) : state (s)
{
    using Ids = WwmaAudioProcessor::ChainIds;
    auto setup = [&] (Knob& k, const char* id, Knob::Kind kind, bool limiter)
    {
        k.kind = kind;
        k.limiter = limiter;
        k.paramName = apvts.getParameter (id)->getName (64);
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider.setPopupDisplayEnabled (false, false, nullptr);
        k.slider.setVelocityBasedMode (false);
        k.slider.setMouseDragSensitivity (180);
        k.slider.getProperties().set ("bipolar", kind == Knob::Kind::gain && ! limiter);
        k.slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, k.slider);
        all.push_back (&k);
    };
    for (size_t b = 0; b < 4; ++b)
    {
        setup (gain[b], Ids::gain[b], Knob::Kind::gain, false);
        setup (freq[b], Ids::freq[b], Knob::Kind::freq, false);
        gain[b].suggestion = &state.chainAdvice.gain[b];
        freq[b].suggestion = &state.chainAdvice.freq[b];
        gain[b].slider.setDoubleClickReturnValue (true, 0.0);
    }
    for (size_t i = 0; i < 2; ++i)
    {
        setup (q[i], Ids::q[i + 1], Knob::Kind::q, false);
        q[i].suggestion = &state.chainAdvice.q[i + 1];
    }
    setup (limGain, Ids::limGain, Knob::Kind::gain, true);
    setup (ceiling, Ids::limCeiling, Knob::Kind::ceiling, true);
    setup (release, Ids::limRelease, Knob::Kind::ms, true);
    limGain.suggestion = &state.chainAdvice.limiterGain;
    ceiling.suggestion = &state.chainAdvice.ceiling;
    release.suggestion = &state.chainAdvice.release;
    limGain.slider.setDoubleClickReturnValue (true, 0.0);

    for (auto* b : { &eqOn, &limOn })
    {
        b->setClickingTogglesState (true);
        b->onStateChange = [this] { repaint(); };
        addAndMakeVisible (b);
    }
    eqOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, Ids::eqOn, eqOn);
    limOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, Ids::limOn, limOn);

    withoutButton.setClickingTogglesState (true);
    withoutButton.getProperties().set ("onColour", (juce::int64) colours::suggest.getARGB());
    withoutButton.onClick = [this] { if (onListenWithout) onListenWithout (withoutButton.getToggleState()); };
    addAndMakeVisible (withoutButton);

    showSuggestions.onClick = [this] { if (onShowSuggestions) onShowSuggestions (showSuggestions.getToggleState()); };
    addAndMakeVisible (showSuggestions);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    updateTexts();
}

ChainPanel::~ChainPanel()
{
    // the attachments must go before the sliders and buttons they hold on to
    eqOnAttachment.reset();
    limOnAttachment.reset();
    for (auto* k : all)
        k->attachment.reset();
}

void ChainPanel::updateTexts()
{
    const auto lang = state.lang;
    eqOn.setButtonText ("EQ");
    eqOn.setTooltip (t (lang, "Mastering EQ on/off. Off by default; it fades in and out, so switching is safe while playing.",
                              "Masterointi-EQ päälle/pois. Oletuksena pois; se feidaa sisään ja ulos, joten kytkeminen soiton aikana on turvallista."));
    limOn.setButtonText (upper (t (lang, "Limiter", "Limitteri")));
    limOn.setTooltip (t (lang, "True peak limiter on/off. Off by default. Keep it the very last thing before the export.",
                               "True peak -limitteri päälle/pois. Oletuksena pois. Pidä se aivan viimeisenä ennen vientiä."));
    withoutButton.setButtonText (upper (t (lang, "Hear without", "Kuuntele ilman")));
    withoutButton.setTooltip (t (lang, "Plays your mix without the EQ and limiter, at the same loudness, so you hear what they do to the sound and not just to the volume. Louder always sounds better for a moment.",
                                       "Soittaa miksin ilman EQ:ta ja limitteriä samalla äänekkyydellä, jotta kuulet mitä ne tekevät soundille eikä vain voimakkuudelle. Kovempi kuulostaa aina hetken paremmalta."));
    showSuggestions.setButtonText (t (lang, "Suggestions", "Ehdotukset"));
    showSuggestions.setTooltip (t (lang, "Show WWMA's suggested values next to the knobs. They are only suggestions: you turn the knobs, your ears decide.",
                                         "Näytä WWMA:n ehdottamat arvot nuppien vieressä. Ne ovat vain ehdotuksia: sinä käännät nupit, korva ratkaisee."));
    closeButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
    closeButton.setTooltip (t (lang, "Close (the EQ and limiter keep their settings)", "Sulje (EQ ja limitteri pitävät asetuksensa)"));
    refresh();
}

juce::String ChainPanel::valueText (const Knob& k, float v) const
{
    const auto lang = state.lang;
    switch (k.kind)
    {
        case Knob::Kind::gain:    return fmt (lang, v, 1, true) + " dB";
        case Knob::Kind::ceiling: return fmt (lang, v, 1) + " dBTP";
        case Knob::Kind::ms:      return juce::String (juce::roundToInt (v)) + " ms";
        case Knob::Kind::q:       return "Q " + fmt (lang, v, 2);
        case Knob::Kind::freq:    break;
    }
    return v >= 1000.0f ? fmt (lang, v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

void ChainPanel::refresh()
{
    showSuggestions.setToggleState (state.showSuggestions, juce::dontSendNotification);
    withoutButton.setToggleState (state.listeningWithout, juce::dontSendNotification);
    withoutButton.setEnabled (state.chainSettings.anyOn());

    const auto lang = state.lang;
    for (auto* k : all)
    {
        auto& props = k->slider.getProperties();
        const bool sectionOn = k->limiter ? state.chainSettings.limiterOn : state.chainSettings.eqOn;
        props.set ("off", ! sectionOn);
        const auto* s = k->suggestion;
        const bool show = state.showSuggestions && state.chainAdvice.ready && s != nullptr && s->shown;
        props.set ("suggState", show ? (s->close ? 2 : 1) : 0);
        if (show)
            props.set ("sugg", (double) s->value);

        auto tip = k->paramName;
        if (show)
            tip << "\n" << t (lang, "Suggested: ", "Ehdotus: ") << s->text << (s->close ? t (lang, " (you're there)", " (olet siinä)") : juce::String())
                << "\n" << s->why;
        if (k->slider.getTooltip() != tip)
            k->slider.setTooltip (tip);
        k->slider.repaint();
    }
    repaint();
}

juce::Rectangle<int> ChainPanel::eqArea() const
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (16 + 6);
    return r.removeFromLeft (4 * colW);
}

juce::Rectangle<int> ChainPanel::limiterArea() const
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (16 + 6);
    r.removeFromLeft (4 * colW + 14);
    return r;
}

void ChainPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto head = r.removeFromTop (16).withSizeKeepingCentre (r.getWidth(), 22);
    closeButton.setBounds (head.removeFromRight (24));
    head.removeFromRight (8);
    withoutButton.setBounds (head.removeFromRight (124));
    head.removeFromRight (8);
    showSuggestions.setBounds (head.removeFromRight (112));

    auto knobAt = [] (Knob& k, juce::Rectangle<int> row, int size)
    {
        k.slider.setBounds (row.getX(), row.getY() + (row.getHeight() - size) / 2, size, size);
    };

    const auto eq = eqArea();
    eqOn.setBounds (eq.getX(), eq.getY() + 1, 46, 20);
    const int rowsTop = eq.getY() + headH + curveH + namesH;
    for (int b = 0; b < 4; ++b)
    {
        const int x = eq.getX() + b * colW;
        knobAt (gain[(size_t) b], { x, rowsTop, colW, gainRowH }, knobGain);
        knobAt (freq[(size_t) b], { x, rowsTop + gainRowH, colW, rowH }, knobSmall);
    }
    for (int i = 0; i < 2; ++i)
        knobAt (q[(size_t) i], { eq.getX() + (i + 1) * colW, rowsTop + gainRowH + rowH, colW, rowH }, knobSmall);

    const auto lim = limiterArea();
    limOn.setBounds (lim.getX(), lim.getY() + 1, 78, 20);
    knobAt (limGain, { lim.getX(), rowsTop, colW, gainRowH }, knobGain);
    knobAt (ceiling, { lim.getX(), rowsTop + gainRowH, colW, rowH }, knobSmall);
    knobAt (release, { lim.getX(), rowsTop + gainRowH + rowH, colW, rowH }, knobSmall);
}

void ChainPanel::paintCurve (juce::Graphics& g, juce::Rectangle<float> r) const
{
    g.setColour (colours::raised.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 4.0f);
    const float range = 6.0f; // ±6 dB shown
    auto xFor = [&] (double hz) { return r.getX() + r.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    auto yFor = [&] (double db) { return r.getCentreY() - (float) juce::jlimit (-range * 1.1, range * 1.1, db) / range * (r.getHeight() * 0.5f - 3.0f); };
    g.setColour (colours::line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 2.0f, r.getRight() - 2.0f);
    for (auto f : { 100.0, 1000.0, 10000.0 })
        g.drawVerticalLine ((int) xFor (f), r.getY() + 2.0f, r.getBottom() - 2.0f);

    auto pathFor = [&] (const ChainSettings& s)
    {
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / 120.0);
            const auto pt = juce::Point<float> (xFor (hz), yFor (MasterEq::responseDb (s, hz)));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    };

    // the suggested curve, dashed, under yours
    if (state.showSuggestions && state.chainAdvice.ready)
    {
        ChainSettings sugg = state.chainSettings;
        for (size_t b = 0; b < 4; ++b)
        {
            sugg.gain[b] = state.chainAdvice.gain[b].value;
            if (state.chainAdvice.freq[b].shown) sugg.freq[b] = state.chainAdvice.freq[b].value;
            if (state.chainAdvice.q[b].shown) sugg.q[b] = state.chainAdvice.q[b].value;
        }
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, pathFor (sugg), dashes, 2);
        g.setColour (colours::suggest.withAlpha (0.8f));
        g.fillPath (dashed);
    }
    g.setColour (state.chainSettings.eqOn ? colours::accent : colours::faint);
    g.strokePath (pathFor (state.chainSettings), juce::PathStrokeType (1.8f));

    g.setFont (fonts::num (8.5f));
    g.setColour (colours::faint);
    g.drawText ("+6", r.withHeight (10.0f).translated (3.0f, 1.0f), juce::Justification::topLeft);
    g.drawText (minus + "6", r.withTrimmedTop (r.getHeight() - 11.0f).translated (3.0f, -1.0f), juce::Justification::bottomLeft);
}

void ChainPanel::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    const auto& adv = state.chainAdvice;
    const bool showSugg = state.showSuggestions;
    drawBox (g, getLocalBounds().toFloat());
    auto r = getLocalBounds().reduced (12, 10);
    drawLabel (g, t (lang, "Mastering EQ + limiter", "Masterointi-EQ + limitteri"), r.removeFromTop (16));

    // text next to a knob: value, then the suggestion
    auto knobText = [&] (const Knob& k, juce::Rectangle<int> row, int knobSize, const juce::String& caption)
    {
        auto text = row.withTrimmedLeft (knobSize + 6);
        const bool on = k.limiter ? state.chainSettings.limiterOn : state.chainSettings.eqOn;
        int lines = 1 + (showSugg ? 1 : 0) + (caption.isNotEmpty() ? 1 : 0);
        auto line = text.withSizeKeepingCentre (text.getWidth(), lines * 12).removeFromTop (12);
        if (caption.isNotEmpty())
        {
            drawLabel (g, caption, line, colours::faint, juce::Justification::centredLeft, 10.0f);
            line.translate (0, 12);
        }
        g.setColour (on ? colours::text : colours::dim);
        g.setFont (fonts::num (10.5f));
        g.drawFittedText (valueText (k, (float) k.slider.getValue()), line, juce::Justification::centredLeft, 1, 0.8f);
        if (! showSugg)
            return;
        line.translate (0, 12);
        const auto* s = k.suggestion;
        if (! adv.ready || s == nullptr || ! s->shown)
        {
            g.setColour (colours::faint);
            g.drawText (dash, line, juce::Justification::centredLeft);
            return;
        }
        g.setColour (s->close ? colours::good : colours::suggest);
        g.drawFittedText ((s->close ? tick : arrowRight) + s->text, line, juce::Justification::centredLeft, 1, 0.75f);
    };

    // ---- EQ
    const auto eq = eqArea();
    {
        auto head = eq.withHeight (headH).withTrimmedLeft (54);
        g.setColour (state.chainSettings.eqOn ? colours::accent : colours::dim);
        g.setFont (fonts::body (12.0f));
        g.drawFittedText (state.chainSettings.eqOn ? t (lang, "On: you hear it", "Päällä: kuulet sen") : t (lang, "Off (bypassed)", "Pois (ohitettu)"),
                          head, juce::Justification::centredLeft, 1);
        paintCurve (g, eq.withTrimmedTop (headH).withHeight (curveH - 4).toFloat());
        if (showSugg)
        {
            auto legend = eq.withTrimmedTop (headH).withHeight (12).withTrimmedRight (4);
            drawLabel (g, t (lang, "- - suggested", "- - ehdotus"), legend, colours::suggest.withAlpha (0.8f), juce::Justification::centredRight, 9.5f);
        }

        const int namesY = eq.getY() + headH + curveH;
        const int rowsTop = namesY + namesH;
        for (int b = 0; b < 4; ++b)
        {
            const int x = eq.getX() + b * colW;
            drawLabel (g, eqBandName (b, lang), { x, namesY, colW - 4, namesH }, colours::dim, juce::Justification::centredLeft, 10.5f);
            knobText (gain[(size_t) b], { x, rowsTop, colW, gainRowH }, knobGain, {});
            knobText (freq[(size_t) b], { x, rowsTop + gainRowH, colW, rowH }, knobSmall, {});
            if (b == 1 || b == 2)
                knobText (q[(size_t) b - 1], { x, rowsTop + gainRowH + rowH, colW, rowH }, knobSmall, {});
        }

        auto note = eq.withTrimmedTop (headH + curveH + namesH + gainRowH + 2 * rowH + 4);
        if (showSugg)
        {
            g.setColour (adv.eqTooMuch ? colours::warn : colours::soft);
            g.setFont (fonts::body (12.0f));
            g.drawFittedText (adv.eqNote, note, juce::Justification::topLeft, 3, 0.9f);
        }
    }

    // ---- limiter
    const auto lim = limiterArea();
    {
        auto head = lim.withHeight (headH).withTrimmedLeft (86);
        g.setColour (state.chainSettings.limiterOn ? colours::accent : colours::dim);
        g.setFont (fonts::body (12.0f));
        g.drawFittedText (state.chainSettings.limiterOn ? t (lang, "On", "Päällä") : t (lang, "Off", "Pois"), head, juce::Justification::centredLeft, 1);

        // gain reduction meter
        auto meter = lim.withTrimmedTop (headH).withHeight (curveH - 4);
        auto grRow = meter.removeFromTop (14);
        drawLabel (g, t (lang, "Gain reduction", "Vaimennus"), grRow, colours::dim, juce::Justification::centredLeft, 10.0f);
        g.setFont (fonts::num (10.5f));
        g.setColour (state.gainReduction > 6.0f ? colours::warn : colours::text);
        g.drawText (state.chainSettings.limiterOn ? fmt (lang, -state.gainReduction, 1) + " dB" : dash, grRow, juce::Justification::centredRight);
        auto bar = meter.removeFromTop (10).toFloat().reduced (0.0f, 1.0f);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (bar, 3.0f);
        if (state.chainSettings.limiterOn && state.gainReduction > 0.05f)
        {
            const float frac = juce::jlimit (0.0f, 1.0f, state.gainReduction / 12.0f);
            g.setColour (state.gainReduction > 6.0f ? colours::warn : colours::accent);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (3.0f, bar.getWidth() * frac)), 3.0f);
        }
        if (showSugg && adv.ready && adv.expectedReduction > 0.0f)
        {
            // where the suggested gain should put it
            const float x = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, adv.expectedReduction / 12.0f);
            g.setColour (colours::suggest);
            g.fillRect (x - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        }
        g.setFont (fonts::num (8.5f));
        g.setColour (colours::faint);
        g.drawText ("0", meter.removeFromTop (10), juce::Justification::centredLeft);
        g.drawText ("12 dB", meter.withY (meter.getY() - 10).withHeight (10), juce::Justification::centredRight);

        const int namesY = lim.getY() + headH + curveH;
        const int rowsTop = namesY + namesH;
        knobText (limGain, { lim.getX(), rowsTop, colW, gainRowH }, knobGain, t (lang, "Gain", "Gain"));
        knobText (ceiling, { lim.getX(), rowsTop + gainRowH, colW, rowH }, knobSmall, t (lang, "Ceiling", "Katto"));
        knobText (release, { lim.getX(), rowsTop + gainRowH + rowH, colW, rowH }, knobSmall, t (lang, "Release", "Release"));

        // what comes out, next to the target
        auto out = juce::Rectangle<int> (lim.getX() + colW + 4, namesY, lim.getWidth() - colW - 4, gainRowH + 2 * rowH + namesH);
        const auto& s = state.snap;
        const auto& p = state.profile;
        drawLabel (g, t (lang, "Out (3 s)", "Ulos (3 s)"), out.removeFromTop (14), colours::faint, juce::Justification::centredLeft, 10.0f);
        const float loud = s.shortTerm;
        const bool inRange = loud >= p.lufsMin && loud <= p.lufsMax;
        g.setColour (loud > -70.0f ? (inRange ? colours::good : colours::text) : colours::dim);
        g.setFont (fonts::numBold (17.0f));
        g.drawText (fmt (lang, loud > -70.0f ? loud : -200.0f, 1), out.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (colours::dim);
        g.setFont (fonts::body (11.0f));
        g.drawText ("LUFS, " + t (lang, "target ", "tavoite ") + fmt (lang, p.lufsMin, 0) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) + fmt (lang, p.lufsMax, 0),
                    out.removeFromTop (14), juce::Justification::centredLeft);
        out.removeFromTop (6);
        drawLabel (g, t (lang, "True peak (3 s)", "True peak (3 s)"), out.removeFromTop (14), colours::faint, juce::Justification::centredLeft, 10.0f);
        const bool over = s.truePeakRecent > state.chainSettings.ceiling + 0.1f;
        g.setColour (s.truePeakRecent > -70.0f ? (over ? colours::warn : colours::text) : colours::dim);
        g.setFont (fonts::num (13.0f));
        g.drawText (fmt (lang, s.truePeakRecent > -70.0f ? s.truePeakRecent : -200.0f, 1) + " dBTP", out.removeFromTop (18), juce::Justification::centredLeft);
        out.removeFromTop (4);
        g.setColour (colours::dim);
        g.setFont (fonts::body (11.0f));
        const float in = state.inputSnap.shortTerm;
        g.drawText (t (lang, "In: ", "Sisään: ") + fmt (lang, in > -70.0f ? in : -200.0f, 1) + " LUFS", out.removeFromTop (14), juce::Justification::centredLeft);

        auto note = lim.withTrimmedTop (headH + curveH + namesH + gainRowH + 2 * rowH + 4);
        if (showSugg)
        {
            g.setColour (adv.expectedReduction > 8.0f ? colours::warn : colours::soft);
            g.setFont (fonts::body (12.0f));
            g.drawFittedText (adv.limiterNote, note, juce::Justification::topLeft, 3, 0.9f);
        }
        else
        {
            g.setColour (colours::faint);
            g.setFont (fonts::body (12.0f));
            g.drawFittedText (t (lang, "You set every knob. Your ears decide.", "Sinä säädät jokaisen nupin. Korva ratkaisee."), note, juce::Justification::topLeft, 2);
        }
    }
}
} // namespace wwma::ui
