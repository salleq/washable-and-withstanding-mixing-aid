#include "VersionsPanel.h"

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }
    const juce::String dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    void drawPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }

    juce::String sessionDetail (const store::SessionInfo& s, Lang lang)
    {
        juce::StringArray parts;
        parts.add (whenText (s.lastUsed, lang));
        if (! s.references.isEmpty())
            parts.add (t (lang, "refs: ", "referenssit: ") + s.references.joinIntoString (", "));
        if (s.versions > 0)
            parts.add (juce::String (s.versions) + t (lang, " before versions", " ennen-versiota"));
        parts.add (store::sizeText (s.bytes));
        return parts.joinIntoString (dot);
    }

    juce::String sessionTitle (const store::SessionInfo& s, Lang lang)
    {
        return s.name.isNotEmpty() ? s.name : t (lang, "Session ", "Istunto ") + s.id.substring (0, 8);
    }
}

juce::String whenText (juce::Time time, Lang lang)
{
    const auto now = juce::Time::getCurrentTime();
    const auto clock = time.formatted ("%H:%M");
    auto sameDay = [] (juce::Time a, juce::Time b) { return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear(); };
    if (sameDay (time, now))
        return t (lang, "today ", "tänään ") + clock;
    if (sameDay (time, now - juce::RelativeTime::days (1)))
        return t (lang, "yesterday ", "eilen ") + clock;
    return (lang == Lang::finnish ? juce::String (time.getDayOfMonth()) + "." + juce::String (time.getMonth() + 1) + ". "
                                  : time.getMonthName (true) + " " + juce::String (time.getDayOfMonth()) + " ")
           + clock;
}

juce::String lengthText (double seconds)
{
    const int s = (int) std::round (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

// =====================================================================================
ListRow::ListRow (juce::String titleText, juce::String detailText, std::vector<Action> acts, bool isHeading)
    : title (std::move (titleText)), detail (std::move (detailText)), heading (isHeading), actions (std::move (acts))
{
    for (auto& a : actions)
    {
        auto b = std::make_unique<juce::TextButton> (upper (a.label));
        b->onClick = a.onClick;
        b->setTooltip (a.tooltip);
        if (a.primary)
            b->getProperties().set ("primary", true);
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    }
}

void ListRow::resized()
{
    auto r = getLocalBounds().reduced (0, 4);
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    {
        const int w = juce::jmax (54, juce::GlyphArrangement::getStringWidthInt (fonts::label (12.0f), (*it)->getButtonText()) + 18);
        (*it)->setBounds (r.removeFromRight (w));
        r.removeFromRight (4);
    }
}

void ListRow::paint (juce::Graphics& g)
{
    int right = getWidth();
    for (const auto& b : buttons)
        right = juce::jmin (right, b->getX());
    auto r = getLocalBounds().withRight (right - 8);
    if (heading)
    {
        g.setColour (colours::dim);
        g.setFont (fonts::labelBold (11.5f));
        g.drawText (upper (title), r.withTrimmedTop (8), juce::Justification::centredLeft);
        return;
    }
    g.setColour (colours::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.setColour (colours::text);
    g.setFont (fonts::bodyBold (12.5f));
    const int titleW = juce::jmin (r.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 10);
    g.drawFittedText (title, r.removeFromLeft (titleW), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (colours::dim);
    g.setFont (fonts::body (11.5f));
    g.drawFittedText (detail, r, juce::Justification::centredLeft, 2, 0.8f);
}

// =====================================================================================
SessionOffer::SessionOffer()
{
    freshButton.onClick = [this] { if (onFresh) onFresh(); };
    addAndMakeVisible (freshButton);
}

juce::Rectangle<int> SessionOffer::card() const
{
    const int h = 150 + (int) rows.size() * 40;
    return getLocalBounds().withSizeKeepingCentre (600, h);
}

void SessionOffer::show (const std::vector<store::SessionInfo>& sessions, Lang l)
{
    lang = l;
    rows.clear();
    for (size_t i = 0; i < sessions.size() && i < 4; ++i)
    {
        const auto folder = sessions[i].folder;
        rows.push_back (std::make_unique<ListRow> (sessionTitle (sessions[i], lang), sessionDetail (sessions[i], lang),
            std::vector<ListRow::Action> { { t (lang, "Continue", "Jatka"), [this, folder] { if (onContinue) onContinue (folder); }, i == 0 } }));
        addAndMakeVisible (*rows.back());
    }
    freshButton.setButtonText (upper (t (lang, "Start fresh", "Aloita alusta")));
    resized();
    repaint();
}

void SessionOffer::resized()
{
    auto r = card().reduced (18, 14);
    r.removeFromTop (78);
    for (auto& row : rows)
        row->setBounds (r.removeFromTop (40));
    r.removeFromTop (12);
    freshButton.setBounds (r.removeFromTop (28).removeFromRight (150));
}

void SessionOffer::paint (juce::Graphics& g)
{
    g.fillAll (colours::background.withAlpha (0.82f));
    const auto c = card();
    drawPanel (g, c.toFloat());
    auto r = c.reduced (18, 14);
    g.setColour (colours::accent);
    g.setFont (fonts::bodyBold (16.0f));
    g.drawText (t (lang, "Continue an earlier session?", "Jatketaanko aiemmasta istunnosta?"), r.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (colours::soft);
    g.setFont (fonts::body (12.5f));
    g.drawFittedText (t (lang, "This WWMA was just added, so it starts empty. WWMA keeps its sessions in its own folder: pick one to get back its references, loops, snapshots and before versions. Nothing in your DAW project is touched either way.",
                               "Tämä WWMA lisättiin juuri, joten se alkaa tyhjänä. WWMA pitää istuntonsa omassa kansiossaan: valitse yksi, niin referenssit, loopit, snapshotit ja ennen-versiot palaavat. DAW-projektiisi ei kosketa kummassakaan tapauksessa."),
                      r.removeFromTop (50), juce::Justification::topLeft, 3, 0.9f);
}

// =====================================================================================
VersionsPanel::VersionsPanel()
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    folderButton.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
    removeAllButton.onClick = [this] { if (onRemoveAll) onRemoveAll(); };
    renameButton.onClick = [this] { if (onRenameSession) onRenameSession(); };
    for (auto* b : { &closeButton, &folderButton, &removeAllButton, &renameButton })
        addAndMakeVisible (b);
}

void VersionsPanel::setData (Data d)
{
    data = std::move (d);
    const auto lang = data.lang;
    closeButton.setButtonText (upper (t (lang, "Close", "Sulje")));
    folderButton.setButtonText (upper (t (lang, "Open folder", "Avaa kansio")));
    folderButton.setTooltip (data.sessionFolder.getFullPathName());
    removeAllButton.setButtonText (upper (t (lang, "Remove all versions", "Poista kaikki versiot")));
    removeAllButton.setTooltip (t (lang, "Moves every before version of this session to the trash.",
                                         "Siirtää tämän istunnon kaikki ennen-versiot roskakoriin."));
    removeAllButton.setEnabled (! data.versions.empty());
    renameButton.setButtonText (upper (t (lang, "Rename", "Nimeä")));
    renameButton.setTooltip (t (lang, "Name this session, so you know it in the list later.",
                                      "Nimeä tämä istunto, jotta tunnistat sen myöhemmin listasta."));
    rebuild();
    repaint();
}

void VersionsPanel::rebuild()
{
    const auto lang = data.lang;
    rows.clear();
    auto heading = [&] (const juce::String& text)
    {
        rows.push_back (std::make_unique<ListRow> (text, juce::String(), std::vector<ListRow::Action> {}, true));
    };

    heading (t (lang, "Before versions of this session", "Tämän istunnon ennen-versiot"));
    if (data.versions.empty())
        rows.push_back (std::make_unique<ListRow> (t (lang, "None saved yet", "Ei vielä tallennettuja"),
                                                   t (lang, "Every snapshot saves the before take here (24-bit FLAC).",
                                                            "Jokainen snapshot tallentaa ennen-äänen tänne (24-bit FLAC)."),
                                                   std::vector<ListRow::Action> {}));
    for (const auto& v : data.versions)
    {
        juce::StringArray parts;
        parts.add (lengthText (v.coveredSeconds) + t (lang, " recorded", " nauhoitettu"));
        if (v.score >= 0.0f)
            parts.add (t (lang, "snapshot", "snapshot"));
        parts.add (store::sizeText (v.bytes));
        const auto title = (v.named ? v.name + dot : juce::String()) + whenText (v.time, lang);
        rows.push_back (std::make_unique<ListRow> (title, parts.joinIntoString (dot), std::vector<ListRow::Action> {
            { t (lang, "Use", "Käytä"), [this, v] { if (onUse) onUse (v); }, true,
              t (lang, "Load this as BEFORE: the BEFORE button then plays it.", "Lataa tämä ENNEN-ääneksi: ENNEN-nappi soittaa sitä.") },
            { t (lang, "Name", "Nimeä"), [this, v] { if (onRename) onRename (v); }, false,
              t (lang, "A named version is never removed automatically.", "Nimettyä versiota ei koskaan poisteta automaattisesti.") },
            { t (lang, "Remove", "Poista"), [this, v] { if (onRemove) onRemove (v); }, false,
              t (lang, "To the trash.", "Roskakoriin.") } }));
    }

    if (! data.others.empty())
    {
        heading (t (lang, "Other sessions in WWMA's folder", "Muut istunnot WWMA:n kansiossa"));
        for (const auto& s : data.others)
            rows.push_back (std::make_unique<ListRow> (sessionTitle (s, lang), sessionDetail (s, lang), std::vector<ListRow::Action> {
                { t (lang, "Continue", "Jatka"), [this, s] { if (onContinueSession) onContinueSession (s); }, false,
                  t (lang, "Replace this WWMA's settings with that session's.", "Korvaa tämän WWMA:n asetukset sen istunnon asetuksilla.") },
                { t (lang, "Remove", "Poista"), [this, s] { if (onRemoveSession) onRemoveSession (s); }, false,
                  t (lang, "The whole session, versions included, to the trash.", "Koko istunto versioineen roskakoriin.") } }));
    }

    int y = 0;
    const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    for (auto& r : rows)
    {
        list.addAndMakeVisible (*r);
        r->setBounds (0, y, w, 34);
        y += 34;
    }
    list.setSize (w, y);
}

void VersionsPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto head = r.removeFromTop (22);
    closeButton.setBounds (head.removeFromRight (70));
    head.removeFromRight (6);
    renameButton.setBounds (head.removeFromRight (70));
    auto foot = r.removeFromBottom (26);
    removeAllButton.setBounds (foot.removeFromRight (170));
    foot.removeFromRight (6);
    folderButton.setBounds (foot.removeFromRight (110));
    r.removeFromBottom (6);
    r.removeFromTop (22);
    viewport.setBounds (r);
    rebuild();
}

void VersionsPanel::paint (juce::Graphics& g)
{
    const auto lang = data.lang;
    drawPanel (g, getLocalBounds().toFloat());
    auto r = getLocalBounds().reduced (12, 10);
    auto head = r.removeFromTop (22).withTrimmedRight (154);
    g.setColour (colours::dim);
    g.setFont (fonts::labelBold (11.5f));
    g.drawText (upper (t (lang, "Versions", "Versiot")) + dot + data.sessionName, head, juce::Justification::centredLeft);

    auto info = r.removeFromTop (22);
    g.setColour (data.busy ? colours::accent : colours::soft);
    g.setFont (fonts::body (11.5f));
    const auto now = data.busy ? t (lang, "Saving or loading in the background...", "Tallennan tai lataan taustalla...")
                   : data.beforeSeconds > 0 ? t (lang, "BEFORE now holds ", "ENNEN sisältää nyt ") + lengthText (data.beforeSeconds) + t (lang, " of the song.", " biisistä.")
                                            : t (lang, "BEFORE is empty: save a snapshot after playing, or use a saved version.",
                                                       "ENNEN on tyhjä: tallenna snapshot soittamisen jälkeen tai käytä tallennettua versiota.");
    g.drawFittedText (now, info, juce::Justification::centredLeft, 1, 0.85f);

    auto foot = getLocalBounds().reduced (12, 10).removeFromBottom (26).withTrimmedRight (292);
    g.setColour (colours::dim);
    g.setFont (fonts::body (11.0f));
    g.drawFittedText (t (lang, "On disk: ", "Levyllä: ") + store::sizeText (data.totalBytes)
                          + t (lang, ". Removed files go to the trash.", ". Poistetut menevät roskakoriin."),
                      foot, juce::Justification::centredLeft, 2, 0.85f);
}
} // namespace wwma::ui
