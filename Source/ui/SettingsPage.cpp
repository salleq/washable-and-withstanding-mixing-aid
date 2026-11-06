#include "SettingsPage.h"

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }

    void styleEditor (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, colours::raised);
        e.setColour (juce::TextEditor::textColourId, colours::text);
        e.setColour (juce::TextEditor::outlineColourId, colours::line);
        e.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        e.setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.3f));
        e.setColour (juce::CaretComponent::caretColourId, colours::accent);
        e.setFont (fonts::body (13.0f));
        e.setIndents (8, 5);
    }

    void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c = colours::soft, float size = 12.5f)
    {
        juce::AttributedString a;
        a.append (text, fonts::body (size), c);
        a.setLineSpacing (2.0f);
        a.setWordWrap (juce::AttributedString::byWord);
        a.draw (g, r.toFloat());
    }

    juce::Path gear (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float outer = r.getWidth() * 0.5f, inner = outer * 0.74f;
        constexpr int teeth = 8;
        for (int i = 0; i < teeth; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / teeth;
            const float step = juce::MathConstants<float>::twoPi / teeth;
            const float pts[] = { a - step * 0.5f, a - step * 0.2f, a - step * 0.12f, a + step * 0.12f, a + step * 0.2f };
            const float rad[] = { inner, inner, outer, outer, inner };
            for (int k = 0; k < 5; ++k)
            {
                const auto pt = c + juce::Point<float> (std::sin (pts[k]), -std::cos (pts[k])) * rad[k];
                if (i == 0 && k == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
        }
        p.closeSubPath();
        p.addEllipse (juce::Rectangle<float> (outer * 0.72f, outer * 0.72f).withCentre (c));
        p.setUsingNonZeroWinding (false);
        return p;
    }
}

// ---- the footer button -------------------------------------------------------------------------
void SettingsButton::setStream (bool on, int listeners)
{
    if (on == streaming && listeners == count)
        return;
    streaming = on;
    count = listeners;
    repaint();
}

void SettingsButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);
    auto r = getLocalBounds().toFloat();
    const auto icon = juce::Rectangle<float> (16.0f, 16.0f).withCentre (r.getCentre());
    // toggled: the button is filled with the accent colour, so the gear goes dark on it
    g.setColour (getToggleState() ? colours::background : highlighted ? colours::text : colours::soft);
    g.fillPath (gear (icon));
    if (streaming)
    {
        // a green dot, with how many listen
        const auto dot = juce::Rectangle<float> (count > 0 ? 15.0f : 9.0f, 9.0f + (count > 0 ? 3.0f : 0.0f))
                             .withPosition (r.getRight() - (count > 0 ? 17.0f : 11.0f), 2.0f);
        g.setColour (colours::good);
        g.fillRoundedRectangle (dot, dot.getHeight() * 0.5f);
        if (count > 0)
        {
            g.setColour (colours::background);
            g.setFont (fonts::numBold (10.0f));
            g.drawText (juce::String (juce::jmin (count, 9)), dot, juce::Justification::centred);
        }
    }
}

// ---- one link -----------------------------------------------------------------------------------
SettingsPage::LinkRow::LinkRow (stream::Link l, Lang lg) : link (std::move (l)), lang (lg)
{
    copyButton.setButtonText (upper (t (lang, "Copy", "Kopioi")));
    copyButton.getProperties().set ("primary", link.kind != stream::Link::Kind::thisComputer);
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (link.url);
        copyButton.setButtonText (upper (t (lang, "Copied", "Kopioitu")));
    };
    openButton.setButtonText (upper (t (lang, "Open", "Avaa")));
    openButton.setTooltip (t (lang, "Opens the player in this computer's browser.", "Avaa soittimen tämän koneen selaimessa."));
    openButton.onClick = [this] { juce::URL (link.url).launchInDefaultBrowser(); };
    addAndMakeVisible (copyButton);
    addAndMakeVisible (openButton);
}

void SettingsPage::LinkRow::resized()
{
    auto r = getLocalBounds().reduced (0, 4);
    openButton.setBounds (r.removeFromRight (60));
    r.removeFromRight (4);
    copyButton.setBounds (r.removeFromRight (82));
}

void SettingsPage::LinkRow::paint (juce::Graphics& g)
{
    g.setColour (colours::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    auto r = getLocalBounds().withRight (copyButton.getX() - 10);
    juce::String kind;
    juce::Colour col = colours::accent;
    switch (link.kind)
    {
        case stream::Link::Kind::ownAddress:   kind = t (lang, "Your address", "Oma osoite"); col = colours::good; break;
        case stream::Link::Kind::home:         kind = t (lang, "Home network", "Kotiverkko"); break;
        case stream::Link::Kind::vpn:          kind = t (lang, "Tailscale / VPN", "Tailscale / VPN"); col = colours::ref; break;
        case stream::Link::Kind::thisComputer: kind = t (lang, "This computer", "Tämä kone"); col = colours::dim; break;
    }
    g.setColour (col);
    g.setFont (fonts::labelBold (12.0f));
    g.drawText (upper (kind), r.removeFromLeft (112), juce::Justification::centredLeft);
    g.setColour (colours::text);
    g.setFont (fonts::num (12.5f));
    g.drawFittedText (link.url, r, juce::Justification::centredLeft, 1, 0.75f);
}

// ---- the page -----------------------------------------------------------------------------------
SettingsPage::SettingsPage (const ViewState& s, stream::Server& srv)
    : state (s), server (srv), settings (stream::loadSettings()), aiView (s)
{
    for (int i = 0; i < 3; ++i)
    {
        auto& b = tabButtons[(size_t) i];
        b.setClickingTogglesState (true);
        b.setRadioGroupId (43);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i] { if (tabButtons[(size_t) i].getToggleState()) setTab ((Tab) i); };
        addAndMakeVisible (b);
    }
    tabButtons[0].setToggleState (true, juce::dontSendNotification);
    for (auto* b : { &englishButton, &finnishButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (44);
        addChildComponent (b);
    }
    englishButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    finnishButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    englishButton.setButtonText ("ENGLISH");
    finnishButton.setButtonText ("SUOMI");
    englishButton.onClick = [this] { if (englishButton.getToggleState() && state.lang != Lang::english && onLanguageChanged) onLanguageChanged (Lang::english); };
    finnishButton.onClick = [this] { if (finnishButton.getToggleState() && state.lang != Lang::finnish && onLanguageChanged) onLanguageChanged (Lang::finnish); };
    addChildComponent (aiView);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &closeButton, &streamButton, &newLinkButton, &homeButton, &localButton,
                                                                          &setPasswordButton, &passwordToggle, &portEditor, &passwordEditor, &addressEditor })
        addAndMakeVisible (c);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    streamButton.getProperties().set ("primary", true);
    streamButton.onClick = [this] { toggleStream(); };

    newLinkButton.onClick = [this]
    {
        settings.token = stream::newToken();
        save();
        server.updateSecurity (settings);
        shownLinks.clear();
        rebuildLinks();
        setNote (t (state.lang, "New link made. The old links no longer work.", "Uusi linkki tehty. Vanhat linkit eivät enää toimi."), colours::accent);
    };

    homeButton.setClickingTogglesState (true);
    localButton.setClickingTogglesState (true);
    homeButton.setRadioGroupId (41);
    localButton.setRadioGroupId (41);
    homeButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    localButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    homeButton.onClick = [this] { if (homeButton.getToggleState() && settings.localOnly) { settings.localOnly = false; save(); syncControls(); repaint(); } };
    localButton.onClick = [this] { if (localButton.getToggleState() && ! settings.localOnly) { settings.localOnly = true; save(); syncControls(); repaint(); } };

    styleEditor (portEditor);
    portEditor.setInputRestrictions (5, "0123456789");
    portEditor.setJustification (juce::Justification::centredLeft);
    auto portDone = [this]
    {
        const int p = portEditor.getText().getIntValue();
        if (p >= 1024 && p <= 65535)
        {
            if (p != settings.port)
            {
                settings.port = p;
                save();
            }
        }
        else
            setNote (t (state.lang, "The port is a number from 1024 to 65535.", "Portti on luku väliltä 1024-65535."), colours::warn);
        syncControls();
    };
    portEditor.onReturnKey = portDone;
    portEditor.onFocusLost = portDone;

    // the user's own address (a reverse proxy): shown first in the links, WWMA adds ?k=...
    styleEditor (addressEditor);
    addressEditor.setFont (fonts::num (12.5f));
    auto addressDone = [this]
    {
        const auto typed = addressEditor.getText().trim();
        const auto url = stream::normalisePublicUrl (typed);
        if (typed.isNotEmpty() && url.isEmpty())
        {
            setNote (t (state.lang, "The address is like https://mydaw.example.com/", "Osoite on muotoa https://mydaw.example.com/"), colours::warn);
            return;
        }
        if (url != settings.publicUrl)
        {
            settings.publicUrl = url;
            save();
            server.updateSecurity (settings);
            rebuildLinks();
            if (url.isNotEmpty() && url.startsWith ("http://"))
                setNote (t (state.lang, "Address saved. It is http, not https: the password and audio go unencrypted.",
                                        "Osoite tallennettu. Se on http eikä https: salasana ja ääni kulkevat salaamatta."), colours::warn);
            else
                setNote (url.isEmpty() ? t (state.lang, "Address removed.", "Osoite poistettu.")
                                       : t (state.lang, "Address saved. Its link is first in the list.", "Osoite tallennettu. Sen linkki on listassa ensimmäisenä."), colours::accent);
        }
        syncControls();
    };
    addressEditor.onReturnKey = addressDone;
    addressEditor.onFocusLost = addressDone;

    styleEditor (passwordEditor);
    passwordEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    auto setPassword = [this]
    {
        const auto pw = passwordEditor.getText();
        if (pw.length() < 8)
        {
            setNote (t (state.lang, "Use at least 8 characters.", "Käytä vähintään 8 merkkiä."), colours::warn);
            return;
        }
        settings.salt = stream::newSalt();
        settings.hash = stream::hashPassword (settings.salt, pw);
        settings.passwordOn = true;
        passwordEditor.clear();
        save();
        server.updateSecurity (settings);
        setNote (t (state.lang, "Password set. The browser asks for it (any user name).", "Salasana asetettu. Selain kysyy sitä (käyttäjänimi on vapaa)."), colours::good);
        syncControls();
    };
    passwordEditor.onReturnKey = setPassword;
    setPasswordButton.onClick = setPassword;
    passwordToggle.onClick = [this]
    {
        const bool on = passwordToggle.getToggleState();
        if (on && ! settings.hasPassword())
        {
            passwordToggle.setToggleState (false, juce::dontSendNotification);
            passwordEditor.grabKeyboardFocus();
            setNote (t (state.lang, "Type a password and press Set.", "Kirjoita salasana ja paina Aseta."), colours::warn);
            return;
        }
        settings.passwordOn = on;
        save();
        server.updateSecurity (settings);
        syncControls();
    };

    updateTexts();
}

void SettingsPage::save() { stream::saveSettings (settings); }

std::vector<juce::Component*> SettingsPage::streamControls()
{
    std::vector<juce::Component*> v { &streamButton, &newLinkButton, &homeButton, &localButton, &setPasswordButton, &passwordToggle,
                                      &portEditor, &passwordEditor, &addressEditor };
    for (auto& r : linkRows)
        v.push_back (r.get());
    return v;
}

void SettingsPage::setTab (Tab t)
{
    tab = t;
    tabButtons[(size_t) t].setToggleState (true, juce::dontSendNotification);
    note = {};
    resized();
    repaint();
}

void SettingsPage::setNote (const juce::String& text, juce::Colour c)
{
    note = text;
    noteColour = c;
    repaint();
}

void SettingsPage::toggleStream()
{
    if (server.isRunning())
    {
        server.stop();
        setNote (t (state.lang, "Stream stopped. Nothing is shared.", "Stream pysäytetty. Mitään ei jaeta."), colours::dim);
    }
    else
    {
        if (server.start (settings))
            setNote ({}, colours::dim);
    }
    refresh();
    if (onStreamChanged)
        onStreamChanged();
}

void SettingsPage::updateTexts()
{
    const auto lang = state.lang;
    closeButton.setButtonText (upper (t (lang, "Close", "Sulje")));
    tabButtons[0].setButtonText (upper (t (lang, "General", "Yleiset")));
    tabButtons[1].setButtonText (upper (t (lang, "AI assistant", "Tekoälyavustaja")));
    tabButtons[2].setButtonText (upper (t (lang, "Listen anywhere", "Kuuntele missä vain")));
    englishButton.setToggleState (lang == Lang::english, juce::dontSendNotification);
    finnishButton.setToggleState (lang == Lang::finnish, juce::dontSendNotification);
    aiView.updateTexts();
    newLinkButton.setButtonText (upper (t (lang, "New link", "Uusi linkki")));
    newLinkButton.setTooltip (t (lang, "Makes a new link. Everyone with an old link loses access (also those listening now, when they reconnect).",
                                       "Tekee uuden linkin. Vanhalla linkillä ei enää pääse kuuntelemaan (eivät myöskään nyt kuuntelevat, kun yhteys avataan uudelleen)."));
    homeButton.setButtonText (upper (t (lang, "Home network", "Kotiverkko")));
    localButton.setButtonText (upper (t (lang, "This computer only", "Vain tämä kone")));
    homeButton.setTooltip (t (lang, "Phones and computers in the same network (Wi-Fi), and over Tailscale or another VPN.",
                                    "Puhelimet ja koneet samassa verkossa (Wi-Fi) sekä Tailscalen tai muun VPN:n kautta."));
    localButton.setTooltip (t (lang, "Only programs on this computer can connect: for a reverse proxy (like Traefik) that shares it with HTTPS and a password.",
                                     "Vain tämän koneen ohjelmat pääsevät kiinni: käänteiselle välityspalvelimelle (esim. Traefik), joka jakaa sen HTTPS:llä ja salasanalla."));
    passwordToggle.setButtonText (t (lang, "Ask for a password", "Kysy salasana"));
    setPasswordButton.setButtonText (upper (t (lang, "Set", "Aseta")));
    addressEditor.setTextToShowWhenEmpty ("https://mydaw.example.com/", colours::faint);
    addressEditor.setTooltip (t (lang, "Your own address, if a reverse proxy (like Traefik) shares WWMA under it. WWMA adds the link's secret part (?k=...) after it.",
                                       "Oma osoitteesi, jos välityspalvelin (esim. Traefik) jakaa WWMA:n sen alla. WWMA lisää perään linkin salaisen osan (?k=...)."));
    passwordEditor.setTextToShowWhenEmpty (t (lang, "New password", "Uusi salasana"), colours::faint);
    rebuildLinks();
    syncControls();
    repaint();
}

void SettingsPage::syncControls()
{
    const bool running = server.isRunning();
    streamButton.setButtonText (upper (running ? t (state.lang, "Stop stream", "Pysäytä stream") : t (state.lang, "Start stream", "Käynnistä stream")));
    streamButton.getProperties().set ("primary", ! running);
    streamButton.repaint();
    if (! portEditor.hasKeyboardFocus (true))
        portEditor.setText (juce::String (settings.port), false);
    if (! addressEditor.hasKeyboardFocus (true))
        addressEditor.setText (settings.publicUrl, false);
    // where it listens is chosen before it starts
    portEditor.setEnabled (! running);
    homeButton.setEnabled (! running);
    localButton.setEnabled (! running);
    portEditor.setTooltip (running ? t (state.lang, "Stop the stream to change the port.", "Pysäytä stream vaihtaaksesi porttia.") : juce::String());
    homeButton.setToggleState (! settings.localOnly, juce::dontSendNotification);
    localButton.setToggleState (settings.localOnly, juce::dontSendNotification);
    passwordToggle.setToggleState (settings.passwordOn && settings.hasPassword(), juce::dontSendNotification);
    newLinkButton.setEnabled (true);
}

void SettingsPage::rebuildLinks()
{
    const bool running = server.isRunning();
    juce::String key;
    std::vector<stream::Link> links;
    if (running)
        links = server.getLinks();
    for (const auto& l : links)
        key << l.url << "\n";
    key << (int) state.lang;
    if (key == shownLinks)
        return;
    shownLinks = key;
    for (auto& r : linkRows)
        removeChildComponent (r.get());
    linkRows.clear();
    for (auto& l : links)
    {
        auto row = std::make_unique<LinkRow> (std::move (l), state.lang);
        addChildComponent (*row);
        linkRows.push_back (std::move (row));
    }
    resized();
}

void SettingsPage::refresh()
{
    const auto st = server.getState();
    const int n = server.getListeners();
    const bool changed = st != shownState || n != shownListeners;
    if (st != shownState)
    {
        shownState = st;
        syncControls();
    }
    shownListeners = n;
    rebuildLinks();
    if (changed)
        repaint();
}

juce::String SettingsPage::statusText (juce::Colour& c) const
{
    const auto lang = state.lang;
    const auto port = juce::String (settings.port);
    switch (server.getState())
    {
        case stream::Server::State::running:
        {
            c = colours::good;
            const int n = server.getListeners();
            const auto rate = juce::String (server.getStreamRate() / 1000.0, 1) + " kHz";
            return t (lang, "On", "Päällä") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + rate + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                   + (n == 0 ? t (lang, "nobody listening", "ei kuuntelijoita")
                             : juce::String (n) + " " + (n == 1 ? t (lang, "listening", "kuuntelija") : t (lang, "listening", "kuuntelijaa")));
        }
        case stream::Server::State::portInUse:
            c = colours::warn;
            return t (lang, "Port ", "Portti ") + port + t (lang, " is already in use, maybe by another WWMA. Stop that one, or choose another port.",
                                                             " on jo käytössä, ehkä toisella WWMA:lla. Pysäytä se tai valitse toinen portti.");
        case stream::Server::State::blocked:
            c = colours::bad;
            return t (lang, "This DAW or the system does not let WWMA open a network port. Try the VST3 or CLAP version, or the standalone app.",
                            "Tämä DAW tai järjestelmä ei anna WWMA:n avata verkkoporttia. Kokeile VST3- tai CLAP-versiota tai standalone-sovellusta.");
        case stream::Server::State::off: break;
    }
    c = colours::dim;
    return t (lang, "Off. Nothing is shared.", "Pois päältä. Mitään ei jaeta.");
}

void SettingsPage::resized()
{
    auto r = getLocalBounds().reduced (18, 16);
    // the tabs, and Close top right (the title is in the top bar)
    {
        auto head = r.removeFromTop (30);
        closeButton.setBounds (head.removeFromRight (80));
        for (auto& b : tabButtons)
            b.setBounds (head.removeFromLeft (180));
        r.removeFromTop (14);
        dividerY = r.getY() - 7;
        r.removeFromTop (4);
    }
    for (auto* c : streamControls())
        c->setVisible (tab == Tab::stream);
    aiView.setVisible (tab == Tab::ai);
    englishButton.setVisible (tab == Tab::general);
    finnishButton.setVisible (tab == Tab::general);
    sectionArea = r.withHeight (20);

    // ---- general
    {
        auto g = r.withWidth (juce::jmin (r.getWidth(), 640));
        g.removeFromTop (30);                // section label
        auto langRow = g.removeFromTop (30);
        languageLabel = langRow.removeFromLeft (110);
        englishButton.setBounds (langRow.removeFromLeft (120));
        finnishButton.setBounds (langRow.removeFromLeft (120));
        g.removeFromTop (6);
        languageHelp = g.removeFromTop (40).withTrimmedLeft (110);
    }

    // ---- AI
    aiView.setBounds (r.withTrimmedTop (30));

    optionsArea = r.removeFromRight (440);
    r.removeFromRight (30);
    streamArea = r;

    // ---- left: the stream and its links
    auto s = streamArea;
    s.removeFromTop (22);                // section label
    introArea = s.removeFromTop (58);
    s.removeFromTop (6);
    auto row = s.removeFromTop (36);
    streamButton.setBounds (row.removeFromLeft (190));
    row.removeFromLeft (14);
    statusArea = row;
    s.removeFromTop (18);
    auto linksHead = s.removeFromTop (24);
    newLinkButton.setBounds (linksHead.removeFromRight (110));
    s.removeFromTop (4);
    linksArea = s;
    auto lr = linksArea;
    for (auto& rowC : linkRows)
        rowC->setBounds (lr.removeFromTop (38));

    // ---- right: options
    auto o = optionsArea;
    o.removeFromTop (22);                // section label
    auto portRow = o.removeFromTop (30);
    portLabel = portRow.removeFromLeft (110);
    portEditor.setBounds (portRow.removeFromLeft (90));
    o.removeFromTop (12);
    auto listenRow = o.removeFromTop (30);
    listenLabel = listenRow.removeFromLeft (110);
    homeButton.setBounds (listenRow.removeFromLeft (130));
    localButton.setBounds (listenRow.removeFromLeft (150));
    o.removeFromTop (4);
    listenHelp = o.removeFromTop (54).withTrimmedLeft (110);
    o.removeFromTop (6);
    auto addressRow = o.removeFromTop (30);
    addressLabel = addressRow.removeFromLeft (110);
    addressEditor.setBounds (addressRow);
    o.removeFromTop (2);
    addressHelp = o.removeFromTop (34).withTrimmedLeft (110);
    o.removeFromTop (8);
    auto pwRow = o.removeFromTop (28);
    passwordLabel = pwRow.removeFromLeft (110);
    passwordToggle.setBounds (pwRow);
    o.removeFromTop (6);
    auto pwEdit = o.removeFromTop (30).withTrimmedLeft (110);
    setPasswordButton.setBounds (pwEdit.removeFromRight (80));
    pwEdit.removeFromRight (6);
    passwordEditor.setBounds (pwEdit);
    o.removeFromTop (2);
    passwordStatus = o.removeFromTop (22).withTrimmedLeft (110);
    o.removeFromTop (14);
    helpArea = o;
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    g.fillAll (colours::background);
    drawBox (g, getLocalBounds().toFloat().reduced (4.0f));
    // the title is in the top bar while this page is open
    g.setColour (colours::line);
    g.drawHorizontalLine (dividerY, 18.0f, (float) getWidth() - 18.0f);

    if (tab == Tab::general)
    {
        drawLabel (g, upper (t (lang, "Language", "Kieli")), sectionArea, colours::accent, juce::Justification::centredLeft, 13.0f);
        drawLabel (g, upper (t (lang, "Texts", "Tekstit")), languageLabel);
        drawText (g, t (lang, "The language of WWMA's texts, the reports and the AI's answers. Kept with the DAW project.",
                              "WWMA:n tekstien, raporttien ja tekoälyn vastausten kieli. Tallentuu DAW-projektin mukana."),
                  languageHelp, colours::dim, 12.0f);
        return;
    }
    if (tab == Tab::ai)
    {
        drawLabel (g, upper (t (lang, "AI assistant", "Tekoälyavustaja")), sectionArea, colours::accent, juce::Justification::centredLeft, 13.0f);
        return;
    }
    // ---- left
    drawLabel (g, upper (t (lang, "Listen anywhere", "Kuuntele missä vain")), streamArea.withHeight (20), colours::accent, juce::Justification::centredLeft, 13.0f);
    drawText (g, t (lang, "Hear WWMA's output on a phone, a tablet or another computer: open a link below in its browser. "
                          "Lossless (16-bit), a quarter of a second or so behind. It is exactly what WWMA sends out, A/B and BEFORE included. "
                          "Off until you start it here; it keeps running with the window closed and stops when WWMA is removed.",
                          "Kuuntele WWMA:n ääntä puhelimella, tabletilla tai toisella koneella: avaa alla oleva linkki sen selaimessa. "
                          "Häviötön (16-bit), noin neljännessekunnin jäljessä. Se on täsmälleen sama ääni, jonka WWMA lähettää eteenpäin, A/B ja ENNEN mukaan lukien. "
                          "Pois päältä, kunnes käynnistät sen tästä; soi myös ikkunan ollessa kiinni ja pysähtyy, kun WWMA poistetaan."),
              introArea);

    juce::Colour sc;
    const auto st = statusText (sc);
    g.setColour (sc.withAlpha (0.25f));
    g.fillEllipse ((float) statusArea.getX() - 3.0f, (float) statusArea.getCentreY() - 7.0f, 14.0f, 14.0f);
    g.setColour (sc);
    g.fillEllipse ((float) statusArea.getX(), (float) statusArea.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setFont (sc == colours::dim ? fonts::body (13.0f) : fonts::bodyBold (13.0f));
    g.drawFittedText (st, statusArea.withTrimmedLeft (18), juce::Justification::centredLeft, 2, 0.85f);

    drawLabel (g, upper (t (lang, "Links", "Linkit")), newLinkButton.getBounds().withX (streamArea.getX()).withRight (newLinkButton.getX() - 8));
    if (linkRows.empty())
        drawText (g, t (lang, "The links appear here when the stream is on. Each one has a secret part (?k=...): only people with the link can listen.",
                              "Linkit näkyvät tässä, kun stream on päällä. Jokaisessa on salainen osa (?k=...): vain linkin saaneet voivat kuunnella."),
                  linksArea.withHeight (44), colours::dim);
    else
    {
        auto below = linksArea.withTrimmedTop (38 * (int) linkRows.size() + 8);
        if (settings.localOnly)
            drawText (g, t (lang, "Only this computer can connect. Point your reverse proxy (Traefik) to http://127.0.0.1:", "Vain tämä kone pääsee kiinni. Ohjaa välityspalvelin (Traefik) osoitteeseen http://127.0.0.1:")
                             + juce::String (settings.port)
                             + (settings.publicUrl.isNotEmpty() ? t (lang, ". Share the link under your own address.", ". Jaa oman osoitteesi linkki.")
                                                                : t (lang, ". The link to share is your own address + /?k=", ". Jaettava linkki on oma osoitteesi + /?k=") + settings.token
                                                                  + t (lang, " (or type the address in the options).", " (tai kirjoita osoite asetuksiin).")),
                      below.withHeight (48), colours::soft);
        else
            drawText (g, t (lang, "Open a home network link on a phone in the same Wi-Fi. A Tailscale link works wherever the phone is, with Tailscale on in it too.",
                                  "Avaa kotiverkon linkki puhelimella samassa Wi-Fissä. Tailscale-linkki toimii missä vain, kun Tailscale on päällä myös puhelimessa."),
                      below.withHeight (48), colours::dim);
    }
    if (note.isNotEmpty())
        drawText (g, note, linksArea.withTrimmedTop (linksArea.getHeight() - 24), noteColour, 13.0f);

    // ---- right
    drawLabel (g, upper (t (lang, "Stream options", "Streamin asetukset")), optionsArea.withHeight (20), colours::accent, juce::Justification::centredLeft, 13.0f);
    drawLabel (g, upper (t (lang, "Port", "Portti")), portLabel);
    drawLabel (g, upper (t (lang, "Listen on", "Kuuntelee")), listenLabel);
    drawText (g, settings.localOnly ? t (lang, "For a reverse proxy on this computer, e.g. Traefik with HTTPS and a password, to share outside your home.",
                                               "Saman koneen välityspalvelimelle, esim. Traefik HTTPS:llä ja salasanalla, kodin ulkopuolelle jakamista varten.")
                                    : t (lang, "Your home network and VPNs like Tailscale. Windows and macOS may ask once whether the DAW may accept connections: allow it for private networks.",
                                               "Kotiverkko ja VPN:t kuten Tailscale. Windows ja macOS voivat kysyä kerran, saako DAW ottaa vastaan yhteyksiä: salli yksityisissä verkoissa."),
              listenHelp, colours::dim, 11.5f);
    drawLabel (g, upper (t (lang, "Your address", "Oma osoite")), addressLabel);
    drawText (g, t (lang, "Optional: where your reverse proxy shares WWMA. Its link is shown first, with ?k=... added.",
                          "Valinnainen: osoite, jossa välityspalvelimesi jakaa WWMA:n. Sen linkki näkyy ensimmäisenä, perässä ?k=..."),
              addressHelp, colours::dim, 11.5f);
    drawLabel (g, upper (t (lang, "Password", "Salasana")), passwordLabel);
    {
        const bool on = settings.passwordOn && settings.hasPassword();
        g.setColour (on ? colours::good : colours::dim);
        g.setFont (fonts::body (11.5f));
        g.drawFittedText (on ? t (lang, "On: the browser asks for it (any user name).", "Päällä: selain kysyy sitä (käyttäjänimi on vapaa).")
                             : settings.hasPassword() ? t (lang, "A password is set, but not asked.", "Salasana on asetettu, mutta sitä ei kysytä.")
                                                      : t (lang, "No password yet.", "Salasanaa ei ole vielä."),
                          passwordStatus, juce::Justification::centredLeft, 1, 0.85f);
    }

    drawLabel (g, upper (t (lang, "Outside your home", "Kodin ulkopuolelta")), helpArea.withHeight (20));
    drawText (g, t (lang, "Easiest and safest: Tailscale (free) on this computer and on the phone, then use the Tailscale link. Nothing is opened to the internet.\n\n"
                          "To share a link with anyone: run a reverse proxy like Traefik with HTTPS (Let's Encrypt), choose \"This computer only\" and turn on the password. "
                          "WWMA itself has no HTTPS, so never open its port straight to the internet: the password and the audio would travel unencrypted. "
                          "The manual has a ready Traefik example.",
                          "Helpoin ja turvallisin: Tailscale (ilmainen) tälle koneelle ja puhelimeen, sitten Tailscale-linkki. Mitään ei avata internetiin.\n\n"
                          "Linkin jakaminen kenelle tahansa: käytä välityspalvelinta kuten Traefik HTTPS:llä (Let's Encrypt), valitse \"Vain tämä kone\" ja kytke salasana päälle. "
                          "WWMA:ssa itsessään ei ole HTTPS:ää, joten älä koskaan avaa sen porttia suoraan internetiin: salasana ja ääni kulkisivat salaamatta. "
                          "Käyttöohjeessa on valmis Traefik-esimerkki."),
              helpArea.withTrimmedTop (24), colours::soft, 12.0f);
}
} // namespace wwma::ui
