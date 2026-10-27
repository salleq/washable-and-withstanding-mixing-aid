#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace wwma;

namespace
{
    const juce::Identifier windowId ("window"), langId ("lang"), autoResetId ("autoReset"), referenceId ("Reference"),
                           stageId ("stage"), targetId ("target"), snapshotsId ("Snapshots"), pointId ("Point"),
                           refsId ("ReferenceTracks"), slotId ("Slot");

    Lang defaultLang() { return Lang::english; } // Finnish is one click away
}

WwmaAudioProcessor::WwmaAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WWMA", createLayout())
{
    apvts.state.setProperty (windowId, 1, nullptr);
    apvts.state.setProperty (langId, defaultLang() == Lang::finnish ? 1 : 0, nullptr);
    apvts.state.setProperty (autoResetId, true, nullptr);
    apvts.state.setProperty (stageId, 1, nullptr);
    apvts.state.setProperty (targetId, 0, nullptr);
    analyzer.prepare (48000.0);
}

juce::AudioProcessorValueTreeState::ParameterLayout WwmaAudioProcessor::createLayout()
{
    juce::StringArray names;
    for (const auto& p : builtInProfiles())
        names.add (p.name);
    names.add ("Reference track");

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { profileParamId, 1 }, "Style profile",
                                                              names, defaultProfileIndex()));
    return layout;
}

bool WwmaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void WwmaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analyzer.prepare (sampleRate);
    refs.setPlaybackRate (sampleRate);
    refScratch.setSize (2, juce::jmax (512, samplesPerBlock), false, false, true);
    abStep = (float) (1.0 / (0.03 * sampleRate)); // 30 ms crossfade between mix and reference
    wasPlaying = false;
}

void WwmaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Never touch the audio. Clear only output channels that have no input.
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    if (n == 0 || numCh == 0)
        return;

    int64_t hostSample = -1;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (hostPlaying && ! wasPlaying && autoReset.load())
                analyzer.requestReset();
            wasPlaying = hostPlaying;
            if (auto t = pos->getTimeInSamples())
                hostSample = *t;
        }
    }

    // measure the mix first: the analysis never hears the reference
    const float* l = buffer.getReadPointer (0);
    const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
    analyzer.process (l, r, n);

    // A/B listening, only when the user asked for it
    const bool listen = listenToReference.load();
    if (! listen && abMix <= 0.0f)
        return;

    const float gainDb = referenceGainDb.load();
    for (int start = 0; start < n;)
    {
        const int len = juce::jmin (n - start, refScratch.getNumSamples());
        float* refL = refScratch.getWritePointer (0);
        float* refR = refScratch.getWritePointer (1);
        const bool playing = refs.render (refL, refR, len, hostSample >= 0 ? hostSample + start : -1, hostPlaying, gainDb);
        if (! playing)
            refScratch.clear (0, len);

        float* outL = buffer.getWritePointer (0, start);
        float* outR = numCh > 1 ? buffer.getWritePointer (1, start) : nullptr;
        const float target = listen ? 1.0f : 0.0f;
        for (int i = 0; i < len; ++i)
        {
            abMix = abMix < target ? juce::jmin (target, abMix + abStep) : juce::jmax (target, abMix - abStep);
            if (outR != nullptr)
            {
                outL[i] += abMix * (refL[i] - outL[i]);
                outR[i] += abMix * (refR[i] - outR[i]);
            }
            else
                outL[i] += abMix * (0.5f * (refL[i] + refR[i]) - outL[i]);
        }
        start += len;
    }
}

// ---- profiles

bool WwmaAudioProcessor::hasReference() const
{
    return reference.has_value() || refs.getStatus (refs.getActive()) == ReferenceTracks::Status::ready;
}

int WwmaAudioProcessor::getProfileIndex() const
{
    const int i = (int) apvts.getRawParameterValue (profileParamId)->load();
    if (i == referenceIndex() && ! hasReference())
        return defaultProfileIndex();
    return juce::jlimit (0, getNumProfiles() - 1, i);
}

void WwmaAudioProcessor::setProfileIndex (int index)
{
    auto* p = apvts.getParameter (profileParamId);
    p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, getNumProfiles() - 1, index)));
}

Profile WwmaAudioProcessor::getProfile() const
{
    const int i = getProfileIndex();
    Profile p = builtInProfiles()[(size_t) juce::jlimit (0, (int) builtInProfiles().size() - 1, i)];
    if (i == referenceIndex())
    {
        // the active reference track, or else a reference learned by listening
        if (auto info = refs.getInfo (refs.getActive()); info != nullptr && refs.getStatus (refs.getActive()) == ReferenceTracks::Status::ready)
            p = profileFromSnapshot (info->snap, info->name.toStdString());
        else if (reference.has_value())
            p = *reference;
    }

    struct Target { float lo, hi, tp; };
    static const Target targets[] = {
        { 0, 0, 0 },                 // the style's own
        { -15.0f, -13.0f, -1.0f },   // streaming (-14 LUFS)
        { -9.0f, -7.0f, -0.5f },     // club / DJ
        { -7.0f, -5.0f, -0.3f },     // loud
        { -17.0f, -15.0f, -1.0f },   // Apple Music (-16 LUFS)
    };
    const int t = getTarget();
    if (t > 0 && t < (int) std::size (targets))
    {
        p.lufsMin = targets[t].lo;
        p.lufsMax = targets[t].hi;
        p.truePeakMax = targets[t].tp;
    }
    return p;
}

juce::StringArray WwmaAudioProcessor::targetNames (Lang lang)
{
    return { tr (lang, "Style's own", "Tyylin oma"),
             tr (lang, "-14 LUFS streaming", "-14 LUFS striimaus"),
             tr (lang, "-8 LUFS club", "-8 LUFS klubi"),
             tr (lang, "-6 LUFS loud", "-6 LUFS kova"),
             tr (lang, "-16 LUFS Apple Music", "-16 LUFS Apple Music") };
}

int WwmaAudioProcessor::getTarget() const { return (int) apvts.state.getProperty (targetId, 0); }
void WwmaAudioProcessor::setTarget (int t) { apvts.state.setProperty (targetId, t, nullptr); }
Stage WwmaAudioProcessor::getStage() const { return (int) apvts.state.getProperty (stageId, 1) == 0 ? Stage::mixing : Stage::mastering; }
void WwmaAudioProcessor::setStage (Stage st) { apvts.state.setProperty (stageId, st == Stage::mixing ? 0 : 1, nullptr); }

std::vector<SavedSnapshot> WwmaAudioProcessor::getSnapshots() const
{
    std::vector<SavedSnapshot> out;
    for (const auto& c : apvts.state.getChildWithName (snapshotsId))
    {
        SavedSnapshot s;
        s.time = juce::Time ((juce::int64) c.getProperty ("t"));
        s.score = (float) c.getProperty ("score");
        s.confirmed = (bool) c.getProperty ("saved", false);
        s.confirmedAt = juce::Time ((juce::int64) c.getProperty ("savedAt", 0));
        for (const auto& m : c.getChildWithName ("Metrics"))
            s.metrics[m.getProperty ("id").toString().toStdString()] = { (float) m.getProperty ("v"), (float) m.getProperty ("o") };
        out.push_back (s);
    }
    return out;
}

void WwmaAudioProcessor::addSnapshot (const SavedSnapshot& s)
{
    auto list = apvts.state.getOrCreateChildWithName (snapshotsId, nullptr);
    juce::ValueTree point (pointId);
    point.setProperty ("t", s.time.toMilliseconds(), nullptr);
    point.setProperty ("score", s.score, nullptr);
    point.setProperty ("saved", s.confirmed, nullptr);
    juce::ValueTree metrics ("Metrics");
    for (const auto& [id, vo] : s.metrics)
    {
        juce::ValueTree m ("M");
        m.setProperty ("id", juce::String (id), nullptr);
        m.setProperty ("v", vo.first, nullptr);
        m.setProperty ("o", vo.second, nullptr);
        metrics.addChild (m, -1, nullptr);
    }
    point.addChild (metrics, -1, nullptr);
    list.addChild (point, -1, nullptr);
    while (list.getNumChildren() > 40)
        list.removeChild (0, nullptr);
}

void WwmaAudioProcessor::confirmLatestSnapshot()
{
    auto list = apvts.state.getChildWithName (snapshotsId);
    if (list.isValid() && list.getNumChildren() > 0)
    {
        auto last = list.getChild (list.getNumChildren() - 1);
        last.setProperty ("saved", true, nullptr);
        last.setProperty ("savedAt", juce::Time::getCurrentTime().toMilliseconds(), nullptr);
    }
}

bool WwmaAudioProcessor::latestSnapshotNeedsConfirmation() const
{
    const auto list = apvts.state.getChildWithName (snapshotsId);
    return list.isValid() && list.getNumChildren() > 0 && ! (bool) list.getChild (list.getNumChildren() - 1).getProperty ("saved", false);
}

bool WwmaAudioProcessor::getLearnMode() const { return (bool) apvts.state.getProperty ("learn", false); }
void WwmaAudioProcessor::setLearnMode (bool on) { apvts.state.setProperty ("learn", on, nullptr); }
int WwmaAudioProcessor::getLessonIndex() const { return (int) apvts.state.getProperty ("lesson", 0); }
void WwmaAudioProcessor::setLessonIndex (int i) { apvts.state.setProperty ("lesson", i, nullptr); }

juce::StringArray WwmaAudioProcessor::getLessonsDone() const
{
    return juce::StringArray::fromTokens (apvts.state.getProperty ("lessonsDone", "").toString(), ",", "");
}

void WwmaAudioProcessor::markLessonDone (const juce::String& id)
{
    auto done = getLessonsDone();
    done.removeEmptyStrings();
    done.addIfNotAlreadyThere (id);
    apvts.state.setProperty ("lessonsDone", done.joinIntoString (","), nullptr);
}

juce::File WwmaAudioProcessor::getReportFolder() const
{
    const auto path = apvts.state.getProperty ("reportFolder", "").toString();
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        return juce::File (path);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WWMA Reports");
}

void WwmaAudioProcessor::setReportFolder (const juce::File& f) { apvts.state.setProperty ("reportFolder", f.getFullPathName(), nullptr); }

void WwmaAudioProcessor::clearScoreSnapshots()
{
    if (auto list = apvts.state.getChildWithName (snapshotsId); list.isValid())
        apvts.state.removeChild (list, nullptr);
}

void WwmaAudioProcessor::learnReference (const Snapshot& s)
{
    reference = profileFromSnapshot (s, "Reference (learned)");

    juce::ValueTree t (referenceId);
    juce::String target;
    for (auto v : reference->target)
        target << juce::String (v, 2) << " ";
    t.setProperty ("target", target.trim(), nullptr);
    t.setProperty ("lufsMin", reference->lufsMin, nullptr);
    t.setProperty ("lufsMax", reference->lufsMax, nullptr);
    t.setProperty ("truePeakMax", reference->truePeakMax, nullptr);
    t.setProperty ("plrMin", reference->plrMin, nullptr);
    t.setProperty ("plrMax", reference->plrMax, nullptr);
    t.setProperty ("lraMax", reference->lraMax, nullptr);
    t.setProperty ("lowWidthMax", reference->lowWidthMax, nullptr);
    t.setProperty ("widthMax", reference->widthMax, nullptr);
    t.setProperty ("widthMin", reference->widthMin, nullptr);
    t.setProperty ("lowContrastMin", reference->lowContrastMin, nullptr);
    t.setProperty ("highContrastMin", reference->highContrastMin, nullptr);
    t.setProperty ("highContrastMax", reference->highContrastMax, nullptr);

    if (auto old = apvts.state.getChildWithName (referenceId); old.isValid())
        apvts.state.removeChild (old, nullptr);
    apvts.state.addChild (t, -1, nullptr);

    setProfileIndex (referenceIndex());
}

void WwmaAudioProcessor::restoreReference()
{
    reference.reset();
    auto t = apvts.state.getChildWithName (referenceId);
    if (! t.isValid())
        return;

    juce::StringArray values;
    values.addTokens (t.getProperty ("target").toString(), " ", "");
    values.removeEmptyStrings();
    if (values.size() != Bands::count)
        return;

    Profile p;
    p.name = "Reference (learned)";
    for (int b = 0; b < Bands::count; ++b)
        p.target[(size_t) b] = values[b].getFloatValue();
    auto get = [&t] (const char* key, float fallback) { return (float) t.getProperty (key, fallback); };
    p.lufsMin = get ("lufsMin", p.lufsMin);
    p.lufsMax = get ("lufsMax", p.lufsMax);
    p.truePeakMax = get ("truePeakMax", p.truePeakMax);
    p.plrMin = get ("plrMin", p.plrMin);
    p.plrMax = get ("plrMax", p.plrMax);
    p.lraMax = get ("lraMax", p.lraMax);
    p.lowWidthMax = get ("lowWidthMax", p.lowWidthMax);
    p.widthMax = get ("widthMax", p.widthMax);
    p.widthMin = get ("widthMin", p.widthMin);
    p.lowContrastMin = get ("lowContrastMin", p.lowContrastMin);
    p.highContrastMin = get ("highContrastMin", p.highContrastMin);
    p.highContrastMax = get ("highContrastMax", p.highContrastMax);
    reference = p;
}

// ---- view settings

Window WwmaAudioProcessor::getWindow() const { return (int) apvts.state.getProperty (windowId, 1) == 0 ? Window::now : Window::song; }
void WwmaAudioProcessor::setWindow (Window w) { apvts.state.setProperty (windowId, w == Window::now ? 0 : 1, nullptr); }
Lang WwmaAudioProcessor::getLang() const { return (int) apvts.state.getProperty (langId, 0) == 1 ? Lang::finnish : Lang::english; }
void WwmaAudioProcessor::setLang (Lang l) { apvts.state.setProperty (langId, l == Lang::finnish ? 1 : 0, nullptr); }

void WwmaAudioProcessor::setAutoReset (bool on)
{
    autoReset.store (on);
    apvts.state.setProperty (autoResetId, on, nullptr);
}

// ---- programs = style profiles, so the host's preset browser can pick them

int WwmaAudioProcessor::getNumPrograms() { return (int) builtInProfiles().size(); }
int WwmaAudioProcessor::getCurrentProgram() { return juce::jmin (getProfileIndex(), getNumPrograms() - 1); }
void WwmaAudioProcessor::setCurrentProgram (int index) { setProfileIndex (juce::jlimit (0, getNumPrograms() - 1, index)); }

const juce::String WwmaAudioProcessor::getProgramName (int index)
{
    const auto& all = builtInProfiles();
    return juce::isPositiveAndBelow (index, (int) all.size()) ? juce::String (all[(size_t) index].name) : juce::String();
}

// ---- state

void WwmaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void WwmaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            autoReset.store ((bool) apvts.state.getProperty (autoResetId, true));
            restoreReference();
            restoreReferenceTracks();
        }
    }
}

// ---- reference tracks

void WwmaAudioProcessor::loadReference (int slot, const juce::File& file)
{
    refs.load (slot, file);
}

void WwmaAudioProcessor::removeReference (int slot)
{
    refs.remove (slot);
    listenToReference.store (listenToReference.load() && hasReference());
    updateReferenceState();
}

void WwmaAudioProcessor::restoreReferenceTracks()
{
    auto t = apvts.state.getChildWithName (refsId);
    if (! t.isValid())
        return;
    wantedActiveRef = (int) t.getProperty ("active", -1);
    for (const auto& c : t)
    {
        const int slot = (int) c.getProperty ("index", -1);
        const juce::File file (c.getProperty ("path").toString());
        if (juce::isPositiveAndBelow (slot, ReferenceTracks::numSlots) && file.existsAsFile())
            refs.load (slot, file);
    }
    savedRefGeneration = refs.getGeneration();
}

void WwmaAudioProcessor::updateReferenceState()
{
    if (wantedActiveRef >= 0)
    {
        const auto st = refs.getStatus (wantedActiveRef);
        if (st == ReferenceTracks::Status::ready)
            refs.setActive (wantedActiveRef);
        if (st != ReferenceTracks::Status::loading)
            wantedActiveRef = -1;
    }

    // keep the saved state in step (only slots that loaded, so a missing file is not forgotten while loading)
    if (refs.getGeneration() == savedRefGeneration)
        return;
    bool anyLoading = false;
    for (int i = 0; i < ReferenceTracks::numSlots; ++i)
        anyLoading = anyLoading || refs.getStatus (i) == ReferenceTracks::Status::loading;
    if (anyLoading)
        return;
    savedRefGeneration = refs.getGeneration();

    juce::ValueTree t (refsId);
    t.setProperty ("active", refs.getActive(), nullptr);
    for (int i = 0; i < ReferenceTracks::numSlots; ++i)
        if (auto info = refs.getInfo (i))
        {
            juce::ValueTree c (slotId);
            c.setProperty ("index", i, nullptr);
            c.setProperty ("path", info->file.getFullPathName(), nullptr);
            t.addChild (c, -1, nullptr);
        }
    if (auto old = apvts.state.getChildWithName (refsId); old.isValid())
        apvts.state.removeChild (old, nullptr);
    apvts.state.addChild (t, -1, nullptr);
}

juce::AudioProcessorEditor* WwmaAudioProcessor::createEditor() { return new WwmaAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WwmaAudioProcessor(); }
