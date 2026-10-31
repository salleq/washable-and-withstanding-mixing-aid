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
    inputAnalyzer.prepare (48000.0);

    using Ids = ChainIds;
    chainParams.eqOn = apvts.getRawParameterValue (Ids::eqOn);
    for (size_t b = 0; b < 4; ++b)
    {
        chainParams.freq[b] = apvts.getRawParameterValue (Ids::freq[b]);
        chainParams.gain[b] = apvts.getRawParameterValue (Ids::gain[b]);
        if (juce::String (Ids::q[b]).isNotEmpty())
            chainParams.q[b] = apvts.getRawParameterValue (Ids::q[b]);
    }
    chainParams.limOn = apvts.getRawParameterValue (Ids::limOn);
    chainParams.limGain = apvts.getRawParameterValue (Ids::limGain);
    chainParams.limCeiling = apvts.getRawParameterValue (Ids::limCeiling);
    chainParams.limRelease = apvts.getRawParameterValue (Ids::limRelease);

    chain.prepare (48000.0, chainBlock);
    refScratch.setSize (2, chainBlock);
    setLatencySamples (chain.latency());
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

    // Mastering EQ and limiter: both off and flat by default. Only the user moves these.
    using Ids = ChainIds;
    auto hz = [] (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, 1.0f);
        r.setSkewForCentre (centre);
        return r;
    };
    auto hzText = juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
        [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " k" : juce::String (juce::roundToInt (v)); });
    auto dbText = juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
        [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1); });

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { Ids::eqOn, 1 }, "EQ on", false));
    const char* bandNames[] = { "EQ low shelf", "EQ low mid", "EQ high mid", "EQ high shelf" };
    const juce::NormalisableRange<float> freqRanges[] = { hz (20.0f, 300.0f, 80.0f), hz (60.0f, 1500.0f, 300.0f),
                                                          hz (400.0f, 12000.0f, 2500.0f), hz (2000.0f, 20000.0f, 8000.0f) };
    const ChainSettings defaults;
    for (size_t b = 0; b < 4; ++b)
    {
        const juce::String name (bandNames[b]);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { Ids::freq[b], 1 }, name + " freq",
                                                                 freqRanges[b], defaults.freq[b], hzText));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { Ids::gain[b], 1 }, name + " gain",
                                                                 juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbText));
        if (juce::String (Ids::q[b]).isNotEmpty())
        {
            juce::NormalisableRange<float> qr (0.3f, 4.0f, 0.01f);
            qr.setSkewForCentre (1.0f);
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { Ids::q[b], 1 }, name + " Q", qr, defaults.q[b]));
        }
    }

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { Ids::limOn, 1 }, "Limiter on", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { Ids::limGain, 1 }, "Limiter gain",
                                                             juce::NormalisableRange<float> (-6.0f, 24.0f, 0.1f), 0.0f, dbText));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { Ids::limCeiling, 1 }, "Limiter ceiling",
                                                             juce::NormalisableRange<float> (-3.0f, 0.0f, 0.1f), -1.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dBTP")));
    juce::NormalisableRange<float> rel (10.0f, 500.0f, 1.0f);
    rel.setSkewForCentre (100.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { Ids::limRelease, 1 }, "Limiter release", rel, 100.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("ms")));
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
    inputAnalyzer.prepare (sampleRate);
    refs.setPlaybackRate (sampleRate);
    mixTrack.setPlaybackRate (sampleRate);
    refScratch.setSize (2, juce::jmax (512, samplesPerBlock), false, false, true);
    chainBlock = juce::jmax (512, samplesPerBlock);
    chain.prepare (sampleRate, chainBlock);
    setLatencySamples (chain.latency());
    withoutMix = 0.0f;
    abStep = (float) (1.0 / (0.03 * sampleRate)); // 30 ms crossfade between mix and reference
    wasPlaying = false;
}

void WwmaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear only output channels that have no input.
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    if (n == 0 || numCh == 0)
        return;

    if (wrapperType == wrapperType_Standalone)
    {
        // the interface input is not used: your mix is the file loaded into the app, while it plays
        for (int c = 0; c < numCh; ++c)
            buffer.clear (c, 0, n);
        if (mixTrack.isPlaying())
        {
            float* l = buffer.getWritePointer (0);
            mixTrack.render (l, numCh > 1 ? buffer.getWritePointer (1) : l, n, -1, false, 0.0f);
        }
    }

    int64_t hostSample = -1;
    bool hostPlaying = false;
    // The standalone app has no transport (its play head reports a sample count but never
    // "playing"), so there a reference simply plays on its own, as with no play head at all.
    auto* ph = wrapperType == wrapperType_Standalone ? nullptr : getPlayHead();
    if (ph != nullptr)
    {
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (hostPlaying && ! wasPlaying && autoReset.load())
            {
                analyzer.requestReset();
                inputAnalyzer.requestReset();
            }
            wasPlaying = hostPlaying;
            if (auto t = pos->getTimeInSamples())
                hostSample = *t;
        }
    }

    // mastering EQ and limiter, exactly as the user set them (a fixed delay when both are off)
    const auto settings = getChainSettings();
    const bool without = listenWithoutChain.load() && settings.anyOn();
    const float withoutGain = juce::Decibels::decibelsToGain (withoutChainGainDb.load());
    const float withoutStep = (float) (1.0 / (0.03 * getSampleRate()));
    const bool measureInput = settings.anyOn() || inputAnalysisWanted.load();
    if (measureInput && ! inputRunning)
        inputAnalyzer.requestReset(); // start fresh, not from whatever it heard last time
    inputRunning = measureInput;
    for (int start = 0; start < n; start += chainBlock)
    {
        const int len = juce::jmin (chainBlock, n - start);
        float* l = buffer.getWritePointer (0, start);
        float* r = numCh > 1 ? buffer.getWritePointer (1, start) : l;

        // the suggestions come from the input, the meters and advice from what goes out
        if (measureInput)
            inputAnalyzer.process (l, r, len);
        if (r == l)
        {
            // mono: run the chain on a copy for the right side, keep the left
            float* tmp = refScratch.getWritePointer (1);
            std::copy (l, l + len, tmp);
            chain.process (l, tmp, len, settings);
        }
        else
            chain.process (l, r, len, settings);
        analyzer.process (l, r, len);

        // level-matched bypass, only when the user asked for it
        if (without || withoutMix > 0.0f)
        {
            const float* dl = chain.dryLeft();
            const float* dr = chain.dryRight();
            const float target = without ? 1.0f : 0.0f;
            for (int i = 0; i < len; ++i)
            {
                withoutMix = withoutMix < target ? juce::jmin (target, withoutMix + withoutStep) : juce::jmax (target, withoutMix - withoutStep);
                l[i] += withoutMix * (dl[i] * withoutGain - l[i]);
                if (r != l)
                    r[i] += withoutMix * (dr[i] * withoutGain - r[i]);
            }
        }
    }

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

// ---- mastering EQ and limiter

ChainSettings WwmaAudioProcessor::getChainSettings() const
{
    ChainSettings s;
    const auto& p = chainParams;
    s.eqOn = p.eqOn->load() >= 0.5f;
    for (size_t b = 0; b < 4; ++b)
    {
        s.freq[b] = p.freq[b]->load();
        s.gain[b] = p.gain[b]->load();
        if (p.q[b] != nullptr)
            s.q[b] = p.q[b]->load();
    }
    s.limiterOn = p.limOn->load() >= 0.5f;
    s.limiterGain = p.limGain->load();
    s.ceiling = p.limCeiling->load();
    s.releaseMs = p.limRelease->load();
    return s;
}

bool WwmaAudioProcessor::getShowSuggestions() const { return (bool) apvts.state.getProperty ("suggestions", true); }
void WwmaAudioProcessor::setShowSuggestions (bool on) { apvts.state.setProperty ("suggestions", on, nullptr); }
bool WwmaAudioProcessor::getChainOpen() const { return (bool) apvts.state.getProperty ("chainOpen", false); }
void WwmaAudioProcessor::setChainOpen (bool on) { apvts.state.setProperty ("chainOpen", on, nullptr); }

// ---- profiles

int WwmaAudioProcessor::targetSlot() const
{
    // the target is built from a real reference, never from a track marked as the user's earlier version
    auto usable = [this] (int i)
    {
        return juce::isPositiveAndBelow (i, ReferenceTracks::numSlots) && refs.getStatus (i) == ReferenceTracks::Status::ready && ! refs.isEarlier (i);
    };
    if (usable (refs.getActive()))
        return refs.getActive();
    for (int i = 0; i < ReferenceTracks::numSlots; ++i)
        if (usable (i))
            return i;
    return -1;
}

bool WwmaAudioProcessor::hasReference() const
{
    return reference.has_value() || targetSlot() >= 0;
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
        if (const int slot = targetSlot(); slot >= 0)
        {
            if (auto info = refs.getInfo (slot))
                p = profileFromSnapshot (info->snap, info->name.toStdString());
        }
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
        const auto spec = juce::StringArray::fromTokens (c.getProperty ("spec").toString(), " ", "");
        if (spec.size() == Bands::count)
            for (const auto& v : spec)
                s.spectrum.push_back (v.getFloatValue());
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
    if (s.spectrum.size() == (size_t) Bands::count)
    {
        juce::StringArray spec;
        for (auto v : s.spectrum)
            spec.add (juce::String (v, 2));
        point.setProperty ("spec", spec.joinIntoString (" "), nullptr);
    }
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

juce::File WwmaAudioProcessor::getMixFile() const
{
    const auto path = apvts.state.getProperty ("mixFile", {}).toString();
    return path.isEmpty() ? juce::File() : juce::File (path);
}

void WwmaAudioProcessor::loadMixFile (const juce::File& file)
{
    mixTrack.setPlaying (false);
    mixTrack.load (0, file);
    apvts.state.setProperty ("mixFile", file.getFullPathName(), nullptr);
    analyzer.requestReset();
    inputAnalyzer.requestReset();
}

bool WwmaAudioProcessor::getShowDifference() const { return (bool) apvts.state.getProperty ("ghostDiff", true); }
void WwmaAudioProcessor::setShowDifference (bool on) { apvts.state.setProperty ("ghostDiff", on, nullptr); }
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
        {
            refs.load (slot, file);
            wantedLoops[(size_t) slot] = { (double) c.getProperty ("loopA", 0.0), (double) c.getProperty ("loopB", 0.0) };
            wantedEarlier[(size_t) slot] = (bool) c.getProperty ("earlier", false);
        }
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

    for (int i = 0; i < ReferenceTracks::numSlots; ++i)
    {
        auto& wl = wantedLoops[(size_t) i];
        if (wl.second <= wl.first || refs.getStatus (i) == ReferenceTracks::Status::loading)
            continue;
        if (refs.getStatus (i) == ReferenceTracks::Status::ready)
            refs.setLoop (i, wl.first, wl.second);
        wl = {};
    }
    for (int i = 0; i < ReferenceTracks::numSlots; ++i)
    {
        if (! wantedEarlier[(size_t) i] || refs.getStatus (i) == ReferenceTracks::Status::loading)
            continue;
        if (refs.getStatus (i) == ReferenceTracks::Status::ready)
            refs.setEarlier (i, true);
        wantedEarlier[(size_t) i] = false;
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
            if (refs.isEarlier (i))
                c.setProperty ("earlier", true, nullptr);
            if (refs.hasLoop (i))
            {
                c.setProperty ("loopA", refs.getLoop (i).first, nullptr);
                c.setProperty ("loopB", refs.getLoop (i).second, nullptr);
            }
            t.addChild (c, -1, nullptr);
        }
    if (auto old = apvts.state.getChildWithName (refsId); old.isValid())
        apvts.state.removeChild (old, nullptr);
    apvts.state.addChild (t, -1, nullptr);
}

juce::AudioProcessorEditor* WwmaAudioProcessor::createEditor() { return new WwmaAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WwmaAudioProcessor(); }
