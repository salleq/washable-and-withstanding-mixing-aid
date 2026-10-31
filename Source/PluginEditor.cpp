#include "PluginEditor.h"

using namespace wwma;
namespace ui = wwma::ui;
namespace colours = wwma::ui::colours;
namespace fonts = wwma::ui::fonts;

namespace
{
    constexpr int topBar = 56, footer = 50, gap = 10, margin = 14;
    constexpr int leftWidth = 222, rightWidth = 272;
    constexpr size_t historyPoints = 360; // 3 minutes, one point every 0.5 s
    constexpr size_t scorePoints = 120;   // 60 s
}

WwmaAudioProcessorEditor::WwmaAudioProcessorEditor (WwmaAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &loudness, &meters, &history, &compare, &refBox, &tonal, &findings, &score, &translation, &progress,
             &profileBox, &targetBox, &trackBox, &mixButton, &masterButton, &trackButton, &nowButton, &songButton, &mixAButton, &refBButton,
             &resetButton, &langButton, &autoResetToggle, &styleButton, &learnButton, &reportButton, &chainButton })
        content.addAndMakeVisible (c);
    content.addChildComponent (beforeAfter);
    content.addChildComponent (chainPanel);
    content.addChildComponent (lessonBox);
    content.addChildComponent (refDetail);

    profileBox.getProperties().set ("caption", "Preset");
    profileBox.onChange = [this]
    {
        const int id = profileBox.getSelectedId();
        if (id > 0)
            proc.setProfileIndex (id - 1);
        reassess();
    };
    targetBox.onChange = [this]
    {
        const int id = targetBox.getSelectedId();
        if (id > 0)
            proc.setTarget (id - 1);
        reassess();
    };

    auto segment = [] (juce::TextButton& a, juce::TextButton& b, int group)
    {
        for (auto* x : { &a, &b })
        {
            x->setClickingTogglesState (true);
            x->setRadioGroupId (group);
        }
        a.setConnectedEdges (juce::Button::ConnectedOnRight);
        b.setConnectedEdges (juce::Button::ConnectedOnLeft);
    };
    segment (mixButton, masterButton, 1);
    // MIX | MASTER | TRACK: one group of three
    trackButton.setClickingTogglesState (true);
    trackButton.setRadioGroupId (1);
    masterButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    trackButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    trackBox.onChange = [this]
    {
        const int id = trackBox.getSelectedId();
        if (id > 0)
            setTrackRole ((TrackRole) id);
    };
    segment (nowButton, songButton, 2);
    segment (mixAButton, refBButton, 3);
    refBButton.getProperties().set ("onColour", (juce::int64) colours::ref.getARGB());
    mixAButton.onClick = [this] { if (mixAButton.getToggleState()) setListening (false); };
    refBButton.onClick = [this] { if (refBButton.getToggleState()) setListening (true); };

    refBox.onBrowse = [this] (int slot) { browseReference (slot); };
    refBox.onSelect = [this] (int slot) { proc.refs.setActive (slot); readReferences(); reassess(); };
    refBox.onRemove = [this] (int slot) { proc.removeReference (slot); readReferences(); reassess(); };
    refBox.onCue = [this] (int, double fraction) { proc.refs.setCue (fraction); };
    refBox.onPlayPause = [this] (int slot) { toggleReferencePlay (slot); };
    refBox.onOpenDetail = [this] (int slot) { openReferenceDetail (slot); };
    refBox.onToggleEarlier = [this] (int slot) { proc.refs.setEarlier (slot, ! proc.refs.isEarlier (slot)); readReferences(); reassess(); };
    refDetail.onToggleEarlier = [this] { refBox.onToggleEarlier (refDetail.getSlot()); };
    refDetail.onClose = [this] { closeReferenceDetail(); };
    refDetail.onPlayPause = [this] { toggleReferencePlay (refDetail.getSlot()); };
    refDetail.onCue = [this] (double f)
    {
        proc.refs.setActive (refDetail.getSlot());
        proc.refs.setCue (f);
        readReferences();
    };
    refDetail.onLoop = [this] (double a, double b)
    {
        proc.refs.setLoop (refDetail.getSlot(), a, b);
        proc.refs.setActive (refDetail.getSlot());
        proc.refs.setCue (a);
        readReferences();
    };
    refDetail.onClearLoop = [this] { proc.refs.clearLoop (refDetail.getSlot()); readReferences(); };
    refDetail.onPlayLoop = [this]
    {
        const int slot = refDetail.getSlot();
        proc.refs.setActive (slot);
        proc.refs.setCue (proc.refs.getLoop (slot).first);
        if (! (state.listening && proc.refs.isPlaying()))
            toggleReferencePlay (slot);
        readReferences();
    };

    mixButton.onClick = [this] { if (mixButton.getToggleState()) { proc.setStage (Stage::mixing); setTrackRole (TrackRole::mix); } };
    masterButton.onClick = [this] { if (masterButton.getToggleState()) { proc.setStage (Stage::mastering); setTrackRole (TrackRole::mix); } };
    trackButton.onClick = [this] { if (trackButton.getToggleState()) setTrackRole (proc.getLastTrackKind()); };
    nowButton.onClick = [this] { if (nowButton.getToggleState()) { proc.setWindow (Window::now); state.scoreHistory.clear(); reassess(); } };
    songButton.onClick = [this] { if (songButton.getToggleState()) { proc.setWindow (Window::song); state.scoreHistory.clear(); reassess(); } };

    resetButton.onClick = [this]
    {
        proc.analyzer.requestReset();
        proc.inputAnalyzer.requestReset();
        state.scoreHistory.clear();
        state.shortTermHistory.clear();
    };
    langButton.onClick = [this]
    {
        proc.setLang (proc.getLang() == Lang::finnish ? Lang::english : Lang::finnish);
        state.lang = proc.getLang();
        updateTexts();
        reassess();
    };
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        content.addAndMakeVisible (mixLoadButton);
        content.addAndMakeVisible (mixPlayButton);
        content.addAndMakeVisible (mixSeek);
        autoResetToggle.setVisible (false);
        mixLoadButton.onClick = [this] { browseMix(); };
        mixPlayButton.setClickingTogglesState (true);
        mixPlayButton.onClick = [this]
        {
            proc.mixTrack.setPlaying (mixPlayButton.getToggleState() && proc.mixTrack.getStatus (0) == ReferenceTracks::Status::ready);
            updateTexts();
        };
        if (const auto file = proc.getMixFile(); file.existsAsFile() && proc.mixTrack.getStatus (0) == ReferenceTracks::Status::empty)
            proc.mixTrack.load (0, file);
    }
    autoResetToggle.setToggleState (proc.getAutoReset(), juce::dontSendNotification);
    autoResetToggle.onClick = [this] { proc.setAutoReset (autoResetToggle.getToggleState()); };

    styleButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.14f));
    styleButton.setColour (juce::TextButton::textColourOffId, colours::accent);
    styleButton.setVisible (false);
    styleButton.onClick = [this]
    {
        // the user accepted the suggestion: switch to the closest preset
        if (styleSuggests && styleGuess.valid && styleGuess.best >= 0)
        {
            proc.setProfileIndex (styleGuess.best);
            profileBox.setSelectedId (styleGuess.best + 1, juce::dontSendNotification);
            reassess();
        }
    };

    progress.onSave = [this] { saveSnapshot(); };
    progress.onClear = [this] { clearSnapshots(); };
    progress.onCompare = [this] { setCompareOpen (! state.compareOpen); };
    progress.onConfirm = [this] { confirmSnapshotSaved(); };
    beforeAfter.onClose = [this] { setCompareOpen (false); };
    lessonBox.onPrev = [this] { showLesson (state.lessonIndex - 1); };
    lessonBox.onNext = [this] { showLesson (state.lessonIndex + 1); };

    learnButton.setClickingTogglesState (true);
    learnButton.onClick = [this] { setLearnMode (learnButton.getToggleState()); };
    chainButton.setClickingTogglesState (true);
    chainButton.onClick = [this] { setChainOpen (chainButton.getToggleState()); };
    chainPanel.onClose = [this] { setChainOpen (false); };
    chainPanel.onShowSuggestions = [this] (bool on) { setShowSuggestions (on); };
    chainPanel.onListenWithout = [this] (bool on) { setListeningWithout (on); };
    state.chainOpen = proc.getChainOpen();
    state.showSuggestions = proc.getShowSuggestions();
    state.chainSettings = proc.getChainSettings();
    chainButton.setToggleState (state.chainOpen, juce::dontSendNotification);
    reportButton.onClick = [this] { chooseReportFile(); };
    state.learnMode = proc.getLearnMode();
    tonal.setShowDifference (proc.getShowDifference());
    tonal.onShowDifferenceChanged = [this] (bool b) { proc.setShowDifference (b); };
    state.lessonIndex = juce::jlimit (0, lessonCount() - 1, proc.getLessonIndex());
    state.lessonsDone = proc.getLessonsDone();
    learnButton.setToggleState (state.learnMode, juce::dontSendNotification);

    state.lang = proc.getLang();
    loadSnapshots();
    readReferences();
    updateTexts();

    setResizable (true, true);
    setResizeLimits (baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 8 / 5, baseHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);

    if (state.chainOpen && state.learnMode)
        state.chainOpen = false;
    proc.inputAnalysisWanted.store (state.chainOpen);
    updatePanels();
    refresh();
    startTimerHz (30);
}

WwmaAudioProcessorEditor::~WwmaAudioProcessorEditor()
{
    stopTimer();
    proc.listenToReference.store (false); // never leave the reference playing with no window to show it
    proc.listenWithoutChain.store (false); // nor the bypass on
    proc.inputAnalysisWanted.store (false);
    setLookAndFeel (nullptr);
}

void WwmaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void WwmaAudioProcessorEditor::resized()
{
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / baseWidth, (float) getHeight() / baseHeight));
}

// =====================================================================================
void WwmaAudioProcessorEditor::Content::paint (juce::Graphics& g)
{
    const auto lang = editor.proc.getLang();
    g.fillAll (colours::background);

    // top bar
    g.setColour (colours::panel);
    g.fillRect (0, 0, baseWidth, topBar);
    g.setColour (colours::line);
    g.drawHorizontalLine (topBar - 1, 0.0f, (float) baseWidth);

    g.setColour (colours::accent);
    g.setFont (fonts::labelBold (26.0f));
    g.drawText ("WWMA", margin, 0, 80, topBar, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (fonts::body (12.5f));
    g.drawText (tr (lang, "mixing aid", "miksausapu"), margin + 64, 2, 80, topBar, juce::Justification::centredLeft);

    // A/B
    g.setColour (colours::dim);
    g.setFont (fonts::label (11.5f));
    g.drawText (ui::upper (tr (lang, "Listen", "Kuuntele")), 716, 0, 48, topBar, juce::Justification::centredRight);
    const auto& st = editor.state;
    if (st.activeRefView() != nullptr)
    {
        auto pill = juce::Rectangle<float> (918.0f, 19.0f, 76.0f, 18.0f);
        g.setColour (colours::ref);
        g.fillRoundedRectangle (pill, 4.0f);
        g.setColour (juce::Colour (0xff1a1230));
        g.setFont (fonts::labelBold (10.5f));
        g.drawText (ui::upper (tr (lang, "Level-matched", "Tasattu")), pill, juce::Justification::centred);
    }

    // what the plugin does to the sound right now
    const bool listening = st.listening;
    const bool eq = st.chainSettings.eqOn, lim = st.chainSettings.limiterOn;
    const bool without = st.listeningWithout && (eq || lim);
    juce::String status = tr (lang, "Analysis only", "Vain mittaus");
    juce::Colour statusCol = colours::dim, dotCol = colours::good;
    if (listening)
    {
        status = tr (lang, "Playing reference", "Referenssi soi");
        statusCol = dotCol = colours::ref;
    }
    else if (without)
    {
        status = tr (lang, "Bypass, level-matched", "Ohitus, tasattu");
        statusCol = dotCol = colours::suggest;
    }
    else if (eq || lim)
    {
        status = eq && lim ? tr (lang, "EQ + limiter on", "EQ + limitteri päällä")
                           : (eq ? tr (lang, "EQ on", "EQ päällä") : tr (lang, "Limiter on", "Limitteri päällä"));
        statusCol = dotCol = colours::accent;
    }
    const bool strong = statusCol != colours::dim;
    auto safe = juce::Rectangle<int> (1002, 0, 80, topBar);
    g.setColour (dotCol.withAlpha (0.25f));
    g.fillEllipse ((float) safe.getX() - 3.0f, (float) safe.getCentreY() - 7.0f, 14.0f, 14.0f);
    g.setColour (dotCol);
    g.fillEllipse ((float) safe.getX(), (float) safe.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (statusCol);
    g.setFont (strong ? fonts::bodyBold (11.5f) : fonts::body (11.5f));
    g.drawFittedText (status, safe.withTrimmedLeft (14), juce::Justification::centredLeft, 2, 0.9f);

    // footer
    const int fy = baseHeight - footer;
    g.setColour (colours::panel);
    g.fillRect (0, fy, baseWidth, footer);
    g.setColour (colours::line);
    g.drawHorizontalLine (fy, 0.0f, (float) baseWidth);

    ui::drawEarIcon (g, juce::Rectangle<float> ((float) margin, (float) fy + 11.0f, 28.0f, 28.0f), colours::accent);
    g.setColour (colours::accent);
    g.setFont (fonts::labelBold (15.0f));
    const auto ear = ui::upper (tr (lang, "Your ears decide", "Korva ratkaisee"));
    g.drawText (ear, margin + 36, fy, 140, footer, juce::Justification::centredLeft);
    const int earW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), ear);
    g.setColour (colours::soft);
    g.setFont (fonts::body (12.5f));
    g.drawFittedText (tr (lang, "Numbers are advice, not rules. If it sounds good on a phone and in a car, it is good.",
                                "Luvut ovat neuvoja, eivät sääntöjä. Jos se kuulostaa hyvältä puhelimessa ja autossa, se on hyvä."),
                      juce::Rectangle<int> (margin + 36 + earW + 14, fy, 360 - earW, footer), juce::Justification::centredLeft, 2, 0.9f);
}

void WwmaAudioProcessorEditor::Content::resized()
{
    auto& e = editor;

    // top bar
    e.profileBox.setBounds (160, 12, 190, 32);
    e.mixButton.setBounds (358, 12, 64, 32);
    e.masterButton.setBounds (422, 12, 72, 32);
    e.trackButton.setBounds (494, 12, 66, 32);
    e.targetBox.setBounds (568, 12, 140, 32);
    e.trackBox.setBounds (568, 12, 140, 32);
    e.mixAButton.setBounds (766, 12, 72, 32);
    e.refBButton.setBounds (838, 12, 72, 32);
    e.langButton.setBounds (baseWidth - margin - 40, 12, 40, 32);

    // footer, right to left
    const int fy = baseHeight - footer + 10;
    int x = baseWidth - margin;
    auto place = [&x, fy] (juce::Component& c, int w, int space = 8) { x -= w; c.setBounds (x, fy, w, 30); x -= space; };
    place (e.resetButton, 130, 12);
    if (e.proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        // no DAW transport here, so "reset on play" has no use: your own song's controls take its place
        e.mixSeek.setBounds (x - 124, fy + 32, 124, 6);
        place (e.mixLoadButton, 90, 4);
        place (e.mixPlayButton, 30, 10);
    }
    else
        place (e.autoResetToggle, 124, 10);
    place (e.songButton, 66, 0);
    place (e.nowButton, 58, 14);
    place (e.reportButton, 84, 8);
    place (e.learnButton, 78, 8);
    place (e.chainButton, 104, 8);

    // columns
    const int top = topBar + gap, bottom = baseHeight - footer - gap;
    const int lx = margin, cx = lx + leftWidth + gap, rx = baseWidth - margin - rightWidth;
    const int cw = rx - gap - cx;

    e.loudness.setBounds (lx, top, leftWidth, 226);
    e.meters.setBounds (lx, top + 226 + gap, leftWidth, 236);
    const auto lowLeft = juce::Rectangle<int> (lx, top + 226 + 236 + 2 * gap, leftWidth, bottom - (top + 226 + 236 + 2 * gap));
    e.history.setBounds (lowLeft);
    e.compare.setBounds (lowLeft);

    e.refBox.setBounds (cx, top, cw, 108);
    e.tonal.setBounds (cx, top + 108 + gap, cw, 256);
    e.styleButton.setBounds (cx + cw - 12 - 330, top + 108 + gap + 8, 330, 22);
    e.findings.setBounds (cx, top + 108 + 256 + 2 * gap, cw, bottom - (top + 108 + 256 + 2 * gap));
    e.beforeAfter.setBounds (e.findings.getBounds());
    e.lessonBox.setBounds (e.findings.getBounds());
    e.chainPanel.setBounds (e.findings.getBounds());
    e.refDetail.setBounds (e.tonal.getBounds());

    e.score.setBounds (rx, top, rightWidth, 196);
    e.translation.setBounds (rx, top + 196 + gap, rightWidth, 222);
    e.progress.setBounds (rx, top + 196 + 222 + 2 * gap, rightWidth, bottom - (top + 196 + 222 + 2 * gap));
}

// =====================================================================================
void WwmaAudioProcessorEditor::fillProfileBox()
{
    const auto lang = proc.getLang();
    profileBox.clear (juce::dontSendNotification);
    const auto& all = builtInProfiles();
    profileBox.addSectionHeading (tr (lang, "Electronic", "Elektroninen"));
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].electronic)
            profileBox.addItem (all[i].name, (int) i + 1);
    profileBox.addSectionHeading (tr (lang, "Other styles", "Muut tyylit"));
    for (size_t i = 0; i < all.size(); ++i)
        if (! all[i].electronic)
            profileBox.addItem (all[i].name, (int) i + 1);
    profileBox.addSeparator();
    profileBox.addItem (tr (lang, "Reference track", "Referenssiraita"), proc.referenceIndex() + 1);
    profileBox.setItemEnabled (proc.referenceIndex() + 1, proc.hasReference());
    profileBox.setSelectedId (proc.getProfileIndex() + 1, juce::dontSendNotification);
}

void WwmaAudioProcessorEditor::fillTargetBox()
{
    targetBox.clear (juce::dontSendNotification);
    const auto names = WwmaAudioProcessor::targetNames (proc.getLang());
    for (int i = 0; i < names.size(); ++i)
        targetBox.addItem (names[i], i + 1);
    targetBox.setSelectedId (proc.getTarget() + 1, juce::dontSendNotification);
}

void WwmaAudioProcessorEditor::fillTrackBox()
{
    trackBox.clear (juce::dontSendNotification);
    for (int i = 1; i < numTrackRoles; ++i)
        trackBox.addItem (trackRoleName ((TrackRole) i, proc.getLang()), i);
    trackBox.setSelectedId ((int) proc.getLastTrackKind(), juce::dontSendNotification);
}

void WwmaAudioProcessorEditor::setTrackRole (TrackRole r)
{
    proc.setTrackRole (r);
    if (r != TrackRole::mix && state.chainOpen)
        setChainOpen (false);
    state.scoreHistory.clear();
    updateTexts();
    reassess();
}

void WwmaAudioProcessorEditor::updateTexts()
{
    const auto lang = proc.getLang();
    fillProfileBox();
    fillTargetBox();
    fillTrackBox();
    const bool trackMode = proc.getTrackRole() != TrackRole::mix;
    trackBox.getProperties().set ("caption", tr (lang, "Track", "Raita"));
    trackBox.setTooltip (tr (lang, "What this track is. WWMA measures what matters for it: the kick's tuning, length and click, the bass's harmonics and mono, or how kick and bass share the low end. With WWMA on both the kick and the bass, the two see each other.",
                                   "Mikä raita tämä on. WWMA mittaa sille tärkeät asiat: kickin virityksen, pituuden ja klikin, basson harmoniset ja monon, tai miten kick ja basso jakavat bassopään. Kun WWMA on sekä kickillä että bassolla, ne näkevät toisensa."));
    trackButton.setButtonText (ui::upper (tr (lang, "Track", "Raita")));
    trackButton.setTooltip (tr (lang, "Track mode: put WWMA on a single kick, bass or kick + bass bus instead of last on the master. It still only measures and suggests.",
                                      "Raitatila: laita WWMA yksittäiselle kick-, basso- tai kick + basso -raidalle masterin viimeisen paikan sijaan. Se edelleen vain mittaa ja ehdottaa."));
    trackBox.setVisible (trackMode);
    targetBox.setVisible (! trackMode);
    chainButton.setEnabled (! trackMode);
    targetBox.getProperties().set ("caption", tr (lang, "Target", "Kohde"));
    targetBox.setTooltip (tr (lang, "Loudness target. The style's own is a good start; pick streaming if the song goes to Spotify and friends.",
                                    "Äänekkyystavoite. Tyylin oma on hyvä alku; valitse suoratoisto, jos biisi menee Spotifyyn ja muihin palveluihin."));
    profileBox.setTooltip (tr (lang, "Style profile: what a finished master in this style usually looks like.",
                                     "Tyyliprofiili: miltä valmis master tässä tyylissä yleensä näyttää."));
    mixButton.setButtonText (ui::upper (tr (lang, "Mix", "Miksaus")));
    mixButton.setTooltip (tr (lang, "Mixing: loudness and peaks are not judged yet, only balance, width, dynamics and drums.",
                                    "Miksaus: äänekkyyttä ja huippuja ei vielä arvioida, vain tasapainoa, leveyttä, dynamiikkaa ja rumpuja."));
    masterButton.setButtonText (ui::upper (tr (lang, "Master", "Master")));
    masterButton.setTooltip (tr (lang, "Mastering: everything is judged, including loudness and true peak.",
                                       "Masterointi: kaikki arvioidaan, myös äänekkyys ja true peak."));
    nowButton.setButtonText (tr (lang, "NOW", "NYT"));
    nowButton.setTooltip (tr (lang, "Last 3 seconds. Loop the part you are working on and watch the score while you tweak.",
                                    "Viimeiset 3 sekuntia. Looppaa työstettävää kohtaa ja seuraa mittaria säätäessäsi."));
    songButton.setButtonText (tr (lang, "SONG", "BIISI"));
    songButton.setTooltip (tr (lang, "Everything since the last reset. Play the whole song.",
                                     "Kaikki viimeisestä nollauksesta lähtien. Soita koko kappale."));
    resetButton.setButtonText (tr (lang, "Reset analysis", "Nollaa mittaus"));
    resetButton.setTooltip (tr (lang, "Start measuring again.", "Aloita mittaus alusta."));
    autoResetToggle.setButtonText (tr (lang, "Reset on play", "Nollaa playssa"));
    autoResetToggle.setTooltip (tr (lang, "Start a new measurement every time the DAW starts playing.",
                                          "Aloita uusi mittaus aina, kun DAW alkaa soittaa."));
    mixAButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("A \xc2\xb7 ")) + ui::upper (tr (lang, "Mix", "Miksi")));
    mixAButton.setTooltip (tr (lang, "Hear your mix. The sound passes through untouched, unless you turned on the EQ or limiter.",
                                     "Kuuntele omaa miksiä. Ääni menee läpi koskemattomana, ellet ole kytkenyt EQ:ta tai limitteriä päälle."));
    refBButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("B \xc2\xb7 ")) + "REF");
    refBButton.setTooltip (tr (lang, "Hear the active reference track instead of your mix, at the same loudness, so the louder one does not win just by being louder. Follows the song position; click its waveform to jump.",
                                     "Kuuntele aktiivista referenssiraitaa miksin sijaan samalla äänekkyydellä, jottei kovempi voita vain siksi, että se on kovempi. Seuraa kappaleen kohtaa; klikkaa aaltomuotoa hypätäksesi."));
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        const auto status = proc.mixTrack.getStatus (0);
        const auto name = proc.mixTrack.getInfo (0) != nullptr ? proc.mixTrack.getInfo (0)->name : juce::String();
        mixLoadButton.setButtonText (status == ReferenceTracks::Status::loading ? tr (lang, "Loading...", "Ladataan...")
                                     : name.isNotEmpty() ? name : ui::upper (tr (lang, "Load your mix", "Lataa miksisi")));
        mixLoadButton.setTooltip ((name.isNotEmpty() ? name + "\n" : juce::String()) + tr (lang, "Your own song: click to choose a file, or drop it here. The audio interface input is not used in the standalone app. Reference tracks go in the boxes below.",
                                            "Oma biisisi: klikkaa ja valitse tiedosto tai pudota se tähän. Äänikortin sisääntuloa ei käytetä standalone-sovelluksessa. Referenssiraidat menevät alla oleviin laatikoihin."));
        mixPlayButton.setButtonText (mixPlayButton.getToggleState() ? juce::String (juce::CharPointer_UTF8 ("\xe2\x8f\xb8")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")));
        mixPlayButton.setEnabled (status == ReferenceTracks::Status::ready);
        mixPlayButton.setTooltip (tr (lang, "Play / pause your mix (when B · REF is on you hear the reference instead).",
                                            "Soita / pysäytä miksisi (kun B · REF on päällä, kuulet referenssin)."));
    }
    langButton.setButtonText (lang == Lang::finnish ? "EN" : "FI");
    langButton.setTooltip (lang == Lang::finnish ? juce::String ("In English") : juce::String (juce::CharPointer_UTF8 ("Suomeksi")));
    progress.updateTexts();
    beforeAfter.updateTexts();
    refDetail.updateTexts();
    lessonBox.updateTexts();
    chainButton.setButtonText (ui::upper (tr (lang, "EQ + Limiter", "EQ + limitteri")));
    chainButton.setTooltip (tr (lang, "Mastering EQ and limiter. You turn the knobs; WWMA shows suggested values next to them, worked out from your mix and the style. Both are off until you turn them on.",
                                      "Masterointi-EQ ja limitteri. Sinä käännät nupit; WWMA näyttää ehdotetut arvot niiden vieressä miksisi ja tyylin perusteella. Molemmat ovat pois päältä, kunnes kytket ne."));
    chainPanel.updateTexts();
    learnButton.setButtonText (ui::upper (tr (lang, "Learn", "Opettele")));
    learnButton.setTooltip (tr (lang, "Lesson mode: short hands-on tasks (like ducking the bass under the kick). WWMA watches the last 3 seconds and tells you when you've made it.",
                                      "Oppimistila: lyhyitä käytännön tehtäviä (esim. basson duckaus kickin alle). WWMA seuraa viimeisiä 3 sekuntia ja kertoo, kun onnistuit."));
    reportButton.setButtonText (ui::upper (tr (lang, "Report", "Raportti")));
    reportButton.setTooltip (tr (lang, "Save a report of this mix (Markdown + JSON). If an earlier report is in the same folder, the new one shows what changed since.",
                                       "Tallenna raportti tästä miksistä (Markdown + JSON). Jos samassa kansiossa on aiempi raportti, uusi näyttää, mikä muuttui sen jälkeen."));

    const bool now = proc.getWindow() == Window::now;
    nowButton.setToggleState (now, juce::dontSendNotification);
    songButton.setToggleState (! now, juce::dontSendNotification);
    mixAButton.setToggleState (! state.listening, juce::dontSendNotification);
    refBButton.setToggleState (state.listening, juce::dontSendNotification);
    const bool mixing = proc.getStage() == Stage::mixing;
    mixButton.setToggleState (mixing && ! trackMode, juce::dontSendNotification);
    masterButton.setToggleState (! mixing && ! trackMode, juce::dontSendNotification);
    trackButton.setToggleState (trackMode, juce::dontSendNotification);
    content.repaint();
}

// =====================================================================================
void WwmaAudioProcessorEditor::timerCallback()
{
    ++ticks;
    proc.updateReferenceState();
    readReferences();
    if (ticks % 15 == 0)
        pushHistory();
    refresh (ticks % 10 == 0);
}

void WwmaAudioProcessorEditor::pushHistory()
{
    auto& h = state.shortTermHistory;
    if (state.snap.secondsAnalysed > 0.0f)
    {
        h.push_back (state.snap.shortTerm);
        if (h.size() > historyPoints)
            h.erase (h.begin());
    }

    if (state.assessment.enoughData)
    {
        auto& s = state.scoreHistory;
        s.push_back (state.assessment.score);
        if (s.size() > scorePoints)
            s.erase (s.begin());
        if (s.size() >= 36)
        {
            auto mean = [&s] (size_t from, size_t to) { float sum = 0.0f; for (size_t i = from; i < to; ++i) sum += s[i]; return sum / (float) (to - from); };
            state.trend = mean (s.size() - 6, s.size()) - mean (s.size() - 36, s.size() - 24);
        }
        else
            state.trend = 0.0f;
    }
}

void WwmaAudioProcessorEditor::refresh (bool shouldReassess)
{
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        const auto status = proc.mixTrack.getStatus (0);
        if (status == ReferenceTracks::Status::ready && proc.mixTrack.getActive() != 0)
            proc.mixTrack.setActive (0);
        mixSeek.repaint();
        if ((int) status != mixStatusShown)
        {
            mixStatusShown = (int) status;
            updateTexts();
        }
    }

    proc.analyzer.readSnapshot (state.snap);

    // mastering EQ and limiter: what the user set, the limiter's meter, and the bypass level
    const auto settings = proc.getChainSettings();
    // with both stages off the input is the output; otherwise the input has its own analyser
    if (settings.anyOn())
        proc.inputAnalyzer.readSnapshot (state.inputSnap);
    else
        state.inputSnap = state.snap;
    const bool settingsChanged = ! settings.sameAs (state.chainSettings);
    state.chainSettings = settings;
    state.gainReduction = juce::jmax (proc.takeGainReduction(), state.gainReduction - 0.4f);
    if (state.snap.shortTerm > -70.0f && state.inputSnap.shortTerm > -70.0f)
        proc.withoutChainGainDb.store (juce::jlimit (-24.0f, 24.0f, state.snap.shortTerm - state.inputSnap.shortTerm));
    if (state.listeningWithout && ! settings.anyOn())
        setListeningWithout (false);
    if (settingsChanged)
    {
        content.repaint (0, 0, baseWidth, topBar);
        shouldReassess = true;
    }

    if (shouldReassess || state.profileName.isEmpty())
        reassess();

    if (state.assessment.enoughData)
        state.displayScore += 0.12f * (state.assessment.score - state.displayScore);

    // level matching for A/B: the reference plays as loud as the mix measures
    if (const auto* ref = state.activeRefView())
    {
        const auto& s = state.snap;
        const float mixLoudness = s.integrated > -70.0f ? s.integrated : (s.shortTerm > -70.0f ? s.shortTerm : ref->snap.integrated);
        state.refGainDb = juce::jlimit (-24.0f, 24.0f, mixLoudness - ref->snap.integrated);
        proc.referenceGainDb.store (state.refGainDb);
    }

    for (juce::Component* c : std::initializer_list<juce::Component*> { &loudness, &meters, &history, &compare, &tonal, &score, &translation, &progress })
        c->repaint();
    if (chainPanel.isVisible())
        chainPanel.repaint();
    if (state.listening)
        refBox.repaint();
    if (refDetail.isVisible())
        refDetail.repaint();
}

void WwmaAudioProcessorEditor::reassess()
{
    state.lang = proc.getLang();
    state.window = proc.getWindow();
    state.stage = proc.getStage();
    state.trackRole = proc.getTrackRole();
    const auto style = proc.getProfile();
    const bool trackMode = state.trackRole != TrackRole::mix;
    state.profile = trackMode ? trackProfile (state.trackRole, style) : style;
    state.profileName = proc.getProfileIndex() == proc.referenceIndex() ? tr (state.lang, "Reference", "Referenssi")
                                                                        : juce::String (style.name);
    if (trackMode)
        state.profileName = trackRoleName (state.trackRole, state.lang) + " (" + state.profileName + ")";
    state.targetSlot = proc.getProfileIndex() == proc.referenceIndex() ? proc.targetSlot() : -1;
    for (auto& r : state.refs)
        if (r.ready())
            r.assessment = assess (r.snap, style, Window::song, state.lang, state.stage);

    auto& bus = TrackBus::shared();
    bus.publish (proc.busId, printOf (state.snap, state.trackRole));
    state.markers.clear();
    if (trackMode)
    {
        const auto others = bus.others (proc.busId);
        const auto* ref = state.targetRefView();
        state.assessment = assessTrack (state.snap, style, state.trackRole, state.window, state.lang, others, ref != nullptr ? &ref->snap : nullptr);
        if (state.assessment.enoughData)
        {
            const auto& fine = state.window == Window::song ? state.snap.lowFineSong : state.snap.lowFineNow;
            const auto pk = state.trackRole == TrackRole::kick ? findKickNote (fine) : findLowPeak (fine, 30.0, 250.0);
            if (pk.found)
                state.markers.push_back ({ pk.hz, trackRoleName (state.trackRole, state.lang) + " " + noteName (pk.hz) + " " + juce::String (juce::roundToInt (pk.hz)) + " Hz", colours::accent });
            for (const auto& o : others)
                if (o.peakHz > 0.0f && o.role != state.trackRole)
                {
                    state.markers.push_back ({ o.peakHz, trackRoleName (o.role, state.lang) + " " + noteName (o.peakHz) + " " + juce::String (juce::roundToInt (o.peakHz)) + " Hz", colours::info });
                    break;
                }
        }
        state.chainAdvice = {};
    }
    else
    {
        state.assessment = assess (state.snap, state.profile, state.window, state.lang, state.stage);
        state.chainAdvice = adviseChain (state.inputSnap, state.profile, state.window, state.lang, state.stage, state.chainSettings);
    }
    chainPanel.refresh();

    updateStyleButton();
    if (state.compareOpen && ! state.snapshots.empty() && state.assessment.enoughData)
        state.changes = compareMetrics (state.snapshots.back().metrics, keyMetrics (state.assessment, state.profile, state.stage));
    else
        state.changes.clear();
    beforeAfter.repaint();
    if (state.learnMode)
        updateLesson();
    updateBaselineDiff();
    findings.refresh();
    findings.repaint();
}

void WwmaAudioProcessorEditor::updateStyleButton()
{
    const auto lang = state.lang;
    styleGuess = guessStyle (state.snap);
    const auto& all = builtInProfiles();
    const int current = proc.getProfileIndex();
    const bool usingPreset = current >= 0 && current < (int) all.size();

    // a loaded reference is the better target, and its legend uses this spot
    const bool show = styleGuess.valid && state.activeRefView() == nullptr && state.trackRole == TrackRole::mix;
    state.styleChipShown = show;
    styleButton.setVisible (show);
    styleSuggests = false;
    if (! show)
        return;

    const auto& best = all[(size_t) styleGuess.best];
    const auto percent = juce::String (juce::roundToInt (100.0f * styleGuess.familyConfidence)) + " %";
    const auto bpm = styleGuess.tempoFound ? juce::String (juce::roundToInt (styleGuess.bpm)) + " BPM" : juce::String();
    const bool fits = usingPreset && all[(size_t) current].family == best.family;
    const auto family = juce::String (best.family);
    const auto name = juce::String (best.name);

    if (fits)
    {
        styleButton.setButtonText ((bpm.isNotEmpty() ? bpm + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) : juce::String())
                                   + tr (lang, "sounds like ", "kuulostaa tyyliltä ") + family + " (" + percent + ")");
        styleSuggests = false;
        styleButton.setColour (juce::TextButton::buttonColourId, colours::panel);
        styleButton.setColour (juce::TextButton::textColourOffId, colours::soft);
        styleButton.setTooltip (tr (lang, "WWMA's guess from the tempo, tonal balance and dynamics agrees with your preset.",
                                          "WWMA:n arvaus tempon, taajuustasapainon ja dynamiikan perusteella vastaa valitsemaasi presettiä."));
    }
    else
    {
        styleButton.setButtonText (tr (lang, "Sounds like ", "Kuulostaa tyyliltä ") + family + " (" + percent + ")"
                                   + (bpm.isNotEmpty() ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + bpm : juce::String())
                                   + tr (lang, "  - use it?", "  - vaihdetaanko?"));
        styleSuggests = true;
        styleButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.14f));
        styleButton.setColour (juce::TextButton::textColourOffId, colours::accent);
        styleButton.setTooltip (tr (lang, "A guess from the tempo, tonal balance and dynamics, not a fact. Click to switch to the closest preset, ",
                                          "Arvaus tempon, taajuustasapainon ja dynamiikan perusteella, ei fakta. Klikkaa vaihtaaksesi lähimpään presettiin, ")
                                + name + tr (lang, ". Nothing changes unless you click. Styles with the same tempo are easy to mix up; your ears and your own choice win.",
                                                   ". Mikään ei muutu, ellet klikkaa. Samantempoiset tyylit menevät helposti sekaisin; korvasi ja oma valintasi ratkaisevat."));
    }
}

// =====================================================================================
void WwmaAudioProcessorEditor::loadSnapshots()
{
    state.snapshots = proc.getSnapshots();
    state.awaitingSaveConfirm = proc.latestSnapshotNeedsConfirmation();
    if (state.snapshots.empty())
        state.compareOpen = false;
    progress.syncButtons();
}

void WwmaAudioProcessorEditor::setBaselineFromCurrent()
{
    state.baselineExcess.clear();
    baselineTitles.clear();
    state.hasBaseline = state.assessment.enoughData;
    if (! state.hasBaseline)
        return;
    state.baselineScore = state.assessment.score;
    for (const auto& f : state.assessment.findings)
    {
        state.baselineExcess[f.id] = f.excess;
        baselineTitles[f.id] = f.title;
    }
}

void WwmaAudioProcessorEditor::saveSnapshot()
{
    if (! state.assessment.enoughData)
        return;
    SavedSnapshot snap;
    snap.time = juce::Time::getCurrentTime();
    snap.score = state.assessment.score;
    snap.metrics = toMap (keyMetrics (state.assessment, state.profile, state.stage));
    snap.spectrum.assign (state.assessment.measures.spectrum.begin(), state.assessment.measures.spectrum.end());
    proc.addSnapshot (snap);
    loadSnapshots();
    setBaselineFromCurrent();
    reassess();
    progress.repaint();
}

void WwmaAudioProcessorEditor::clearSnapshots()
{
    proc.clearScoreSnapshots();
    loadSnapshots();
    state.hasBaseline = false;
    state.baselineExcess.clear();
    baselineTitles.clear();
    reassess();
    progress.repaint();
}

void WwmaAudioProcessorEditor::confirmSnapshotSaved()
{
    proc.confirmLatestSnapshot();
    loadSnapshots();
    progress.repaint();
    beforeAfter.repaint();
}

void WwmaAudioProcessorEditor::setCompareOpen (bool open)
{
    state.compareOpen = open && ! state.snapshots.empty();
    if (state.compareOpen && state.learnMode)
        setLearnMode (false);
    if (state.compareOpen && state.chainOpen)
        setChainOpen (false);
    updatePanels();
    reassess();
}

void WwmaAudioProcessorEditor::setLearnMode (bool on)
{
    state.learnMode = on;
    proc.setLearnMode (on);
    learnButton.setToggleState (on, juce::dontSendNotification);
    if (on)
    {
        state.compareOpen = false;
        state.chainOpen = false;
        proc.setChainOpen (false);
        proc.inputAnalysisWanted.store (false);
    }
    updatePanels();
    lessonSeconds = -1.0f;
    state.lessonHeld = 0.0f;
    reassess();
}

void WwmaAudioProcessorEditor::updatePanels()
{
    // one panel at a time over the findings: before/after, lesson, or EQ + limiter
    beforeAfter.setVisible (state.compareOpen);
    lessonBox.setVisible (state.learnMode);
    chainPanel.setVisible (state.chainOpen);
    findings.setVisible (! state.compareOpen && ! state.learnMode && ! state.chainOpen);
    learnButton.setToggleState (state.learnMode, juce::dontSendNotification);
    chainButton.setToggleState (state.chainOpen, juce::dontSendNotification);
    progress.syncButtons();
}

void WwmaAudioProcessorEditor::setChainOpen (bool open)
{
    state.chainOpen = open;
    proc.setChainOpen (open);
    proc.inputAnalysisWanted.store (open);
    if (open)
    {
        state.compareOpen = false;
        if (state.learnMode)
        {
            state.learnMode = false;
            proc.setLearnMode (false);
        }
    }
    updatePanels();
    reassess();
}

void WwmaAudioProcessorEditor::setShowSuggestions (bool on)
{
    state.showSuggestions = on;
    proc.setShowSuggestions (on);
    chainPanel.refresh();
}

void WwmaAudioProcessorEditor::setListeningWithout (bool on)
{
    on = on && state.chainSettings.anyOn();
    state.listeningWithout = on;
    proc.listenWithoutChain.store (on);
    chainPanel.refresh();
    content.repaint (0, 0, baseWidth, topBar);
}

void WwmaAudioProcessorEditor::showLesson (int index)
{
    state.lessonIndex = (index + lessonCount()) % lessonCount();
    proc.setLessonIndex (state.lessonIndex);
    state.lessonHeld = 0.0f;
    lessonSeconds = -1.0f;
    reassess();
}

void WwmaAudioProcessorEditor::updateLesson()
{
    // lessons watch the last 3 seconds, so a looped part can be tweaked live
    const auto nowAssessment = assess (state.snap, state.profile, Window::now, state.lang, state.stage);
    state.lesson = checkLesson (state.lessonIndex, nowAssessment, state.profile, state.stage, state.lang);

    // the goal must hold for 3 seconds of music (not wall-clock time: a stopped transport doesn't count)
    const float t = state.snap.secondsAnalysed;
    if (state.lesson.ready && state.lesson.passed)
    {
        if (lessonSeconds >= 0.0f)
            state.lessonHeld += juce::jmax (0.0f, t - lessonSeconds);
    }
    else
        state.lessonHeld = 0.0f;
    lessonSeconds = t;

    const auto id = juce::String (lessonId (state.lessonIndex));
    if (state.lessonHeld >= 3.0f && ! state.lessonsDone.contains (id))
    {
        proc.markLessonDone (id);
        state.lessonsDone = proc.getLessonsDone();
    }
    lessonBox.repaint();
}

void WwmaAudioProcessorEditor::chooseReportFile()
{
    auto folder = proc.getReportFolder();
    folder.createDirectory();
    const auto name = "WWMA report " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M") + ".md";
    chooser = std::make_unique<juce::FileChooser> (tr (proc.getLang(), "Save the mix report", "Tallenna miksausraportti"),
                                                   folder.getChildFile (name), "*.md");
    juce::Component::SafePointer<WwmaAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File())
                                  return;
                              auto file = fc.getResult();
                              if (! file.hasFileExtension ("md"))
                                  file = file.withFileExtension ("md");
                              safe->saveReport (file);
                          });
}

juce::File WwmaAudioProcessorEditor::saveReport (const juce::File& markdownFile)
{
    const auto folder = markdownFile.getParentDirectory();
    folder.createDirectory();
    const auto jsonFile = markdownFile.withFileExtension ("json");

    // the newest earlier report in the same folder, to compare with
    juce::File previous;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
        if (f != jsonFile && (previous == juce::File() || f.getLastModificationTime() > previous.getLastModificationTime()))
            if (juce::JSON::parse (f).getProperty ("format", {}).toString() == "wwma-report-1")
                previous = f;

    ReportInput in;
    in.time = juce::Time::getCurrentTime();
    in.presetName = state.profileName;
    in.stage = state.stage;
    in.window = state.window;
    in.secondsAnalysed = state.snap.secondsAnalysed;
    in.tempoBpm = styleGuess.tempoFound ? styleGuess.bpm : 0.0f;
    if (styleGuess.valid && styleGuess.best >= 0)
        in.styleGuess = juce::String (builtInProfiles()[(size_t) styleGuess.best].family) + " ("
                        + juce::String (juce::roundToInt (100.0f * styleGuess.familyConfidence)) + " %)";
    in.assessment = state.assessment;
    in.profile = state.profile;
    in.snapshots = state.snapshots;
    in.chain = state.chainSettings;
    in.chainAdvice = state.chainAdvice;
    if (const auto* ref = state.activeRefView())
    {
        in.referenceName = ref->name;
        in.referenceAssessment = ref->assessment;
    }

    const auto prevJson = previous.existsAsFile() ? juce::JSON::parse (previous) : juce::var();
    markdownFile.replaceWithText (reportMarkdown (in, state.lang, prevJson, previous.getFileNameWithoutExtension()));
    jsonFile.replaceWithText (juce::JSON::toString (reportJson (in)));
    proc.setReportFolder (folder);
    return previous == juce::File() ? previous : previous.withFileExtension ("md");
}

void WwmaAudioProcessorEditor::updateBaselineDiff()
{
    state.fixedSinceBaseline.clear();
    if (! state.hasBaseline || ! state.assessment.enoughData)
        return;
    for (const auto& [id, title] : baselineTitles)
    {
        if (id == "good" || id == "listening")
            continue;
        const bool stillThere = std::any_of (state.assessment.findings.begin(), state.assessment.findings.end(),
                                             [&id] (const Finding& f) { return f.id == id; });
        if (! stillThere)
            state.fixedSinceBaseline.add (title);
    }
}

// =====================================================================================
void WwmaAudioProcessorEditor::readReferences()
{
    auto& refs = proc.refs;
    const int gen = refs.getGeneration();
    if (gen != refGeneration)
    {
        refGeneration = gen;
        for (int i = 0; i < (int) state.refs.size(); ++i)
        {
            auto& v = state.refs[(size_t) i];
            switch (refs.getStatus (i))
            {
                case ReferenceTracks::Status::empty:   v.status = ui::RefView::Status::empty; break;
                case ReferenceTracks::Status::loading: v.status = ui::RefView::Status::loading; break;
                case ReferenceTracks::Status::ready:   v.status = ui::RefView::Status::ready; break;
                case ReferenceTracks::Status::failed:  v.status = ui::RefView::Status::failed; break;
            }
            v.error = refs.getError (i);
            if (auto info = refs.getInfo (i))
            {
                v.name = info->name;
                v.seconds = info->seconds;
                v.snap = info->snap;
                v.wave = info->wave;
                std::tie (v.loopStart, v.loopEnd) = refs.getLoop (i);
                v.earlier = refs.isEarlier (i);
                v.assessment = assess (v.snap, state.profile, Window::song, state.lang, state.stage);
            }
            else if (v.status != ui::RefView::Status::loading)
            {
                v.name = {};
                v.wave.clear();
                v.earlier = false;
            }
        }
        state.activeRef = refs.getActive();
        fillProfileBox();
        refBox.repaint();
        refDetail.repaint();
        tonal.repaint();
        findings.refresh();

        const bool hasRef = state.activeRefView() != nullptr;
        refBButton.setEnabled (hasRef);
        compare.setVisible (hasRef);
        history.setVisible (! hasRef);
        if (! hasRef && state.listening)
            setListening (false);
        if (refDetail.isVisible() && ! state.refs[(size_t) juce::jlimit (0, (int) state.refs.size() - 1, refDetail.getSlot())].ready())
            closeReferenceDetail();
        content.repaint();
    }
    state.playPosition = refs.getPlayPosition();
    state.refPlaying = refs.isPlaying();
}

void WwmaAudioProcessorEditor::toggleReferencePlay (int slot)
{
    auto& refs = proc.refs;
    if (refs.getStatus (slot) != ReferenceTracks::Status::ready)
        return;
    if (state.listening && refs.isPlaying() && refs.getActive() == slot)
    {
        setListening (false); // pause: back to your mix, the position is kept
        return;
    }
    refs.setActive (slot);
    readReferences();
    refs.setPlaying (true);
    state.refPlaying = true;
    setListening (true);
}

void WwmaAudioProcessorEditor::openReferenceDetail (int slot)
{
    if (proc.refs.getStatus (slot) != ReferenceTracks::Status::ready)
        return;
    proc.refs.setActive (slot);
    readReferences();
    refDetail.setSlot (slot);
    refDetail.setVisible (true);
    refDetail.toFront (false);
    styleButton.setVisible (false);
}

void WwmaAudioProcessorEditor::closeReferenceDetail()
{
    refDetail.setVisible (false);
    updateStyleButton();
}

void WwmaAudioProcessorEditor::setListening (bool b)
{
    b = b && state.activeRefView() != nullptr;
    if (! b)
    {
        // A, or pause: the reference's own playback stops too
        proc.refs.setPlaying (false);
        state.refPlaying = false;
    }
    state.listening = b;
    proc.listenToReference.store (b);
    mixAButton.setToggleState (! b, juce::dontSendNotification);
    refBButton.setToggleState (b, juce::dontSendNotification);
    // the panels show the reference's numbers while B plays, the mix's otherwise
    findings.refresh();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loudness, &tonal, &score, &translation })
        c->repaint();
    content.repaint();
    refBox.repaint();
}

void WwmaAudioProcessorEditor::browseReference (int slot)
{
    chooser = std::make_unique<juce::FileChooser> (tr (state.lang, "Choose a reference track", "Valitse referenssiraita"),
                                                   juce::File(), proc.refs.wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  loadReference (slot, file);
                          });
}

void WwmaAudioProcessorEditor::loadReference (int slot, const juce::File& file)
{
    if (slot < 0)
    {
        // the first free slot, else replace the active one
        slot = -1;
        for (int i = 0; i < ReferenceTracks::numSlots && slot < 0; ++i)
            if (proc.refs.getStatus (i) == ReferenceTracks::Status::empty || proc.refs.getStatus (i) == ReferenceTracks::Status::failed)
                slot = i;
        if (slot < 0)
            slot = juce::jmax (0, proc.refs.getActive());
    }
    proc.loadReference (slot, file);
    readReferences();
}

void WwmaAudioProcessorEditor::MixSeekBar::paint (juce::Graphics& g)
{
    const bool ready = proc.mixTrack.getStatus (0) == ReferenceTracks::Status::ready;
    if (proc.mixTrack.isPlaying())
        shown = proc.mixTrack.getPlayPosition();
    auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
    g.setColour (colours::raised);
    g.fillRoundedRectangle (r, 2.0f);
    if (ready)
    {
        g.setColour (colours::accent);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (3.0f, r.getWidth() * (float) juce::jlimit (0.0, 1.0, shown))), 2.0f);
    }
}

void WwmaAudioProcessorEditor::MixSeekBar::jump (const juce::MouseEvent& e)
{
    if (proc.mixTrack.getStatus (0) != ReferenceTracks::Status::ready)
        return;
    shown = juce::jlimit (0.0, 1.0, (double) e.position.x / (double) juce::jmax (1, getWidth()));
    proc.mixTrack.setCue (shown);
    repaint();
}

void WwmaAudioProcessorEditor::browseMix()
{
    chooser = std::make_unique<juce::FileChooser> (tr (state.lang, "Choose your mix", "Valitse miksisi"), juce::File(), proc.refs.wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto file = fc.getResult(); file.existsAsFile())
                                  loadMix (file);
                          });
}

void WwmaAudioProcessorEditor::loadMix (const juce::File& file)
{
    proc.loadMixFile (file);
    mixPlayButton.setToggleState (false, juce::dontSendNotification);
    mixSeek.shown = 0.0;
    state.scoreHistory.clear();
    state.shortTermHistory.clear();
    updateTexts();
}

bool WwmaAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (proc.refs.canRead (juce::File (f)))
            return true;
    return false;
}

void WwmaAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    state.dragOver = true;
    refBox.repaint();
}

void WwmaAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    state.dragOver = false;
    refBox.repaint();
}

void WwmaAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    state.dragOver = false;
    // standalone: a file dropped on the top bar is your own mix, anywhere else a reference
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone && mixLoadButton.getBounds().expanded (40, 20).contains (content.getLocalPoint (this, juce::Point<int> (x, y))))
    {
        for (const auto& f : files)
            if (proc.refs.canRead (juce::File (f)))
            {
                loadMix (juce::File (f));
                return;
            }
    }
    int slot = refBox.slotAt (refBox.getLocalPoint (this, juce::Point<int> (x, y)));
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! proc.refs.canRead (file))
            continue;
        loadReference (slot, file);
        slot = -1; // a second file goes to the next free slot
    }
    refBox.repaint();
}
