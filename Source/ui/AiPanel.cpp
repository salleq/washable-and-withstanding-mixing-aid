#include "AiPanel.h"

namespace wwma::ui
{
namespace
{
    juce::String t (Lang l, const char* en, const char* fi) { return tr (l, en, fi); }

    constexpr int cardH = 48;

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

std::vector<TextLink> findTextLinks (const juce::String& text, const juce::String& parts, float length)
{
    std::vector<TextLink> out;
    if (length <= 0.0f)
        return out;
    using CF = juce::CharacterFunctions;
    const int n = text.length();
    auto at = [&] (int i) -> juce::juce_wchar { return juce::isPositiveAndBelow (i, n) ? text[i] : 0; };
    auto wordChar = [&] (int i) { return CF::isLetterOrDigit (at (i)); };
    auto digit = [&] (int i) { return CF::isDigit (at (i)); };
    // words that name a part: "part G", "section G", "osa G", "osassa G", "kohdassa G", "G-kohta"
    auto partWord = [] (const juce::String& w)
    {
        const auto l = w.toLowerCase();
        for (auto* p : { "part", "section", "osa", "osi", "kohta", "kohda", "kohd" })
            if (l.startsWith (p))
                return true;
        return false;
    };
    for (int i = 0; i < n; ++i)
    {
        if (wordChar (i - 1))
            continue; // only at the start of a word
        const auto c = at (i);
        if (CF::isDigit (c))
        {
            if (at (i - 1) == ':' || at (i - 1) == '.')
                continue; // the end of "1:05:30" or "1.5"
            // a time: "1:05" or "12:40" (not "1:05:30")
            int j = i;
            while (digit (j) && j - i < 2)
                ++j;
            if (at (j) == ':' && digit (j + 1) && digit (j + 2) && ! wordChar (j + 3) && ! (at (j + 3) == ':' && digit (j + 4)))
            {
                const float seconds = (float) (text.substring (i, j).getIntValue() * 60 + text.substring (j + 1, j + 3).getIntValue());
                if (seconds <= length + 1.0f)
                    out.push_back ({ { i, j + 3 }, 0, seconds });
                i = j + 2;
            }
            continue;
        }
        if (c < 'A' || c > 'Z' || ! parts.containsChar (c))
            continue;
        int end = -1;
        if (at (i + 1) == '-')
        {
            int k = i + 2;
            while (CF::isLetter (at (k)))
                ++k;
            if (k > i + 2 && partWord (text.substring (i + 2, k)))
                end = k;
        }
        if (end < 0 && ! wordChar (i + 1))
        {
            int k = i - 1;
            while (at (k) == ' ')
                --k;
            int b = k;
            while (CF::isLetter (at (b)))
                --b;
            const auto before = text.substring (b + 1, k + 1);
            if (k > b && partWord (before))
                end = i + 1;
            else if (at (i - 1) == '(' && at (i + 1) == ')')
                end = i + 1;
            else if (! out.empty() && out.back().part != 0 && i - out.back().range.getEnd() <= 6)
            {
                // "parts B and C", "osat B ja C", "B, C", "B-C"
                const auto between = text.substring (out.back().range.getEnd(), i).trim();
                for (auto* s : { ",", "and", "ja", "&", "/", "-", "to", "or", "tai",
                                 (const char*) juce::CharPointer_UTF8 ("\xe2\x80\x93") })
                    if (between == juce::String (juce::CharPointer_UTF8 (s)))
                        end = i + 1;
            }
        }
        if (end > 0)
            out.push_back ({ { i, end }, (char) c, -1.0f });
    }
    return out;
}

const TextLink* AiPanel::LinkText::linkAt (juce::Point<int> p) const
{
    for (const auto& l : links)
        if (getTextBounds (l.range).containsPoint (p))
            return &l;
    return nullptr;
}

void AiPanel::LinkText::mouseMove (const juce::MouseEvent& e)
{
    juce::TextEditor::mouseMove (e);
    setMouseCursor (linkAt (e.getPosition()) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::IBeamCursor);
}

void AiPanel::LinkText::mouseUp (const juce::MouseEvent& e)
{
    juce::TextEditor::mouseUp (e);
    if (e.mouseWasDraggedSinceMouseDown() || ! getHighlightedRegion().isEmpty())
        return; // selecting text, not clicking a link
    if (const auto* l = linkAt (e.getPosition()))
    {
        const auto link = *l; // onLink may rebuild the links
        if (onLink)
            onLink (link);
    }
}

AiPanel::AiPanel (const ViewState& s) : state (s)
{
    for (auto* b : { &settingsButton, &closeButton, &askButton, &askAgainButton, &cancelButton, &copyButton, &pasteButton, &openButton, &showFileButton, &sendButton,
                     &keepButton, &undoButton })
        addChildComponent (b);
    settingsButton.setVisible (true);
    closeButton.setVisible (true);
    settingsButton.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    askButton.getProperties().set ("primary", true);
    askButton.onClick = [this] { if (onAsk) onAsk(); };
    askAgainButton.onClick = [this] { if (onAskAgain) onAskAgain(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    copyButton.onClick = [this] { if (onCopyQuestion) onCopyQuestion(); };
    pasteButton.onClick = [this] { if (onPasteAnswer) onPasteAnswer(); };
    openButton.onClick = [this] { if (onOpenFile) onOpenFile(); };
    showFileButton.onClick = [this] { if (onShowFile) onShowFile(); };
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
    answerText.onLink = [this] (const TextLink& l) { if (onJump) onJump (l.part, l.seconds); };
    addChildComponent (answerText);
    styleEditor (followUp, false);
    followUp.onReturnKey = [this] { sendButton.triggerClick(); };
    addChildComponent (followUp);

    updateTexts();
}

void AiPanel::updateTexts()
{
    const auto lang = state.lang;
    settingsButton.setButtonText (upper (t (lang, "Settings", "Asetukset")));
    settingsButton.setTooltip (t (lang, "Choose the AI service and how WWMA reaches it (on WWMA's settings page).", "Valitse tekoälypalvelu ja miten WWMA käyttää sitä (WWMA:n asetuksissa)."));
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
    askAgainButton.setButtonText (upper (t (lang, "Ask again", "Kysy uudelleen")));
    askAgainButton.setTooltip (t (lang, "A new answer about this same track. It is saved over the earlier one.",
                                        "Uusi vastaus tästä samasta raidasta. Se tallennetaan edellisen päälle."));
    cancelButton.setButtonText (upper (t (lang, "Cancel", "Peruuta")));
    copyButton.setButtonText (upper (t (lang, "Copy question", "Kopioi kysymys")));
    copyButton.setTooltip (t (lang, "Copies the question to the clipboard: paste it into the chat after the files.", "Kopioi kysymyksen leikepöydälle: liitä se keskusteluun tiedostojen jälkeen."));
    pasteButton.setButtonText (upper (t (lang, "Paste answer", "Liitä vastaus")));
    pasteButton.setTooltip (t (lang, "Copy the whole answer in the chat, then press this: WWMA reads its suggestions and makes Try buttons for them.",
                                     "Kopioi koko vastaus keskustelusta ja paina tätä: WWMA lukee sen ehdotukset ja tekee niille Kokeile-napit."));
    openButton.setButtonText (upper (t (lang, "Open file", "Avaa tiedosto")));
    openButton.setTooltip (t (lang, "Opens the saved answer (a Markdown file next to the report).", "Avaa tallennetun vastauksen (Markdown-tiedosto raportin vieressä)."));
    showFileButton.setButtonText (upper (t (lang, "Show in folder", "Näytä kansiossa")));
    showFileButton.setTooltip (t (lang, "Shows the saved answer and its report in your file manager.", "Näyttää tallennetun vastauksen ja raportin tiedostonhallinnassa."));
    sendButton.setButtonText (upper (t (lang, "Send", "Lähetä")));
    keepButton.setButtonText (upper (t (lang, "Keep", "Pidä")));
    keepButton.setTooltip (t (lang, "Keep the change in WWMA's EQ / limiter.", "Pidä muutos WWMA:n EQ:ssa / limitterissä."));
    undoButton.setButtonText (upper (t (lang, "Undo", "Peru")));
    undoButton.setTooltip (t (lang, "Puts the EQ / limiter back exactly as they were.", "Palauttaa EQ:n / limitterin täsmälleen ennalleen."));
    followUp.setTextToShowWhenEmpty (t (lang, "Ask more, e.g. \"what about the bass in the second drop?\"", "Kysy lisää, esim. \"entä basso toisessa dropissa?\""), colours::faint);
    rebuildCards();
    resized();
    repaint();
}

// ---- settings ---------------------------------------------------------------------------
void AiPanel::setSettings (const ai::Settings& s)
{
    settings = s;
    updateTexts();
}

// ---- answer ------------------------------------------------------------------------------
void AiPanel::setView (View v)
{
    view = std::move (v);
    setAnswerText();
    rebuildCards();
    resized();
    repaint();
}

void AiPanel::setAnswerText()
{
    const auto plain = plainAnswer (view.answer);
    const auto shownFor = plain + "\n" + view.jumpParts + "\n" + juce::String (view.jumpLength);
    if (shownFor == answerShownFor)
        return;
    answerShownFor = shownFor;

    // the times and parts that play the track there: coloured and underlined
    const auto links = findTextLinks (plain, view.jumpParts, view.jumpLength);
    const auto font = answerText.getFont();
    answerText.clear();
    answerText.links.clear();
    int pos = 0;
    auto add = [&] (int end, bool link)
    {
        auto f = font;
        f.setUnderline (link);
        answerText.setFont (f);
        answerText.setColour (juce::TextEditor::textColourId, link ? colours::ref : colours::text);
        answerText.insertTextAtCaret (plain.substring (pos, end));
        pos = end;
    };
    for (const auto& l : links)
    {
        add (l.range.getStart(), false);
        add (l.range.getEnd(), true);
        answerText.links.push_back (l);
    }
    add (plain.length(), false);
    answerText.moveCaretToTop (false);
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
        if (c.jumpPart != 0 || c.jumpSeconds >= 0.0f)
            actions.push_back ({ juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 ")) + c.jumpLabel,
                                 [this, part = c.jumpPart, seconds = c.jumpSeconds] { if (onJump) onJump (part, seconds); }, false,
                                 t (lang, "Plays the track from this point.", "Soittaa raidan tästä kohdasta.") });
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

    for (juce::Component* c : std::initializer_list<juce::Component*> { &askButton, &askAgainButton, &cancelButton, &copyButton, &pasteButton, &openButton,
                                                                          &showFileButton, &answerText, &followUp, &sendButton, &keepButton, &undoButton })
        c->setVisible (false);
    for (auto& c : cards)
        c->setVisible (false);
    layoutAnswer (body());
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
    placeRight (showFileButton, view.hasFile);
    placeRight (openButton, view.hasFile);
    askAgainButton.setButtonText (upper (view.answer.isEmpty() ? t (state.lang, "Ask about this track", "Kysy tästä raidasta")
                                                               : t (state.lang, "Ask again", "Kysy uudelleen")));
    placeRight (askAgainButton, view.canAskAgain);
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
    savedArea = {};
    if (view.savedFile.isNotEmpty() && view.answer.isNotEmpty())
    {
        savedArea = r.removeFromBottom (16);
        r.removeFromBottom (4);
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
    if (! savedArea.isEmpty())
    {
        g.setColour (colours::dim);
        g.setFont (fonts::body (11.5f));
        g.drawFittedText (t (lang, "Saved: ", "Tallennettu: ") + view.savedFile, savedArea, juce::Justification::centredLeft, 1, 0.7f);
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

} // namespace wwma::ui
