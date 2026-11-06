// The AI assistant in the editor: asking Claude, Gemini or ChatGPT about the report, showing the
// answer, and trying its suggestions (by hand, or on WWMA's own EQ / limiter with one click).

#include "PluginEditor.h"

using namespace wwma;
namespace ui = wwma::ui;
using Status = wwma::ui::AiPanel::View::Status;

namespace
{
    const juce::String dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    juce::String clock (int seconds) { return juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2); }
}

bool WwmaAudioProcessorEditor::masterChangesAllowed() const
{
    return state.trackRole == TrackRole::mix && (state.stage == Stage::mastering || state.chainSettings.eqOn);
}

void WwmaAudioProcessorEditor::setAiOpen (bool open)
{
    aiPanel.setVisible (open);
    aiButton.setToggleState (open, juce::dontSendNotification);
    if (open)
    {
        if (isSettingsOpen())
            setSettingsOpen (false);
        aiPanel.toFront (false);
        refreshAiView();
    }
}

void WwmaAudioProcessorEditor::setAiStatus (Status status, const juce::String& headline, const juce::String& message)
{
    aiSession.status = status;
    aiSession.headline = headline;
    aiSession.message = message;
    refreshAiView();
}

void WwmaAudioProcessorEditor::refreshAiView()
{
    const auto lang = state.lang;
    ui::AiPanel::View v;
    v.status = aiSession.status;
    v.headline = aiSession.headline;
    v.message = aiSession.message;
    if (aiSession.subject.isNotEmpty())
        v.message = tr (lang, "About ", "Koskee: ") + aiSession.subject + ". " + v.message;
    if (v.status == Status::working && aiWorker.busy())
        v.message << "  " << clock (aiWorker.elapsedSeconds());
    v.answer = aiSession.answerShown;
    v.hasFile = aiSession.answerFile.existsAsFile();
    if (v.hasFile)
        v.savedFile = aiSession.answerFile.getFullPathName();
    v.canPaste = aiSession.mode == ai::Mode::browser && aiSession.reportFile != juce::File();
    v.canFollowUp = aiSession.mode == ai::Mode::api && aiSession.answerShown.isNotEmpty() && ! aiSession.request.turns.empty()
                    && v.status != Status::working;
    v.canAskAgain = aiSession.key.startsWith ("ref:") && v.status != Status::working;

    // an answer about a reference that is loaded: its times and parts play the track there
    std::vector<SongSection> sections;
    if (const auto info = proc.refs.getInfo (aiSessionSlot()))
    {
        v.jumpLength = (float) info->seconds;
        sections = findSections (info->timeline);
        for (const auto& s : sections)
            v.jumpParts << juce::String::charToString ((juce::juce_wchar) s.letter);
    }

    const auto now = proc.getChainSettings();
    for (const auto& s : aiSession.suggestions)
    {
        ui::AiPanel::Card c;
        juce::StringArray where;
        if (s.part.isNotEmpty()) where.add (tr (lang, "part ", "osa ") + s.part);
        if (s.at.isNotEmpty()) where.add (s.at);
        c.title = s.title + (where.isEmpty() ? juce::String() : " (" + where.joinIntoString (", ") + ")");
        c.detail = s.step;
        c.tooltip = s.step + (s.listenFor.isNotEmpty() ? "\n" + tr (lang, "Listen for: ", "Kuuntele: ") + s.listenFor : juce::String());
        c.canTry = true;
        if (v.jumpLength > 0.0f)
        {
            // the time is the more exact; else the start of the part
            const auto times = ui::findTextLinks (s.at.trim(), {}, v.jumpLength);
            const auto part = s.part.trim().toUpperCase();
            if (! times.empty())
            {
                c.jumpSeconds = times.front().seconds;
                c.jumpLabel = s.at.trim();
            }
            else if (part.length() == 1 && v.jumpParts.containsChar (part[0]))
            {
                c.jumpPart = (char) part[0];
                c.jumpLabel = tr (lang, "part ", "osa ") + part;
            }
        }
        if (! s.chain.empty() && masterChangesAllowed())
        {
            juce::StringArray notes;
            const auto target = ai::applyChange (now, s.chain, ai::aiLimits(), &notes, lang);
            auto lines = ai::describeChange (now, target, lang);
            if (! lines.isEmpty())
            {
                c.canTryMaster = true;
                lines.addArray (notes);
                c.masterTooltip = tr (lang, "Sets WWMA's own EQ / limiter (you can undo it):\n", "Asettaa WWMA:n oman EQ:n / limitterin (voit perua):\n")
                                  + lines.joinIntoString ("\n");
            }
        }
        v.cards.push_back (std::move (c));
    }
    aiPanel.setView (std::move (v));
}

// =====================================================================================
bool WwmaAudioProcessorEditor::aiReady()
{
    const auto lang = state.lang;
    if (aiWorker.busy())
        return false;
    const auto& ps = aiSettings.current();
    if (ps.mode != ai::Mode::browser && ! aiSettings.consent)
    {
        openSettingsTab (ui::SettingsPage::Tab::ai);
        settingsPage.aiSettings().setTestStatus (tr (lang, "First allow WWMA to send the report (the box above), or use the browser.",
                                         "Salli ensin raportin lähetys (yllä oleva ruutu) tai käytä selainta."), false);
        return false;
    }
    if (ps.mode == ai::Mode::api && ps.apiKey.isEmpty())
    {
        openSettingsTab (ui::SettingsPage::Tab::ai);
        settingsPage.aiSettings().setTestStatus (tr (lang, "Paste your API key first.", "Liitä ensin API-avaimesi."), false);
        return false;
    }
    return true;
}

void WwmaAudioProcessorEditor::askAi()
{
    const auto lang = state.lang;
    if (! state.assessment.enoughData)
    {
        setAiStatus (Status::error, tr (lang, "Nothing measured yet", "Mitään ei ole vielä mitattu"),
                     tr (lang, "Play the song (or a good part of it) first.", "Soita kappale (tai hyvä osa siitä) ensin."));
        return;
    }
    if (! aiReady())
        return;
    // the report, the listening data and the pictures, like the Report button saves them
    auto folder = proc.getReportFolder();
    folder.createDirectory();
    const auto file = folder.getChildFile ("WWMA report " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M") + ".md");
    saveReport (file);
    askAiAbout (file, -1);
}

namespace
{
    juce::File savedAnswerFile (const juce::String& key)
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("WWMA").getChildFile ("ai-answers")
            .getChildFile (juce::String::toHexString (key.hashCode64()) + ".xml");
    }
}

juce::String WwmaAudioProcessorEditor::referenceAiKey (int slot) const
{
    const auto info = proc.refs.getInfo (slot);
    if (info == nullptr)
        return {};
    const auto& f = info->file;
    return "ref:" + f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
}

void WwmaAudioProcessorEditor::stashAiSession()
{
    // only sessions worth coming back to: an answer, or the browser waiting for one
    if (aiSession.key.isNotEmpty() && (aiSession.answerShown.isNotEmpty() || aiSession.status == Status::browser))
        aiStash[aiSession.key] = aiSession;
}

void WwmaAudioProcessorEditor::saveAiSessionToDisk() const
{
    // a reference file does not change, so its answer stays good; the mix changes all the time
    if (! aiSession.key.startsWith ("ref:") || aiSession.answerShown.isEmpty())
        return;
    juce::XmlElement x ("AiAnswer");
    x.setAttribute ("key", aiSession.key);
    x.setAttribute ("subject", aiSession.subject);
    x.setAttribute ("provider", (int) aiSession.provider);
    x.setAttribute ("mode", (int) aiSession.mode);
    x.setAttribute ("model", aiSession.request.model);
    x.setAttribute ("report", aiSession.reportFile.getFullPathName());
    x.setAttribute ("answerFile", aiSession.answerFile.getFullPathName());
    x.setAttribute ("saved", juce::Time::getCurrentTime().toISO8601 (true));
    x.createNewChildElement ("Prompt")->addTextElement (aiSession.prompt);
    x.createNewChildElement ("Shown")->addTextElement (aiSession.answerShown);
    x.createNewChildElement ("SuggestionsFrom")->addTextElement (aiSession.suggestionsFrom);
    for (const auto& turn : aiSession.request.turns)
    {
        auto* e = x.createNewChildElement ("Turn");
        e->setAttribute ("user", turn.user);
        e->addTextElement (turn.text);
    }
    const auto file = savedAnswerFile (aiSession.key);
    file.getParentDirectory().createDirectory();
    x.writeTo (file);
}

bool WwmaAudioProcessorEditor::loadAiSessionFromDisk (const juce::String& key)
{
    const auto x = juce::XmlDocument::parse (savedAnswerFile (key));
    if (x == nullptr || ! x->hasTagName ("AiAnswer") || x->getStringAttribute ("key") != key)
        return false;
    auto text = [&x] (const char* tag) { auto* e = x->getChildByName (tag); return e != nullptr ? e->getAllSubText() : juce::String(); };
    AiSession s;
    s.key = key;
    s.subject = x->getStringAttribute ("subject");
    s.provider = (ai::Provider) juce::jlimit (0, ai::numProviders - 1, x->getIntAttribute ("provider"));
    s.mode = (ai::Mode) juce::jlimit (0, 2, x->getIntAttribute ("mode"));
    s.reportFile = juce::File (x->getStringAttribute ("report"));
    s.answerFile = juce::File (x->getStringAttribute ("answerFile"));
    s.prompt = text ("Prompt");
    s.answerShown = text ("Shown");
    s.suggestionsFrom = text ("SuggestionsFrom");
    if (s.answerShown.isEmpty())
        return false;
    s.suggestions = ai::parseSuggestions (s.suggestionsFrom);

    // API: the conversation, to ask more; it needs the report files, which may be gone
    auto& req = s.request;
    req.provider = s.provider;
    req.model = x->getStringAttribute ("model");
    req.apiKey = aiSettings.of (s.provider).apiKey;
    for (auto* e : x->getChildWithTagNameIterator ("Turn"))
        req.turns.push_back ({ e->getBoolAttribute ("user"), e->getAllSubText() });
    const auto folder = s.reportFile.getParentDirectory();
    const auto base = s.reportFile.getFileNameWithoutExtension();
    if (s.mode == ai::Mode::api && s.reportFile.existsAsFile())
    {
        req.context = folder.getChildFile (base + " AI.md").loadFileAsString() + "\n\n---\n\n" + s.reportFile.loadFileAsString();
        for (const auto& f : { folder.getChildFile (base + " AI.png"), folder.getChildFile (base + " AI reference.png") })
        {
            juce::MemoryBlock png;
            if (f.existsAsFile() && f.loadFileAsData (png))
                req.images.push_back (std::move (png));
        }
    }
    else
        req.turns.clear();

    const auto lang = state.lang;
    const auto saved = juce::Time::fromISO8601 (x->getStringAttribute ("saved"));
    s.status = Status::done;
    s.headline = ai::providerName (s.provider) + tr (lang, " answered on ", " vastasi ") + saved.formatted ("%d.%m.%Y %H:%M");
    s.message = tr (lang, "A saved answer about this same file: nothing was sent. Ask again for a new one.",
                          "Tallennettu vastaus tästä samasta tiedostosta: mitään ei lähetetty. Kysy uudelleen saadaksesi uuden.");
    stashAiSession();
    aiSession = std::move (s);
    return true;
}

bool WwmaAudioProcessorEditor::restoreAiSession (const juce::String& key)
{
    if (key.isEmpty() || aiWorker.busy())
        return false;
    const bool same = aiSession.key == key;
    if (same && (aiSession.answerShown.isNotEmpty() || aiSession.status == Status::browser))
        return true;
    // (the same track with no answer: Ask again failed or was cancelled, so the earlier answer is back)
    const auto it = aiStash.find (key);
    if (it == aiStash.end())
        return key.startsWith ("ref:") && loadAiSessionFromDisk (key);
    if (! same)
        stashAiSession();
    aiSession = it->second;
    return true;
}

int WwmaAudioProcessorEditor::aiSessionSlot() const
{
    if (aiSession.key.startsWith ("ref:"))
        for (int i = 0; i < (int) state.refs.size(); ++i)
            if (state.refs[(size_t) i].ready() && referenceAiKey (i) == aiSession.key)
                return i;
    return -1;
}

void WwmaAudioProcessorEditor::followAiReference()
{
    // a question still running stays on screen; the mix's answer is not about any reference
    if (aiWorker.busy() || ! aiSession.key.startsWith ("ref:"))
        return;
    const int slot = state.activeRef;
    if (! juce::isPositiveAndBelow (slot, (int) state.refs.size()) || ! state.refs[(size_t) slot].ready())
        return;
    const auto key = referenceAiKey (slot);
    if (key.isEmpty() || key == aiSession.key)
        return;
    if (! restoreAiSession (key))
    {
        const auto lang = state.lang;
        stashAiSession();
        aiSession = {};
        aiSession.key = key;
        aiSession.subject = state.refs[(size_t) slot].name;
        aiSession.provider = aiSettings.provider;
        aiSession.status = Status::done; // (idle would show the mix's intro instead)
        aiSession.headline = tr (lang, "No answer about this track yet", "Tästä raidasta ei ole vielä vastausta");
        aiSession.message = tr (lang, "Press Ask about this track to ask.", "Kysy painamalla Kysy tästä raidasta.");
    }
    refreshAiView();
}

void WwmaAudioProcessorEditor::jumpAiTrack (char part, float seconds)
{
    const int slot = aiSessionSlot();
    const auto info = proc.refs.getInfo (slot);
    if (info == nullptr || info->seconds <= 0.0)
        return;
    if (part != 0)
    {
        seconds = -1.0f;
        const auto& t = info->timeline;
        for (const auto& s : findSections (t))
            if (s.letter == part && juce::isPositiveAndBelow (s.first, (int) t.points.size()))
            {
                const float song = t.points[(size_t) s.first].songSeconds;
                seconds = song >= 0.0f ? song : heardTime (t, s.first);
            }
    }
    if (seconds < 0.0f)
        return;
    const double f = juce::jlimit (0.0, 1.0, seconds / info->seconds);
    // a loop elsewhere would keep the playback away from that point
    const auto loop = proc.refs.getLoop (slot);
    if (loop.second > loop.first && (f < loop.first || f >= loop.second))
        proc.refs.clearLoop (slot);
    proc.refs.setActive (slot);
    proc.refs.setCue (f);
    if (! (state.listening && proc.refs.isPlaying()))
        toggleReferencePlay (slot);
    readReferences();
}

void WwmaAudioProcessorEditor::askAiAgain()
{
    if (aiWorker.busy() || ! aiSession.key.startsWith ("ref:"))
        return;
    for (int i = 0; i < (int) state.refs.size(); ++i)
        if (referenceAiKey (i) == aiSession.key)
        {
            // the old answer stays until the new one comes, then the new one is saved over it
            askAiAboutReference (i, true);
            return;
        }
    setAiStatus (Status::error, tr (state.lang, "The track is not loaded", "Raitaa ei ole ladattu"),
                 tr (state.lang, "Load the file as a reference again to ask about it.", "Lataa tiedosto uudelleen referenssiksi kysyäksesi siitä."));
}

void WwmaAudioProcessorEditor::askAiAboutReference (int slot, bool fresh)
{
    if (! juce::isPositiveAndBelow (slot, (int) state.refs.size()) || ! state.refs[(size_t) slot].ready())
        return;
    // the panel covers the big view: close it, the answer is about this track
    closeReferenceDetail();
    const auto key = referenceAiKey (slot);
    // asked before (or being asked now): show that answer, no new question
    if (! fresh && (restoreAiSession (key) || (aiWorker.busy() && aiSession.key == key)))
    {
        setAiOpen (true);
        return;
    }
    setAiOpen (true);
    if (! aiReady())
        return;
    const auto file = writeReferenceReport (slot);
    if (file.existsAsFile())
        askAiAbout (file, slot);
}

void WwmaAudioProcessorEditor::askAiAbout (const juce::File& file, int referenceSlot)
{
    const auto lang = state.lang;
    const auto provider = aiSettings.provider;
    const auto& ps = aiSettings.current();
    auto folder = file.getParentDirectory();
    const auto base = file.getFileNameWithoutExtension();
    const bool aboutReference = referenceSlot >= 0;
    const auto& assessment = aboutReference ? state.refs[(size_t) referenceSlot].assessment : state.assessment;

    stashAiSession();
    aiSession = {};
    aiSession.key = aboutReference ? referenceAiKey (referenceSlot) : juce::String ("mix");
    aiSession.reportFile = file;
    if (aboutReference)
        aiSession.subject = state.refs[(size_t) referenceSlot].name;
    aiSession.provider = provider;
    aiSession.mode = ps.mode;

    ai::PromptInput in;
    in.question = listeningQuestion (lastListening, lang);
    in.lang = lang;
    in.stage = state.stage;
    // a reference track alone is not what plays through WWMA's EQ: no master changes for it
    in.masterChanges = ! aboutReference && masterChangesAllowed();
    in.chain = proc.getChainSettings();
    if (aboutReference)
        in.question << tr (lang, " Note: the attached report is about the audio file ", " Huom: liitteenä oleva raportti koskee äänitiedostoa ")
                    << state.refs[(size_t) referenceSlot].name
                    << tr (lang, " alone (for example an earlier version of my song, or a track I compare with), not about the mix playing now.",
                                 " yksinään (esim. biisini aiempi versio tai vertailukappale), ei nyt soivaa miksiä.");
    for (const auto& f : assessment.findings)
        if (f.severity != Severity::good && ! f.earOverruled)
            in.findings.emplace_back (f.id, f.title);
    aiSession.prompt = ai::buildPrompt (in);

    // what the assistant reads
    juce::Array<juce::File> files;
    for (const auto& f : { folder.getChildFile (base + " AI.md"), folder.getChildFile (base + " AI.png"),
                           folder.getChildFile (base + " AI reference.png"), file })
        if (f.existsAsFile())
            files.add (f);
    aiSession.promptFile = folder.getChildFile (base + " AI prompt.md");
    {
        juce::String text = aiSession.prompt;
        text << "\n\nThe files (read all of them before you answer):\n";
        for (const auto& f : files)
            text << "- " << f.getFullPathName() << "\n";
        aiSession.promptFile.replaceWithText (text);
    }

    const auto name = ai::providerName (provider);
    switch (ps.mode)
    {
        case ai::Mode::browser:
        {
            const bool filesCopied = shareReportFiles (file);
            if (! filesCopied)
                juce::SystemClipboard::copyTextToClipboard (aiSession.prompt);
            juce::URL (ai::chatUrl (provider)).launchInDefaultBrowser();
            setAiStatus (Status::browser, name + tr (lang, " opened in your browser", " avattiin selaimeen"),
                         filesCopied ? tr (lang, "1) Paste the files into the chat (Ctrl+V).  2) Press Copy question, paste it and send.  3) Copy the answer and press Paste answer.",
                                                 "1) Liitä tiedostot keskusteluun (Ctrl+V).  2) Paina Kopioi kysymys, liitä se ja lähetä.  3) Kopioi vastaus ja paina Liitä vastaus.")
                                     : tr (lang, "1) Drag the files from the folder that opened into the chat.  2) Paste the question (it is on the clipboard) and send.  3) Copy the answer and press Paste answer.",
                                                 "1) Vedä tiedostot auenneesta kansiosta keskusteluun.  2) Liitä kysymys (se on leikepöydällä) ja lähetä.  3) Kopioi vastaus ja paina Liitä vastaus."));
            return;
        }
        case ai::Mode::cli:
        {
            const auto exe = ai::findCli (provider, ps.cliPath);
            if (! exe.existsAsFile())
            {
                setAiStatus (Status::error, ai::cliAppName (provider) + tr (lang, " was not found", " ei löytynyt"),
                             tr (lang, "Install it and sign in once in a terminal, or choose where it is in the settings. Or use the browser.",
                                       "Asenna se ja kirjaudu kerran päätteessä, tai valitse sen sijainti asetuksista. Tai käytä selainta."));
                return;
            }
            ai::CliRequest r;
            r.provider = provider;
            r.exe = exe;
            r.folder = folder;
            r.image = folder.getChildFile (base + " AI.png");
            r.lastMessage = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wwma-ai-answer-" + juce::Uuid().toDashedString() + ".txt");
            r.prompt = "Read the file " + aiSession.promptFile.getFullPathName().quoted()
                       + " and do what it asks. Read the files it names before you answer. Reply with the answer only.";
            juce::Component::SafePointer<WwmaAudioProcessorEditor> self (this);
            if (aiWorker.runCli (r, [self, tmp = r.lastMessage] (const ai::Reply& reply) { tmp.deleteFile(); if (self != nullptr) self->aiReplied (reply); }))
                setAiStatus (Status::working, ai::cliAppName (provider) + tr (lang, " is reading the report...", " lukee raporttia..."),
                             tr (lang, "This takes a minute or two.", "Tämä kestää minuutin tai pari."));
            return;
        }
        case ai::Mode::api:
            break;
    }

    // API: the data as text, the pictures as images, then the question
    auto& req = aiSession.request;
    req.provider = provider;
    req.apiKey = ps.apiKey;
    req.model = ps.model;
    req.context = folder.getChildFile (base + " AI.md").loadFileAsString() + "\n\n---\n\n" + file.loadFileAsString();
    for (const auto& f : { folder.getChildFile (base + " AI.png"), folder.getChildFile (base + " AI reference.png") })
    {
        juce::MemoryBlock png;
        if (f.existsAsFile() && f.loadFileAsData (png))
            req.images.push_back (std::move (png));
    }
    req.turns.push_back ({ true, aiSession.prompt });
    sendAiRequest();
}

void WwmaAudioProcessorEditor::sendAiRequest()
{
    const auto lang = state.lang;
    auto& req = aiSession.request;
    juce::Component::SafePointer<WwmaAudioProcessorEditor> self (this);
    const auto name = ai::providerName (req.provider);
    if (req.model.isEmpty() && req.provider != ai::Provider::claude)
    {
        // Gemini and ChatGPT: pick a model from the account's own list first
        if (aiWorker.listModels (req.provider, req.apiKey, [self] (const ai::Reply& reply)
            {
                if (self == nullptr)
                    return;
                if (! reply.ok)
                {
                    self->aiReplied (reply);
                    return;
                }
                auto& r = self->aiSession.request;
                r.model = ai::pickModel (r.provider, reply.models);
                self->aiSettings.of (r.provider).model = r.model;
                ai::saveSettings (self->aiSettings);
                self->aiPanel.setSettings (self->aiSettings);
            self->settingsPage.aiSettings().setSettings (self->aiSettings);
                self->settingsPage.aiSettings().setSettings (self->aiSettings);
                self->settingsPage.aiSettings().setModels (reply.models);
                self->sendAiRequest();
            }))
            setAiStatus (Status::working, tr (lang, "Looking up ", "Haetaan ") + name + tr (lang, "'s models...", " -malleja..."), {});
        return;
    }
    if (aiWorker.ask (req, [self] (const ai::Reply& reply) { if (self != nullptr) self->aiReplied (reply); }))
        setAiStatus (Status::working, name + tr (lang, " is reading the report...", " lukee raporttia..."),
                     (req.model.isNotEmpty() ? req.model : ai::defaultModel (req.provider)) + dot
                         + tr (lang, "This takes from a few seconds to a few minutes.", "Tämä kestää muutamasta sekunnista muutamaan minuuttiin."));
}

void WwmaAudioProcessorEditor::aiReplied (const ai::Reply& reply)
{
    const auto lang = state.lang;
    if (! reply.ok)
    {
        // a follow-up that failed: forget the unanswered question, so it can be asked again
        auto& turns = aiSession.request.turns;
        if (turns.size() > 1 && turns.back().user)
            turns.pop_back();
        setAiStatus (Status::error, tr (lang, "No answer", "Ei vastausta"), reply.error);
        return;
    }
    aiSession.request.turns.push_back ({ false, reply.text });
    showAiAnswer (reply.text);
}

void WwmaAudioProcessorEditor::showAiAnswer (const juce::String& answer)
{
    const auto lang = state.lang;
    juce::String shown;
    auto suggestions = ai::parseSuggestions (answer, &shown);
    if (! suggestions.empty() || aiSession.suggestions.empty())
    {
        aiSession.suggestions = std::move (suggestions); // a follow-up without new suggestions keeps the earlier ones
        aiSession.suggestionsFrom = answer;
    }
    if (aiSession.answerShown.isNotEmpty() && aiSession.mode == ai::Mode::api && aiSession.request.turns.size() > 2)
    {
        // a follow-up: the conversation so far, newest last
        const auto& turns = aiSession.request.turns;
        aiSession.answerShown << "\n\n" << tr (lang, "## You", "## Sinä") << "\n" << turns[turns.size() - 2].text << "\n\n## "
                              << ai::providerName (aiSession.provider) << "\n" << shown;
    }
    else
        aiSession.answerShown = shown;
    saveAiAnswerFile();
    saveAiSessionToDisk(); // a reference track's answer, over any earlier one about the same file
    aiStash.erase (aiSession.key);
    const int n = (int) aiSession.suggestions.size();
    setAiStatus (Status::done, ai::providerName (aiSession.provider) + tr (lang, " answered", " vastasi"),
                 n > 0 ? juce::String (n) + tr (lang, n == 1 ? " suggestion to try below. Try one at a time: WWMA measures the next pass, then your ears decide."
                                                             : " suggestions to try below. Try one at a time: WWMA measures the next pass, then your ears decide.",
                                                n == 1 ? " ehdotus kokeiltavaksi alla. Kokeile yksi kerrallaan: WWMA mittaa seuraavan soiton, sitten korva ratkaisee."
                                                       : " ehdotusta kokeiltavaksi alla. Kokeile yksi kerrallaan: WWMA mittaa seuraavan soiton, sitten korva ratkaisee.")
                       : tr (lang, "No suggestions WWMA could read (the answer had no block for WWMA). Read the answer and try by hand.",
                                   "Ei ehdotuksia, joita WWMA osaisi lukea (vastauksessa ei ollut WWMA:n lohkoa). Lue vastaus ja kokeile käsin."));
}

void WwmaAudioProcessorEditor::saveAiAnswerFile()
{
    if (aiSession.reportFile == juce::File())
        return;
    const auto folder = aiSession.reportFile.getParentDirectory();
    const auto name = ai::providerName (aiSession.provider);
    aiSession.answerFile = folder.getChildFile (aiSession.reportFile.getFileNameWithoutExtension() + " AI answer (" + name + ").md");
    juce::String md;
    md << "# " << tr (state.lang, "Answer from ", "Vastaus: ") << name << "\n\n";
    md << tr (state.lang, "About ", "Raportista ") << "`" << aiSession.reportFile.getFileName() << "`";
    if (aiSession.request.model.isNotEmpty())
        md << dot << aiSession.request.model;
    md << "\n\n" << aiSession.answerShown << "\n";
    if (! aiSession.suggestions.empty())
    {
        // the suggestions WWMA read from the answer (they become Try cards in the AI panel)
        md << "\n## " << tr (state.lang, "Suggestions to try", "Kokeiltavat ehdotukset") << "\n\n";
        int i = 0;
        for (const auto& s : aiSession.suggestions)
        {
            juce::StringArray where;
            if (s.part.isNotEmpty()) where.add (tr (state.lang, "part ", "osa ") + s.part);
            if (s.at.isNotEmpty()) where.add (s.at);
            md << ++i << ". **" << s.title << "**" << (where.isEmpty() ? juce::String() : " (" + where.joinIntoString (", ") + ")") << "  \n   " << s.step;
            if (s.listenFor.isNotEmpty())
                md << "  \n   " << tr (state.lang, "Listen for: ", "Kuuntele: ") << s.listenFor;
            md << "\n";
        }
    }
    aiSession.answerFile.replaceWithText (md);
}

void WwmaAudioProcessorEditor::aiFollowUp (const juce::String& question)
{
    if (aiWorker.busy() || aiSession.mode != ai::Mode::api || aiSession.request.turns.empty())
        return;
    aiSession.request.turns.push_back ({ true, question });
    aiPanel.clearFollowUp();
    sendAiRequest();
}

void WwmaAudioProcessorEditor::testAiConnection()
{
    const auto lang = state.lang;
    const auto provider = aiSettings.provider;
    const auto& ps = aiSettings.current();
    if (ps.mode == ai::Mode::cli)
    {
        const auto exe = ai::findCli (provider, ps.cliPath);
        settingsPage.aiSettings().setCliFound (exe);
        settingsPage.aiSettings().setTestStatus (exe.existsAsFile() ? tr (lang, "Found. The first question tells whether it is signed in.", "Löytyi. Ensimmäinen kysymys kertoo, onko se kirjautunut.")
                                                  : ai::cliAppName (provider) + tr (lang, " was not found.", " ei löytynyt."),
                               exe.existsAsFile());
        return;
    }
    if (ps.mode != ai::Mode::api)
        return;
    if (ps.apiKey.isEmpty())
    {
        settingsPage.aiSettings().setTestStatus (tr (lang, "Paste your API key first.", "Liitä ensin API-avaimesi."), false);
        return;
    }
    if (aiWorker.busy())
    {
        settingsPage.aiSettings().setTestStatus (tr (lang, "Busy with a question: try again in a moment.", "Kysymys on kesken: yritä hetken päästä uudelleen."), false);
        return;
    }
    juce::Component::SafePointer<WwmaAudioProcessorEditor> self (this);
    settingsPage.aiSettings().setTestStatus (tr (lang, "Checking the key...", "Tarkistetaan avainta..."), true);
    aiWorker.listModels (provider, ps.apiKey, [self, provider] (const ai::Reply& reply)
    {
        if (self == nullptr)
            return;
        const auto l = self->state.lang;
        if (! reply.ok)
        {
            self->settingsPage.aiSettings().setTestStatus (reply.error, false);
            return;
        }
        auto& p = self->aiSettings.of (provider);
        if (p.model.isEmpty() && provider != ai::Provider::claude)
        {
            p.model = ai::pickModel (provider, reply.models);
            ai::saveSettings (self->aiSettings);
            self->aiPanel.setSettings (self->aiSettings);
            self->settingsPage.aiSettings().setSettings (self->aiSettings);
        }
        self->settingsPage.aiSettings().setModels (reply.models);
        const auto model = p.model.isNotEmpty() ? p.model : ai::defaultModel (provider);
        const bool known = reply.models.contains (model);
        self->settingsPage.aiSettings().setTestStatus (tr (l, "The key works. Model: ", "Avain toimii. Malli: ") + model
                                         + (known ? juce::String() : tr (l, " (not in your account's list: pick another)", " (ei tilisi listassa: valitse toinen)")),
                                     known);
    });
}

// =====================================================================================
bool WwmaAudioProcessorEditor::startTrial (const std::string& findingId, const juce::String& title, const juce::String& step, const juce::String& undoText)
{
    const auto& before = state.currentPass.valid() ? state.currentPass
                       : (! state.passes.empty() ? state.passes.back() : PassRecord {});
    if (! before.valid())
        return false;
    state.trial = {};
    state.trialResult = {};
    state.trial.active = true;
    state.trial.findingId = findingId;
    state.trial.title = title;
    state.trial.step = step;
    state.trial.undoText = undoText;
    state.trial.before = before;
    state.trial.passAtStart = passSeen;
    // what played until now becomes BEFORE, so the ears can compare the change with it
    proc.before.freeze();
    readBefore();
    state.trialResult = judgeTrial (state.trial, state.currentPass, false, true, state.lang, state.showScore);
    progress.syncButtons();
    progress.repaint();
    return true;
}

bool WwmaAudioProcessorEditor::tryAiSuggestion (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) aiSession.suggestions.size()))
        return false;
    const auto lang = state.lang;
    const auto& s = aiSession.suggestions[(size_t) index];
    // tied to one of WWMA's findings only when that finding is there now
    std::string id;
    for (const auto& f : state.assessment.findings)
        if (f.id == s.findingId && ! f.earOverruled)
            id = f.id;
    const bool ok = startTrial (id, s.title, s.step, {});
    if (ok)
        setAiStatus (Status::done, tr (lang, "Trying: ", "Kokeillaan: ") + s.title,
                     tr (lang, "Make the change, then play the same part again. The result shows on the right.",
                               "Tee muutos ja soita sama kohta uudelleen. Tulos näkyy oikealla."));
    else
        setAiStatus (Status::error, tr (lang, "Play the part once first", "Soita kohta ensin kerran"),
                     tr (lang, "WWMA compares the pass after the change with the one before it: play the part, then press Try again.",
                               "WWMA vertaa muutoksen jälkeistä soittoa sitä edeltävään: soita kohta ja paina sitten Kokeile uudelleen."));
    return ok;
}

bool WwmaAudioProcessorEditor::tryAiSuggestionOnMaster (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) aiSession.suggestions.size()) || ! masterChangesAllowed())
        return false;
    const auto& s = aiSession.suggestions[(size_t) index];
    if (s.chain.empty())
        return false;
    std::string id;
    for (const auto& f : state.assessment.findings)
        if (f.id == s.findingId && ! f.earOverruled)
            id = f.id;
    if (! applyChainChange (s.chain, ai::aiLimits(), s.title, id))
        return false;
    // show the knobs that moved, with Keep and Undo
    setAiOpen (false);
    if (! state.chainOpen)
        setChainOpen (true);
    return true;
}

bool WwmaAudioProcessorEditor::useSuggestedChain (bool eq, bool limiter)
{
    const auto lang = state.lang;
    const auto change = ai::changeFromAdvice (state.chainAdvice, eq, limiter);
    if (change.empty())
        return false;
    const auto title = eq && limiter ? tr (lang, "WWMA's suggested EQ and limiter", "WWMA:n ehdottama EQ ja limitteri")
                     : eq            ? tr (lang, "WWMA's suggested EQ", "WWMA:n ehdottama EQ")
                                     : tr (lang, "WWMA's suggested limiter", "WWMA:n ehdottama limitteri");
    return applyChainChange (change, ai::ownLimits(), title);
}

bool WwmaAudioProcessorEditor::applyChainChange (const ai::ChainChange& change, const ai::ChainLimits& limits, const juce::String& title,
                                                 const std::string& findingId)
{
    const auto lang = state.lang;
    const auto current = proc.getChainSettings();
    juce::StringArray notes;
    const auto target = ai::applyChange (current, change, limits, &notes, lang);
    const auto lines = ai::describeChange (current, target, lang);
    if (lines.isEmpty())
        return false;
    // Undo goes back to where the knobs were before the first change that is not kept yet
    if (! proc.chainUndo.has_value())
    {
        proc.chainUndo = current;
        proc.chainUndoLines.clear();
    }
    proc.chainUndoLines.addArray (lines);
    proc.chainUndoLines.addArray (notes);
    setChainParameters (target);
    syncChainChangeState();

    const auto undoText = tr (lang, "Press Undo in the EQ + limiter panel: the knobs go back exactly as they were.",
                                    "Paina Peru EQ + limitteri -paneelissa: nupit palaavat täsmälleen ennalleen.");
    const bool trial = startTrial (findingId, title, tr (lang, "WWMA changed: ", "WWMA muutti: ") + lines.joinIntoString ("; ") + ".", undoText);
    if (! trial)
        setAiStatus (aiSession.status, aiSession.headline,
                     tr (lang, "Changed on the master. Play the part and compare with Hear without, then keep or undo.",
                               "Muutettu masterissa. Soita kohta ja vertaa Kuuntele ilman -napilla, sitten pidä tai peru."));
    refresh (true);
    return true;
}

void WwmaAudioProcessorEditor::setChainParameters (const ChainSettings& s)
{
    using Ids = WwmaAudioProcessor::ChainIds;
    // through the host, as if the knob was turned: automation and the host's undo see it
    auto set = [this] (const char* id, float value)
    {
        if (juce::String (id).isEmpty())
            return;
        if (auto* p = proc.apvts.getParameter (id))
        {
            const float v = p->convertTo0to1 (value);
            if (std::abs (p->getValue() - v) < 1.0e-6f)
                return;
            p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            p->endChangeGesture();
        }
    };
    for (size_t b = 0; b < ChainSettings::numBands; ++b)
    {
        set (Ids::freq[b], s.freq[b]);
        set (Ids::gain[b], s.gain[b]);
        set (Ids::q[b], s.q[b]);
    }
    set (Ids::limGain, s.limiterGain);
    set (Ids::limCeiling, s.ceiling);
    set (Ids::limRelease, s.releaseMs);
    set (Ids::eqOn, s.eqOn ? 1.0f : 0.0f);
    set (Ids::limOn, s.limiterOn ? 1.0f : 0.0f);
}

void WwmaAudioProcessorEditor::syncChainChangeState()
{
    state.chainChangePending = proc.chainUndo.has_value();
    state.chainChangeLines = state.chainChangePending ? proc.chainUndoLines : juce::StringArray();
    chainPanel.refresh();
    if (aiPanel.isVisible())
        refreshAiView();
}

void WwmaAudioProcessorEditor::keepChainChange()
{
    proc.chainUndo.reset();
    proc.chainUndoLines.clear();
    syncChainChangeState();
}

void WwmaAudioProcessorEditor::undoChainChange()
{
    if (! proc.chainUndo.has_value())
        return;
    setChainParameters (*proc.chainUndo);
    proc.chainUndo.reset();
    proc.chainUndoLines.clear();
    // the trial was about that change: it is gone now
    if (state.trial.active && state.trial.undoText.isNotEmpty())
    {
        state.trial = {};
        state.trialResult = {};
        progress.syncButtons();
        progress.repaint();
    }
    syncChainChangeState();
    refresh (true);
}
