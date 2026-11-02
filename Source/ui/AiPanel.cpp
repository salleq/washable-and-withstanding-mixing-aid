#include "AiPanel.h"

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }

    constexpr int rowH = 26, labelW = 92, cardH = 48;

    void styleEditor (juce::TextEditor& e, bool multi)
    {
        e.setMultiLine (multi, true);
        e.setReturnKeyStartsNewLine (multi);
        e.setScrollbarsShown (multi);
        e.setColour (juce::TextEditor::backgroundColourId, colours::raised);
        e.setColour (juce::TextEditor::textColourId, colours::text);
        e.setColour (juce::TextEditor::outlineColourId, colours::line);
        e.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        e.setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.3f));
        e.setColour (juce::CaretComponent::caretColourId, colours::accent);
        e.setFont (fonts::body (multi ? 13.5f : 13.0f));
        e.setIndents (8, multi ? 6 : 4);
    }

    juce::String modeText (ai::Provider p, ai::Mode m, Lang lang)
    {
        const auto name = ai::providerName (p);
        switch (m)
        {
            case ai::Mode::browser:
                return t (lang, "WWMA saves the report and opens ", "WWMA tallentaa raportin ja avaa ") + name
                       + t (lang, " in your browser. On Windows the files are on the clipboard (paste them with Ctrl+V); elsewhere the folder opens so you can drag them in. "
                                  "Then copy the question here and send it. No key needed, and WWMA sends nothing itself. Paste the answer back here to get Try buttons.",
                                  " -palvelun selaimeen. Windowsissa tiedostot ovat leikepöydällä (liitä Ctrl+V); muualla kansio aukeaa ja voit vetää ne mukaan. "
                                  "Kopioi sitten kysymys täältä ja lähetä. Avainta ei tarvita, eikä WWMA lähetä mitään itse. Liitä vastaus takaisin tänne, niin saat Kokeile-napit.");
            case ai::Mode::cli:
                return t (lang, "WWMA runs ", "WWMA ajaa ") + ai::cliAppName (p)
                       + t (lang, " installed on this computer, signed in with your own ", " -sovelluksen tältä koneelta, kirjautuneena omalla ")
                       + name + t (lang, " account (your subscription, no API key). Install it and sign in once in a terminal. It reads only the report folder; the answer comes back here.",
                                   "-tililläsi (oma tilauksesi, ei API-avainta). Asenna se ja kirjaudu kerran päätteessä. Se lukee vain raporttikansiota; vastaus tulee tänne.");
            case ai::Mode::api:
                break;
        }
        return t (lang, "WWMA sends the report to ", "WWMA lähettää raportin ") + name
               + t (lang, "'s API with your own API key. It is billed to your API account (usually cents per question). The key stays on this computer, never in the DAW project or a report.",
                          " -rajapintaan omalla API-avaimellasi. Laskutus menee API-tilillesi (yleensä senttejä kysymykseltä). Avain pysyy tällä koneella, ei koskaan DAW-projektissa tai raportissa.");
    }
}

juce::String plainAnswer (const juce::String& markdown)
{
    juce::StringArray lines;
    lines.addLines (markdown);
    for (auto& l : lines)
    {
        auto s = l;
        const auto trimmed = s.trimStart();
        if (trimmed.startsWithChar ('#'))
            s = upper (trimmed.trimCharactersAtStart ("#").trim());
        else if (trimmed.startsWith ("* ") || trimmed.startsWith ("- "))
            s = s.substring (0, s.length() - trimmed.length()) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2 ")) + trimmed.substring (2);
        s = s.replace ("**", "").replace ("__", "").replace ("`", "");
        l = s;
    }
    return lines.joinIntoString ("\n").trim();
}

AiPanel::AiPanel (const ViewState& s) : state (s)
{
    for (auto* b : { &settingsButton, &closeButton, &askButton, &cancelButton, &copyButton, &pasteButton, &openButton, &sendButton,
                     &keepButton, &undoButton, &modelsButton, &testButton, &browseCliButton })
        addChildComponent (b);
    settingsButton.setVisible (true);
    closeButton.setVisible (true);
    settingsButton.setClickingTogglesState (true);
    settingsButton.onClick = [this] { showSettings (settingsButton.getToggleState()); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    askButton.getProperties().set ("primary", true);
    askButton.onClick = [this] { if (onAsk) onAsk(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    copyButton.onClick = [this] { if (onCopyQuestion) onCopyQuestion(); };
    pasteButton.onClick = [this] { if (onPasteAnswer) onPasteAnswer(); };
    openButton.onClick = [this] { if (onOpenFile) onOpenFile(); };
    keepButton.getProperties().set ("primary", true);
    keepButton.onClick = [this] { if (onKeepChange) onKeepChange(); };
    undoButton.onClick = [this] { if (onUndoChange) onUndoChange(); };
    sendButton.onClick = [this]
    {
        const auto q = followUp.getText().trim();
        if (q.isNotEmpty() && onFollowUp)
            onFollowUp (q);
    };

    styleEditor (answerText, true);
    answerText.setReadOnly (true);
    answerText.setCaretVisible (false);
    addChildComponent (answerText);
    styleEditor (followUp, false);
    followUp.onReturnKey = [this] { sendButton.triggerClick(); };
    addChildComponent (followUp);

    for (int i = 0; i < ai::numProviders; ++i)
    {
        auto& b = providerButtons[(size_t) i];
        b.setButtonText (ai::providerName ((ai::Provider) i));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (41);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < ai::numProviders - 1 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i]
        {
            if (! providerButtons[(size_t) i].getToggleState())
                return;
            settings.provider = (ai::Provider) i;
            models.clear();
            testStatus = {};
            cliChecked = false;
            syncSettingsControls();
            settingsEdited();
        };
        addChildComponent (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[(size_t) i];
        b.setClickingTogglesState (true);
        b.setRadioGroupId (42);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i]
        {
            if (! modeButtons[(size_t) i].getToggleState())
                return;
            settings.of (settings.provider).mode = (ai::Mode) i;
            testStatus = {};
            syncSettingsControls();
            settingsEdited();
        };
        addChildComponent (b);
    }
    for (auto* e : { &keyEditor, &modelEditor, &cliEditor })
    {
        styleEditor (*e, false);
        addChildComponent (e);
    }
    keyEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    keyEditor.onTextChange = [this] { settings.of (settings.provider).apiKey = keyEditor.getText().trim(); models.clear(); settingsEdited(); };
    modelEditor.onTextChange = [this] { settings.of (settings.provider).model = modelEditor.getText().trim(); settingsEdited(); };
    cliEditor.onTextChange = [this] { settings.of (settings.provider).cliPath = cliEditor.getText().trim(); cliChecked = false; settingsEdited(); };
    showKey.onClick = [this]
    {
        keyEditor.setPasswordCharacter (showKey.getToggleState() ? (juce::juce_wchar) 0 : (juce::juce_wchar) 0x2022);
        keyEditor.repaint();
    };
    consent.onClick = [this] { settings.consent = consent.getToggleState(); settingsEdited(); };
    addChildComponent (showKey);
    addChildComponent (consent);
    modelsButton.onClick = [this]
    {
        if (models.isEmpty())
        {
            if (onTest) onTest(); // the list comes with the key check
            return;
        }
        juce::PopupMenu menu;
        for (int i = 0; i < models.size(); ++i)
            menu.addItem (i + 1, models[i], true, models[i] == settings.current().model);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modelsButton), [this] (int r)
        {
            if (r > 0)
                modelEditor.setText (models[r - 1], juce::sendNotification);
        });
    };
    testButton.onClick = [this] { if (onTest) onTest(); };
    browseCliButton.onClick = [this] { if (onBrowseCli) onBrowseCli(); };
    updateTexts();
}

void AiPanel::updateTexts()
{
    const auto lang = state.lang;
    settingsButton.setButtonText (upper (t (lang, "Settings", "Asetukset")));
    settingsButton.setTooltip (t (lang, "Choose the AI service and how WWMA reaches it.", "Valitse tekoälypalvelu ja miten WWMA käyttää sitä."));
    closeButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
    closeButton.setTooltip (t (lang, "Close", "Sulje"));
    if (lang == Lang::finnish)
        askButton.setButtonText (upper (juce::String (settings.provider == ai::Provider::claude   ? juce::CharPointer_UTF8 ("Kysy Claudelta")
                                                      : settings.provider == ai::Provider::gemini ? juce::CharPointer_UTF8 ("Kysy Geminilt\xc3\xa4")
                                                                                                   : juce::CharPointer_UTF8 ("Kysy ChatGPT:lt\xc3\xa4"))));
    else
        askButton.setButtonText (upper ("Ask " + ai::providerName (settings.provider)));
    askButton.setTooltip (t (lang, "Saves a report of the mix as it is now and gives it to the AI: the numbers, the song part by part and the spectrogram pictures. Never audio.",
                                   "Tallentaa raportin miksistä sellaisena kuin se nyt on ja antaa sen tekoälylle: luvut, kappaleen osat ja spektrogrammikuvat. Ei koskaan ääntä."));
    cancelButton.setButtonText (upper (t (lang, "Cancel", "Peruuta")));
    copyButton.setButtonText (upper (t (lang, "Copy question", "Kopioi kysymys")));
    copyButton.setTooltip (t (lang, "Copies the question to the clipboard: paste it into the chat after the files.", "Kopioi kysymyksen leikepöydälle: liitä se keskusteluun tiedostojen jälkeen."));
    pasteButton.setButtonText (upper (t (lang, "Paste answer", "Liitä vastaus")));
    pasteButton.setTooltip (t (lang, "Copy the whole answer in the chat, then press this: WWMA reads its suggestions and makes Try buttons for them.",
                                     "Kopioi koko vastaus keskustelusta ja paina tätä: WWMA lukee sen ehdotukset ja tekee niille Kokeile-napit."));
    openButton.setButtonText (upper (t (lang, "Open file", "Avaa tiedosto")));
    openButton.setTooltip (t (lang, "The answer is saved next to the report.", "Vastaus on tallennettu raportin viereen."));
    sendButton.setButtonText (upper (t (lang, "Send", "Lähetä")));
    keepButton.setButtonText (upper (t (lang, "Keep", "Pidä")));
    keepButton.setTooltip (t (lang, "Keep the change in WWMA's EQ / limiter.", "Pidä muutos WWMA:n EQ:ssa / limitterissä."));
    undoButton.setButtonText (upper (t (lang, "Undo", "Peru")));
    undoButton.setTooltip (t (lang, "Puts the EQ / limiter back exactly as they were.", "Palauttaa EQ:n / limitterin täsmälleen ennalleen."));
    followUp.setTextToShowWhenEmpty (t (lang, "Ask more, e.g. \"what about the bass in the second drop?\"", "Kysy lisää, esim. \"entä basso toisessa dropissa?\""), colours::faint);
    const juce::String modeNames[] = { t (lang, "Browser (no key)", "Selain (ei avainta)"), t (lang, "App on this computer", "Sovellus koneella"),
                                       t (lang, "API key", "API-avain") };
    for (int i = 0; i < 3; ++i)
        modeButtons[(size_t) i].setButtonText (modeNames[i]);
    modeButtons[1].setTooltip (t (lang, "Claude Code, Gemini CLI or Codex CLI, signed in with your own subscription. No API key.",
                                        "Claude Code, Gemini CLI tai Codex CLI omalla tilauksellasi kirjautuneena. Ei API-avainta."));
    showKey.setButtonText (t (lang, "Show", "Näytä"));
    consent.setButtonText (t (lang, "Allow WWMA to send the report (numbers and spectrogram pictures, never audio) to the service I chose",
                                    "Salli WWMA:n lähettää raportti (luvut ja spektrogrammikuvat, ei koskaan ääntä) valitsemalleni palvelulle"));
    modelsButton.setButtonText (upper (t (lang, "Models", "Mallit")) + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
    modelsButton.setTooltip (t (lang, "Pick a model from your account's list.", "Valitse malli tilisi listasta."));
    browseCliButton.setButtonText (upper (t (lang, "Choose...", "Valitse...")));
    testButton.setButtonText (upper (t (lang, "Test connection", "Testaa yhteys")));
    keyEditor.setTextToShowWhenEmpty (t (lang, "paste your API key", "liitä API-avaimesi"), colours::faint);
    modelEditor.setTextToShowWhenEmpty (settings.provider == ai::Provider::claude ? ai::defaultModel (ai::Provider::claude) + t (lang, " (default)", " (oletus)")
                                                                                  : t (lang, "picked from your account's list", "valitaan tilisi listasta"), colours::faint);
    cliEditor.setTextToShowWhenEmpty (t (lang, "found automatically", "haetaan automaattisesti"), colours::faint);
    rebuildCards();
    resized();
    repaint();
}

// ---- settings ---------------------------------------------------------------------------
void AiPanel::setSettings (const ai::Settings& s)
{
    settings = s;
    syncSettingsControls();
}

void AiPanel::syncSettingsControls()
{
    const auto& p = settings.current();
    providerButtons[(size_t) settings.provider].setToggleState (true, juce::dontSendNotification);
    modeButtons[(size_t) p.mode].setToggleState (true, juce::dontSendNotification);
    keyEditor.setText (p.apiKey, juce::dontSendNotification);
    modelEditor.setText (p.model, juce::dontSendNotification);
    cliEditor.setText (p.cliPath, juce::dontSendNotification);
    consent.setToggleState (settings.consent, juce::dontSendNotification);
    updateTexts();
}

void AiPanel::settingsEdited()
{
    if (onSettingsChanged)
        onSettingsChanged();
    updateTexts();
}

void AiPanel::showSettings (bool show)
{
    settingsShown = show;
    settingsButton.setToggleState (show, juce::dontSendNotification);
    resized();
    repaint();
}

void AiPanel::setTestStatus (const juce::String& text, bool ok)
{
    testStatus = text;
    testOk = ok;
    repaint();
}

void AiPanel::setModels (const juce::StringArray& m)
{
    models = m;
    repaint();
}

void AiPanel::setCliFound (const juce::File& exe)
{
    cliFound = exe;
    cliChecked = true;
    repaint();
}

// ---- answer ------------------------------------------------------------------------------
void AiPanel::setView (View v)
{
    const bool newAnswer = v.answer != view.answer;
    view = std::move (v);
    if (newAnswer)
    {
        answerText.setText (plainAnswer (view.answer), false);
        answerText.moveCaretToTop (false);
    }
    rebuildCards();
    resized();
    repaint();
}

void AiPanel::rebuildCards()
{
    for (auto& c : cards)
        removeChildComponent (c.get());
    cards.clear();
    const auto lang = state.lang;
    for (size_t i = 0; i < view.cards.size(); ++i)
    {
        const auto& c = view.cards[i];
        std::vector<ListRow::Action> actions;
        if (c.canTryMaster)
            actions.push_back ({ t (lang, "Try on master", "Kokeile masterissa"), [this, i] { if (onTryMaster) onTryMaster ((int) i); }, true, c.masterTooltip });
        if (c.canTry)
            actions.push_back ({ t (lang, "Try", "Kokeile"), [this, i] { if (onTry) onTry ((int) i); }, ! c.canTryMaster,
                                 c.tooltip + "\n\n" + t (lang, "Make the change yourself, then play the same part again: WWMA tells you whether it helped, and your ears decide.",
                                                                "Tee muutos itse ja soita sama kohta uudelleen: WWMA kertoo, auttoiko se, ja korva ratkaisee.") });
        auto row = std::make_unique<ListRow> (c.title, c.detail, std::move (actions));
        addChildComponent (*row);
        cards.push_back (std::move (row));
    }
}

juce::Rectangle<int> AiPanel::body() const
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (22 + 8);
    return r;
}

void AiPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto head = r.removeFromTop (22);
    closeButton.setBounds (head.removeFromRight (24));
    head.removeFromRight (8);
    settingsButton.setBounds (head.removeFromRight (110));

    const bool answerPage = ! settingsShown;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &askButton, &cancelButton, &copyButton, &pasteButton, &openButton,
                                                                          &answerText, &followUp, &sendButton, &keepButton, &undoButton })
        c->setVisible (false);
    for (auto& c : cards)
        c->setVisible (false);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &keyEditor, &modelEditor, &cliEditor, &showKey, &consent, &modelsButton,
                                                                          &testButton, &browseCliButton })
        c->setVisible (false);
    for (auto& b : providerButtons) b.setVisible (! answerPage);
    for (auto& b : modeButtons) b.setVisible (! answerPage);

    if (answerPage)
        layoutAnswer (body());
    else
        layoutSettings (body());
}

void AiPanel::layoutAnswer (juce::Rectangle<int> r)
{
    using Status = View::Status;
    const bool working = view.status == Status::working;
    auto top = r.removeFromTop (28);
    askButton.setVisible (! working);
    askButton.setEnabled (state.assessment.enoughData);
    askButton.setBounds (top.removeFromLeft (juce::jmax (150, juce::GlyphArrangement::getStringWidthInt (fonts::label (13.0f), askButton.getButtonText()) + 30)));
    cancelButton.setVisible (working);
    cancelButton.setBounds (askButton.getBounds().withWidth (110));
    auto placeRight = [&top] (juce::TextButton& b, bool show)
    {
        if (! show)
            return;
        const int w = juce::GlyphArrangement::getStringWidthInt (fonts::label (12.0f), b.getButtonText()) + 22;
        b.setBounds (top.removeFromRight (w));
        b.setVisible (true);
        top.removeFromRight (6);
    };
    placeRight (openButton, view.hasFile);
    placeRight (pasteButton, view.canPaste && ! working);
    placeRight (copyButton, view.canPaste && ! working);

    r.removeFromTop (6);
    statusArea = r.removeFromTop (view.status == Status::idle && view.answer.isEmpty() ? 0 : 40);

    pendingArea = {};
    if (state.chainChangePending)
    {
        pendingArea = r.removeFromBottom (juce::jmax (44, 14 * juce::jmin (4, state.chainChangeLines.size()) + 18));
        auto buttons = pendingArea.withSizeKeepingCentre (pendingArea.getWidth(), 26).removeFromRight (160);
        undoButton.setBounds (buttons.removeFromRight (76));
        buttons.removeFromRight (8);
        keepButton.setBounds (buttons.removeFromRight (76));
        keepButton.setVisible (true);
        undoButton.setVisible (true);
        r.removeFromBottom (6);
    }

    if (view.canFollowUp)
    {
        auto row = r.removeFromBottom (28);
        sendButton.setBounds (row.removeFromRight (80));
        row.removeFromRight (6);
        followUp.setBounds (row);
        sendButton.setVisible (true);
        followUp.setVisible (true);
        sendButton.setEnabled (! working);
        r.removeFromBottom (6);
    }
    for (auto it = cards.rbegin(); it != cards.rend(); ++it)
    {
        (*it)->setBounds (r.removeFromBottom (cardH));
        (*it)->setVisible (true);
    }
    if (! cards.empty())
        r.removeFromBottom (4);
    answerText.setVisible (view.answer.isNotEmpty());
    answerText.setBounds (r);
}

void AiPanel::layoutSettings (juce::Rectangle<int> r)
{
    auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
    auto providers = row (rowH).withTrimmedLeft (labelW);
    for (auto& b : providerButtons)
        b.setBounds (providers.removeFromLeft (110));
    auto modes = row (rowH).withTrimmedLeft (labelW);
    for (auto& b : modeButtons)
        b.setBounds (modes.removeFromLeft (juce::jmin (160, modes.getWidth() / 3 + 1)));
    modeTextArea = row (48);

    keyLabelArea = modelLabelArea = cliLabelArea = cliStatusArea = {};
    const auto m = mode();
    if (m == ai::Mode::api)
    {
        auto keyRow = row (rowH);
        keyLabelArea = keyRow.removeFromLeft (labelW);
        showKey.setBounds (keyRow.removeFromRight (70));
        keyRow.removeFromRight (6);
        keyEditor.setBounds (keyRow);
        auto modelRow = row (rowH);
        modelLabelArea = modelRow.removeFromLeft (labelW);
        modelsButton.setBounds (modelRow.removeFromRight (110));
        modelRow.removeFromRight (6);
        modelEditor.setBounds (modelRow);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &keyEditor, &showKey, &modelEditor, &modelsButton })
            c->setVisible (true);
    }
    else if (m == ai::Mode::cli)
    {
        auto cliRow = row (rowH);
        cliLabelArea = cliRow.removeFromLeft (labelW);
        browseCliButton.setBounds (cliRow.removeFromRight (110));
        cliRow.removeFromRight (6);
        cliEditor.setBounds (cliRow);
        cliStatusArea = row (16).withTrimmedLeft (labelW);
        cliEditor.setVisible (true);
        browseCliButton.setVisible (true);
    }
    if (m != ai::Mode::browser)
    {
        consent.setBounds (row (36));
        consent.setVisible (true);
        auto testRow = row (rowH);
        testButton.setBounds (testRow.removeFromLeft (150));
        testRow.removeFromLeft (10);
        testStatusArea = testRow;
        testButton.setVisible (true);
    }
    else
        testStatusArea = {};
    privacyArea = r.removeFromBottom (34);
}

// ---- paint -------------------------------------------------------------------------------
void AiPanel::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    drawBox (g, getLocalBounds().toFloat());
    auto r = getLocalBounds().reduced (12, 10);
    const auto& p = settings.current();
    const juce::String how = p.mode == ai::Mode::browser ? t (lang, "browser", "selain")
                           : p.mode == ai::Mode::cli     ? ai::cliAppName (settings.provider)
                                                         : t (lang, "API key", "API-avain");
    drawLabel (g, t (lang, "AI assistant", "Tekoälyavustaja") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + ai::providerName (settings.provider) + " (" + how + ")",
               r.removeFromTop (22).withTrimmedRight (150));
    if (settingsShown)
        paintSettings (g, body());
    else
        paintAnswer (g, body());
}

void AiPanel::paintAnswer (juce::Graphics& g, juce::Rectangle<int>)
{
    using Status = View::Status;
    const auto lang = state.lang;
    if (! state.assessment.enoughData && view.status == Status::idle && view.answer.isEmpty())
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (12.5f));
        g.drawFittedText (t (lang, "Play the song (or a good part of it) first: the report needs something to measure.",
                                   "Soita kappale (tai hyvä osa siitä) ensin: raportti tarvitsee jotain mitattavaa."),
                          askButton.getBounds().withX (askButton.getRight() + 12).withRight (getWidth() - 12), juce::Justification::centredLeft, 2);
    }
    if (! statusArea.isEmpty())
    {
        const auto col = view.status == Status::error   ? colours::bad
                       : view.status == Status::working ? colours::accent
                       : view.status == Status::done    ? colours::good
                                                        : colours::info;
        auto s = statusArea;
        g.setColour (col);
        g.setFont (fonts::bodyBold (13.0f));
        g.drawFittedText (view.headline, s.removeFromTop (16), juce::Justification::centredLeft, 1, 0.85f);
        g.setColour (colours::soft);
        g.setFont (fonts::body (12.0f));
        g.drawFittedText (view.message, s, juce::Justification::topLeft, 2, 0.85f);
    }
    if (view.status == Status::idle && view.answer.isEmpty())
    {
        auto hint = body().withTrimmedTop (28 + 14);
        if (state.chainChangePending)
            hint = hint.withBottom (pendingArea.getY() - 6);
        g.setColour (colours::soft);
        g.setFont (fonts::body (13.0f));
        g.drawFittedText (t (lang, "Ask gives the report of your mix (the numbers, the song part by part and the spectrogram pictures, never audio) to the AI you chose in the settings. "
                                   "Its answer comes back here, and its suggestions get Try buttons: WWMA measures whether a change helped, and your ears decide.\n\n"
                                   "When a fix fits WWMA's own mastering EQ or limiter (e.g. a little more bass on the whole master), it also gets Try on master: one click sets the knobs, and Undo puts them back.",
                                   "Kysy antaa miksisi raportin (luvut, kappaleen osat ja spektrogrammikuvat, ei koskaan ääntä) asetuksissa valitsemallesi tekoälylle. "
                                   "Vastaus tulee tänne, ja ehdotuksista tulee Kokeile-nappeja: WWMA mittaa, auttoiko muutos, ja korva ratkaisee.\n\n"
                                   "Kun korjaus sopii WWMA:n omaan masterointi-EQ:hon tai limitteriin (esim. vähän lisää bassoa koko masteriin), siitä tulee myös Kokeile masterissa: yksi klikkaus asettaa nupit, ja Peru palauttaa ne."),
                          hint.removeFromTop (150), juce::Justification::topLeft, 9, 0.9f);
    }
    if (! pendingArea.isEmpty())
    {
        auto a = pendingArea.withTrimmedRight (170);
        g.setColour (colours::suggest.withAlpha (0.12f));
        g.fillRoundedRectangle (pendingArea.toFloat(), 5.0f);
        a = a.reduced (8, 4);
        g.setColour (colours::suggest);
        g.setFont (fonts::bodyBold (12.0f));
        g.drawText (t (lang, "Changed on the master: keep or undo?", "Muutettu masterissa: pidetäänkö vai perutaanko?"), a.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (colours::soft);
        g.setFont (fonts::body (11.5f));
        for (int i = 0; i < juce::jmin (4, state.chainChangeLines.size()) && a.getHeight() >= 12; ++i)
            g.drawFittedText (state.chainChangeLines[i], a.removeFromTop (14), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void AiPanel::paintSettings (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto lang = state.lang;
    auto label = [&] (const juce::String& text, juce::Rectangle<int> area)
    {
        drawLabel (g, upper (text), area, colours::dim, juce::Justification::centredLeft, 11.0f);
    };
    label (t (lang, "Service", "Palvelu"), r.withHeight (rowH));
    label (t (lang, "How", "Tapa"), r.withY (r.getY() + rowH + 8).withHeight (rowH));

    g.setColour (colours::soft);
    g.setFont (fonts::body (12.0f));
    g.drawFittedText (modeText (settings.provider, mode(), lang), modeTextArea, juce::Justification::topLeft, 3, 0.85f);
    if (! keyLabelArea.isEmpty())
        label (t (lang, "API key", "API-avain"), keyLabelArea);
    if (! modelLabelArea.isEmpty())
        label (t (lang, "Model", "Malli"), modelLabelArea);
    if (! cliLabelArea.isEmpty())
        label (t (lang, "App", "Sovellus"), cliLabelArea);
    if (! cliStatusArea.isEmpty())
    {
        g.setFont (fonts::body (11.5f));
        const bool found = cliChecked && cliFound.existsAsFile();
        g.setColour (! cliChecked ? colours::dim : found ? colours::good : colours::warn);
        g.drawFittedText (! cliChecked ? t (lang, "Press Test connection to look for it.", "Paina Testaa yhteys, niin se etsitään.")
                          : found ? t (lang, "Found: ", "Löytyi: ") + cliFound.getFullPathName()
                                  : ai::cliAppName (settings.provider) + t (lang, " was not found. Install it, or choose where it is.", " ei löytynyt. Asenna se tai valitse, missä se on."),
                          cliStatusArea, juce::Justification::centredLeft, 1, 0.8f);
    }
    if (! testStatusArea.isEmpty() && testStatus.isNotEmpty())
    {
        g.setColour (testOk ? colours::good : colours::warn);
        g.setFont (fonts::body (11.5f));
        g.drawFittedText (testStatus, testStatusArea, juce::Justification::centredLeft, 2, 0.8f);
    }
    g.setColour (colours::faint);
    g.setFont (fonts::body (11.0f));
    g.drawFittedText (t (lang, "Kept on this computer only: ", "Tallennettu vain tälle koneelle: ") + ai::settingsFile().getFullPathName()
                          + t (lang, ". Not in the DAW project, not in the reports.", ". Ei DAW-projektissa, ei raporteissa."),
                      privacyArea, juce::Justification::bottomLeft, 2, 0.8f);
}
} // namespace wwma::ui
