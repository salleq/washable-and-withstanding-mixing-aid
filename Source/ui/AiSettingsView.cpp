#include "AiSettingsView.h"

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }

    constexpr int rowH = 30, labelW = 110;

    void styleEditor (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, colours::raised);
        e.setColour (juce::TextEditor::textColourId, colours::text);
        e.setColour (juce::TextEditor::outlineColourId, colours::line);
        e.setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
        e.setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.3f));
        e.setColour (juce::CaretComponent::caretColourId, colours::accent);
        e.setFont (fonts::body (13.0f));
        e.setIndents (8, 6);
    }

    void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Colour c = colours::soft, float size = 12.5f)
    {
        juce::AttributedString a;
        a.append (text, fonts::body (size), c);
        a.setLineSpacing (2.0f);
        a.setWordWrap (juce::AttributedString::byWord);
        a.draw (g, r.toFloat());
    }

    juce::String modeText (ai::Provider p, ai::Mode m, Lang lang)
    {
        const auto name = ai::providerName (p);
        switch (m)
        {
            case ai::Mode::browser:
                return t (lang, "WWMA saves the report and opens ", "WWMA tallentaa raportin ja avaa ") + name
                       + t (lang, " in your browser. On Windows the files are on the clipboard (paste them with Ctrl+V); elsewhere the folder opens so you can drag them in. "
                                  "Then copy the question and send it. No key needed, and WWMA sends nothing itself. Paste the answer back in the AI panel to get Try buttons.",
                                  " -palvelun selaimeen. Windowsissa tiedostot ovat leikepöydällä (liitä Ctrl+V); muualla kansio aukeaa ja voit vetää ne mukaan. "
                                  "Kopioi sitten kysymys ja lähetä. Avainta ei tarvita, eikä WWMA lähetä mitään itse. Liitä vastaus takaisin AI-paneeliin, niin saat Kokeile-napit.");
            case ai::Mode::cli:
                return t (lang, "WWMA runs ", "WWMA ajaa ") + ai::cliAppName (p)
                       + t (lang, " installed on this computer, signed in with your own ", " -sovelluksen tältä koneelta, kirjautuneena omalla ")
                       + name + t (lang, " account (your subscription, no API key). Install it and sign in once in a terminal. It reads only the report folder; the answer comes back to the AI panel.",
                                   "-tililläsi (oma tilauksesi, ei API-avainta). Asenna se ja kirjaudu kerran päätteessä. Se lukee vain raporttikansiota; vastaus tulee AI-paneeliin.");
            case ai::Mode::api:
                break;
        }
        return t (lang, "WWMA sends the report to ", "WWMA lähettää raportin ") + name
               + t (lang, "'s API with your own API key. It is billed to your API account (usually cents per question). The key stays on this computer, never in the DAW project or a report.",
                          " -rajapintaan omalla API-avaimellasi. Laskutus menee API-tilillesi (yleensä senttejä kysymykseltä). Avain pysyy tällä koneella, ei koskaan DAW-projektissa tai raportissa.");
    }
}

AiSettingsView::AiSettingsView (const ViewState& s) : state (s)
{
    for (int i = 0; i < ai::numProviders; ++i)
    {
        auto& b = providerButtons[(size_t) i];
        b.setButtonText (ai::providerName ((ai::Provider) i));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (51);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < ai::numProviders - 1 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i]
        {
            if (! providerButtons[(size_t) i].getToggleState())
                return;
            settings.provider = (ai::Provider) i;
            models.clear();
            testStatus = {};
            cliChecked = false;
            syncControls();
            edited();
        };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[(size_t) i];
        b.setClickingTogglesState (true);
        b.setRadioGroupId (52);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i]
        {
            if (! modeButtons[(size_t) i].getToggleState())
                return;
            settings.of (settings.provider).mode = (ai::Mode) i;
            testStatus = {};
            syncControls();
            edited();
        };
        addAndMakeVisible (b);
    }
    for (auto* e : { &keyEditor, &modelEditor, &cliEditor })
    {
        styleEditor (*e);
        addChildComponent (e);
    }
    keyEditor.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    keyEditor.onTextChange = [this] { settings.of (settings.provider).apiKey = keyEditor.getText().trim(); models.clear(); edited(); };
    modelEditor.onTextChange = [this] { settings.of (settings.provider).model = modelEditor.getText().trim(); edited(); };
    cliEditor.onTextChange = [this] { settings.of (settings.provider).cliPath = cliEditor.getText().trim(); cliChecked = false; edited(); };
    showKey.onClick = [this]
    {
        keyEditor.setPasswordCharacter (showKey.getToggleState() ? (juce::juce_wchar) 0 : (juce::juce_wchar) 0x2022);
        keyEditor.repaint();
    };
    consent.onClick = [this] { settings.consent = consent.getToggleState(); edited(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &showKey, &consent, &modelsButton, &testButton, &browseCliButton })
        addChildComponent (c);
    addAndMakeVisible (folderButton);
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
    folderButton.onClick = [this] { if (onOpenAnswerFolder) onOpenAnswerFolder(); };
    updateTexts();
}

void AiSettingsView::updateTexts()
{
    const auto lang = state.lang;
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
    folderButton.setButtonText (upper (t (lang, "Open folder", "Avaa kansio")));
    folderButton.setTooltip (t (lang, "Opens the folder in your file manager.", "Avaa kansion tiedostonhallintaan."));
    keyEditor.setTextToShowWhenEmpty (t (lang, "paste your API key", "liitä API-avaimesi"), colours::faint);
    modelEditor.setTextToShowWhenEmpty (settings.provider == ai::Provider::claude ? ai::defaultModel (ai::Provider::claude) + t (lang, " (default)", " (oletus)")
                                                                                  : t (lang, "picked from your account's list", "valitaan tilisi listasta"), colours::faint);
    cliEditor.setTextToShowWhenEmpty (t (lang, "found automatically", "haetaan automaattisesti"), colours::faint);
    resized();
    repaint();
}

void AiSettingsView::setSettings (const ai::Settings& s)
{
    settings = s;
    syncControls();
}

void AiSettingsView::syncControls()
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

void AiSettingsView::edited()
{
    if (onSettingsChanged)
        onSettingsChanged();
    updateTexts();
}

void AiSettingsView::setTestStatus (const juce::String& text, bool ok)
{
    testStatus = text;
    testOk = ok;
    repaint();
}

void AiSettingsView::setModels (const juce::StringArray& m)
{
    models = m;
    repaint();
}

void AiSettingsView::setCliFound (const juce::File& exe)
{
    cliFound = exe;
    cliChecked = true;
    repaint();
}

void AiSettingsView::setAnswerFolder (const juce::File& folder)
{
    if (folder == answerFolder)
        return;
    answerFolder = folder;
    repaint();
}

void AiSettingsView::resized()
{
    auto r = getLocalBounds();
    // the right column: where the answers are kept
    auto right = r.removeFromRight (juce::jmin (380, r.getWidth() / 3));
    r.removeFromRight (30);
    savedLabel = right.removeFromTop (20);
    right.removeFromTop (4);
    savedArea = right.removeFromTop (128);
    folderButton.setBounds (right.removeFromTop (28).removeFromLeft (130));
    right.removeFromTop (18);
    privacyArea = right.removeFromTop (60);

    auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (10); return x; };
    auto providers = row (rowH);
    serviceLabel = providers.removeFromLeft (labelW);
    for (auto& b : providerButtons)
        b.setBounds (providers.removeFromLeft (120));
    auto modes = row (rowH);
    howLabel = modes.removeFromLeft (labelW);
    for (auto& b : modeButtons)
        b.setBounds (modes.removeFromLeft (juce::jmin (180, modes.getWidth() / 3 + 1)));
    modeTextArea = row (62).withTrimmedLeft (labelW);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &keyEditor, &modelEditor, &cliEditor, &showKey, &consent, &modelsButton,
                                                                          &testButton, &browseCliButton })
        c->setVisible (false);
    keyLabelArea = modelLabelArea = cliLabelArea = cliStatusArea = testStatusArea = {};
    const auto m = mode();
    if (m == ai::Mode::api)
    {
        auto keyRow = row (rowH);
        keyLabelArea = keyRow.removeFromLeft (labelW);
        showKey.setBounds (keyRow.removeFromRight (80));
        keyRow.removeFromRight (6);
        keyEditor.setBounds (keyRow);
        auto modelRow = row (rowH);
        modelLabelArea = modelRow.removeFromLeft (labelW);
        modelsButton.setBounds (modelRow.removeFromRight (120));
        modelRow.removeFromRight (6);
        modelEditor.setBounds (modelRow);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &keyEditor, &showKey, &modelEditor, &modelsButton })
            c->setVisible (true);
    }
    else if (m == ai::Mode::cli)
    {
        auto cliRow = row (rowH);
        cliLabelArea = cliRow.removeFromLeft (labelW);
        browseCliButton.setBounds (cliRow.removeFromRight (120));
        cliRow.removeFromRight (6);
        cliEditor.setBounds (cliRow);
        cliStatusArea = row (18).withTrimmedLeft (labelW);
        cliEditor.setVisible (true);
        browseCliButton.setVisible (true);
    }
    if (m != ai::Mode::browser)
    {
        consent.setBounds (row (40).withTrimmedLeft (labelW));
        consent.setVisible (true);
        auto testRow = row (rowH).withTrimmedLeft (labelW);
        testButton.setBounds (testRow.removeFromLeft (160));
        testRow.removeFromLeft (12);
        testStatusArea = testRow;
        testButton.setVisible (true);
    }
}

void AiSettingsView::paint (juce::Graphics& g)
{
    const auto lang = state.lang;
    auto label = [&] (const juce::String& text, juce::Rectangle<int> area) { drawLabel (g, upper (text), area); };
    label (t (lang, "Service", "Palvelu"), serviceLabel);
    label (t (lang, "How", "Tapa"), howLabel);
    drawText (g, modeText (settings.provider, mode(), lang), modeTextArea);
    if (! keyLabelArea.isEmpty())
        label (t (lang, "API key", "API-avain"), keyLabelArea);
    if (! modelLabelArea.isEmpty())
        label (t (lang, "Model", "Malli"), modelLabelArea);
    if (! cliLabelArea.isEmpty())
        label (t (lang, "App", "Sovellus"), cliLabelArea);
    if (! cliStatusArea.isEmpty())
    {
        g.setFont (fonts::body (12.0f));
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
        g.setFont (fonts::body (12.0f));
        g.drawFittedText (testStatus, testStatusArea, juce::Justification::centredLeft, 2, 0.8f);
    }

    // where the answers are kept
    drawLabel (g, upper (t (lang, "Saved answers", "Tallennetut vastaukset")), savedLabel, colours::accent, juce::Justification::centredLeft, 13.0f);
    auto s = savedArea;
    drawText (g, t (lang, "Every answer is saved as a Markdown file next to its report, with the suggestions listed at the end:",
                          "Jokainen vastaus tallennetaan Markdown-tiedostona raporttinsa viereen, ehdotukset lopussa listana:"),
              s.removeFromTop (40));
    g.setColour (colours::text);
    g.setFont (fonts::num (12.0f));
    g.drawFittedText (answerFolder.getFullPathName(), s.removeFromTop (34), juce::Justification::topLeft, 2, 0.75f);
    drawText (g, t (lang, "Named like \"WWMA report 2026-11-06 1420 AI answer (Claude).md\". Follow-up questions are added to the same file.",
                          "Nimetty esim. \"WWMA report 2026-11-06 1420 AI answer (Claude).md\". Jatkokysymykset lisätään samaan tiedostoon."),
              s, colours::dim, 11.5f);

    drawText (g, t (lang, "These settings are kept on this computer only: ", "Nämä asetukset ovat vain tällä koneella: ") + ai::settingsFile().getFullPathName()
                     + t (lang, ". Not in the DAW project, not in the reports.", ". Ei DAW-projektissa, ei raporteissa."),
              privacyArea, colours::faint, 11.5f);
}
} // namespace wwma::ui
