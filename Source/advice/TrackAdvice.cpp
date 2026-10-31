#include "TrackAdvice.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
juce::String trackRoleName (TrackRole r, Lang lang)
{
    switch (r)
    {
        case TrackRole::mix:      return tr (lang, "Whole mix", "Koko miksi");
        case TrackRole::kick:     return tr (lang, "Kick", "Kick");
        case TrackRole::bass:     return tr (lang, "Bass", "Basso");
        case TrackRole::kickBass: return tr (lang, "Kick + bass bus", "Kick + basso -väylä");
    }
    return {};
}

// =====================================================================================
TrackBus& TrackBus::shared()
{
    static TrackBus bus;
    return bus;
}

int TrackBus::join()
{
    std::lock_guard<std::mutex> g (lock);
    const int id = nextId++;
    prints[id] = {};
    return id;
}

void TrackBus::leave (int id)
{
    std::lock_guard<std::mutex> g (lock);
    prints.erase (id);
}

void TrackBus::publish (int id, const TrackPrint& p)
{
    std::lock_guard<std::mutex> g (lock);
    prints[id] = p;
}

std::vector<TrackPrint> TrackBus::others (int id) const
{
    std::vector<TrackPrint> out;
    {
        std::lock_guard<std::mutex> g (lock);
        for (const auto& [k, p] : prints)
            if (k != id && p.valid && p.role != TrackRole::mix)
                out.push_back (p);
    }
    std::sort (out.begin(), out.end(), [] (const TrackPrint& a, const TrackPrint& b) { return a.updatedMs > b.updatedMs; });
    return out;
}

// =====================================================================================
LowPeak findLowPeak (const std::array<float, LowFine::count>& fine, double loHz, double hiHz)
{
    LowPeak p;
    const int lo = juce::jmax (1, LowFine::index (loHz)), hi = juce::jmin (LowFine::count - 2, LowFine::index (hiHz));
    int best = -1;
    for (int i = lo; i <= hi; ++i)
        if (fine[(size_t) i] > -150.0f && (best < 0 || fine[(size_t) i] > fine[(size_t) best]))
            best = i;
    if (best < 0)
        return p;
    // parabola through the peak and its neighbours, for a position between grid points
    const float a = fine[(size_t) best - 1], b = fine[(size_t) best], c = fine[(size_t) best + 1];
    const float den = a - 2.0f * b + c;
    const float shift = std::abs (den) > 1.0e-6f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
    p.found = true;
    p.hz = (float) LowFine::freq (best + shift);
    p.db = b - 0.25f * (a - c) * shift;
    return p;
}

LowPeak findKickNote (const std::array<float, LowFine::count>& fine)
{
    const auto strongest = findLowPeak (fine, 30.0, 160.0);
    if (! strongest.found)
        return strongest;
    // lightly smoothed, so ripple doesn't count as a peak
    std::array<float, LowFine::count> sm = fine;
    for (int i = 1; i + 1 < LowFine::count; ++i)
        sm[(size_t) i] = 0.25f * fine[(size_t) i - 1] + 0.5f * fine[(size_t) i] + 0.25f * fine[(size_t) i + 1];
    const int lo = juce::jmax (3, LowFine::index (30.0)), hi = LowFine::index (strongest.hz);
    for (int i = lo; i < hi; ++i)
    {
        const float v = sm[(size_t) i];
        if (v < strongest.db - 6.0f)
            continue;
        bool isPeak = true;
        for (int k = 1; k <= 3 && isPeak; ++k)
            isPeak = v >= sm[(size_t) (i - k)] && v >= sm[(size_t) juce::jmin (LowFine::count - 1, i + k)];
        if (isPeak && v > sm[(size_t) (i - 3)] + 1.0f)
            return findLowPeak (fine, LowFine::freq (i - 1.5), LowFine::freq (i + 1.5));
    }
    return strongest;
}

juce::String noteName (double hz)
{
    if (hz <= 0.0)
        return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1);
}

Profile trackProfile (TrackRole role, const Profile& style)
{
    Profile p = style;
    switch (role)
    {
        case TrackRole::mix: return p;
        case TrackRole::kick:     p.tilt = -4.5f; p.lowShelf = 9.0f; p.subCorner = 38.0f; p.highCorner = 9000.0f; p.tolerance = 5.0f; break;
        case TrackRole::bass:     p.tilt = -7.0f; p.lowShelf = 6.0f; p.subCorner = 30.0f; p.highCorner = 2500.0f; p.tolerance = 5.0f; break;
        case TrackRole::kickBass: p.tilt = -5.5f; p.lowShelf = 6.0f; p.subCorner = 32.0f; p.highCorner = 5000.0f; p.tolerance = 4.5f; break;
    }
    p.computeTarget();
    return p;
}

TrackPrint printOf (const Snapshot& s, TrackRole role)
{
    TrackPrint t;
    t.role = role;
    t.valid = role != TrackRole::mix && s.spectrumSongValid && s.secondsAnalysed >= 4.0f;
    t.fine = s.lowFineSong;
    const auto pk = role == TrackRole::kick ? findKickNote (s.lowFineSong) : findLowPeak (s.lowFineSong, 30.0, 250.0);
    t.peakHz = pk.found ? pk.hz : 0.0f;
    t.updatedMs = juce::Time::getMillisecondCounter();
    return t;
}

// =====================================================================================
namespace
{
    juce::String num (Lang lang, float v, int decimals = 1, bool sign = false)
    {
        auto s = juce::String (std::abs (v), decimals);
        if (v < 0.0f && s.containsAnyOf ("123456789"))
            s = (lang == Lang::finnish ? juce::String (juce::CharPointer_UTF8 ("−")) : juce::String ("-")) + s;
        else if (sign)
            s = "+" + s;
        return lang == Lang::finnish ? s.replaceCharacter ('.', ',') : s;
    }

    juce::String hzText (float hz) { return juce::String (juce::roundToInt (hz)) + " Hz"; }
    juce::String noteText (float hz) { return hzText (hz) + " (" + noteName (hz) + ")"; }

    float power (float db) { return std::pow (10.0f, db / 10.0f); }

    /** Total of the third-octave bands whose centre lies between two frequencies, dB. */
    float bandsDb (const std::array<float, Bands::count>& db, double lo, double hi)
    {
        return sumBands (db, Bands::nearest (lo), Bands::nearest (hi));
    }

    struct Check
    {
        float over, scale;
        bool outside() const { return over > 0.0f; }
        float excess() const { return over / scale; }
    };

    struct Builder
    {
        std::vector<Finding>& out;
        float& penalty;

        void add (const char* id, Check c, float weight, int tags, int direction, juce::String title, juce::String value,
                  juce::String why, juce::String howTo, bool watchOnly = false)
        {
            if (! c.outside())
                return;
            Finding f;
            f.id = id;
            f.excess = c.excess();
            f.severity = watchOnly || f.excess <= 1.0f ? Severity::watch : Severity::fix;
            f.tags = tags;
            f.direction = direction;
            f.title = std::move (title);
            f.value = std::move (value);
            f.why = std::move (why);
            f.howTo = std::move (howTo);
            f.penalty = weight * 14.0f * std::min (1.6f, f.excess);
            penalty += f.penalty;
            out.push_back (std::move (f));
        }

        void info (const char* id, int tags, juce::String title, juce::String value, juce::String why, juce::String howTo = {})
        {
            Finding f;
            f.id = id;
            f.severity = Severity::info;
            f.tags = tags;
            f.title = std::move (title);
            f.value = std::move (value);
            f.why = std::move (why);
            f.howTo = std::move (howTo);
            out.push_back (std::move (f));
        }
    };

    /** Styles where a long, ringing kick (808, rumble, hardstyle) is the sound itself. */
    bool longKickStyle (const Profile& p)
    {
        for (const char* s : { "Hip Hop", "Hardstyle", "Dubstep", "Future Bass", "Lo-fi" })
            if (juce::String (p.name).containsIgnoreCase (s))
                return true;
        return false;
    }

    bool psyStyle (const Profile& p) { return juce::String (p.name).containsIgnoreCase ("Psytrance"); }

    /** Semitones from a to b, as a fraction. */
    float semitones (float a, float b) { return 12.0f * std::log2 (b / a); }

    /** How many 1/24 octave points both prints have within 6 dB of their own peak (40-200 Hz). */
    int sharedPoints (const std::array<float, LowFine::count>& a, const std::array<float, LowFine::count>& b)
    {
        const int lo = LowFine::index (40.0), hi = LowFine::index (200.0);
        float ma = -300.0f, mb = -300.0f;
        for (int i = lo; i <= hi; ++i)
        {
            ma = juce::jmax (ma, a[(size_t) i]);
            mb = juce::jmax (mb, b[(size_t) i]);
        }
        int n = 0;
        for (int i = lo; i <= hi; ++i)
            if (a[(size_t) i] > ma - 6.0f && b[(size_t) i] > mb - 6.0f)
                ++n;
        return n;
    }
}

// =====================================================================================
Assessment assessTrack (const Snapshot& s, const Profile& style, TrackRole role, Window w, Lang lang,
                        const std::vector<TrackPrint>& others, const Snapshot* reference)
{
    const Profile tp = trackProfile (role, style);
    Assessment a;
    auto& m = a.measures;
    const bool song = w == Window::song;
    const bool kick = role == TrackRole::kick, bass = role == TrackRole::bass, bus = role == TrackRole::kickBass;

    const auto& mid = song ? s.midSong : s.midNow;
    const auto& side = song ? s.sideSong : s.sideNow;
    const auto& fine = song ? s.lowFineSong : s.lowFineNow;
    const bool spectrumValid = song ? s.spectrumSongValid : s.spectrumNowValid;

    m.loudness = song ? s.integrated : s.shortTerm;
    m.truePeak = song ? s.truePeakMax : s.truePeakRecent;
    m.plr = m.truePeak - m.loudness;
    m.lra = s.loudnessRange;
    m.correlation = song ? s.correlationSong : s.correlationNow;
    m.contrastValid = song ? s.contrastSongValid : s.contrastNowValid;
    m.lowContrast = song ? s.lowContrastSong : s.lowContrastNow;
    m.kickValid = song ? s.kickSongValid : s.kickNowValid;
    m.duck = song ? s.duckSong : s.duckNow;
    m.beatSteady = s.tempoConfidence >= 0.5f;
    m.highContrast = song ? s.highContrastSong : s.highContrastNow;
    m.balance = song ? s.balanceSong : s.balanceNow;
    m.dcOffset = s.dcOffset;
    m.samplePeak = s.samplePeakMax;
    m.clipEvents = s.clipEvents;

    m.valid = spectrumValid && m.loudness > -60.0f && (! song || s.secondsAnalysed >= 4.0f);
    a.enoughData = m.valid;

    if (! m.valid)
    {
        Finding f;
        f.id = "listening";
        f.severity = Severity::info;
        f.title = tr (lang, "Play the track", "Soita raitaa");
        f.why = (kick ? tr (lang, "WWMA is on a kick track. Play a part where the kick runs for a few bars; solo it if other sounds go through this channel.",
                               "WWMA on kick-raidalla. Soita kohtaa, jossa kick soi muutaman tahdin; soolaa se, jos samaa kanavaa pitkin kulkee muutakin.")
              : bass ? tr (lang, "WWMA is on a bass track. Play a part where the bass line runs; the more notes, the better.",
                               "WWMA on bassoraidalla. Soita kohtaa, jossa bassolinja soi; mitä enemmän nuotteja, sen parempi.")
                     : tr (lang, "WWMA is on the kick + bass bus. Play a part where both play together, like a drop.",
                               "WWMA on kick + basso -väylällä. Soita kohtaa, jossa molemmat soivat yhdessä, esim. drop."));
        a.findings.push_back (f);
        return a;
    }

    // ---- spectrum, the same way as for a mix but against the typical shape of this sound
    std::array<float, Bands::count> total {};
    for (int b = 0; b < Bands::count; ++b)
        total[(size_t) b] = 10.0f * std::log10 (power (mid[(size_t) b]) + power (side[(size_t) b]) + 1.0e-30f);
    const auto rel = relativeToTotal (total);
    std::array<float, numRanges> dev {};
    for (int r = 0; r < numRanges; ++r)
    {
        const auto info = rangeBands ((Range) r);
        dev[(size_t) r] = sumBands (rel, info.firstBand, info.lastBand) - sumBands (tp.target, info.firstBand, info.lastBand);
    }
    auto sorted = dev;
    std::sort (sorted.begin(), sorted.end());
    const float offset = 0.5f * (sorted[2] + sorted[3]);
    for (auto& d : dev)
        d -= offset;
    m.rangeDeviation = dev;
    const float totalDb = sumBands (total, 0, Bands::count - 1);
    for (int b = 0; b < Bands::count; ++b)
    {
        m.spectrum[(size_t) b] = rel[(size_t) b] - offset;
        m.side[(size_t) b] = side[(size_t) b] - totalDb - offset;
    }
    m.lowWidth = sumBands (side, 0, 8) - sumBands (mid, 0, 8);
    m.width = sumBands (side, 0, Bands::count - 1) - sumBands (mid, 0, Bands::count - 1);
    m.monoLossBass = sumBands (total, 0, 8) - sumBands (mid, 0, 8);
    {
        const float measRatio = sumBands (rel, 7, 11) - sumBands (rel, 2, 6);
        const float targetRatio = sumBands (tp.target, 7, 11) - sumBands (tp.target, 2, 6);
        m.phoneBass = measRatio - targetRatio;
    }

    float penalty = 0.0f;
    Builder add { a.findings, penalty };
    const auto dB = [lang] (float v) { return num (lang, v, 1, true) + " dB"; };
    const float bpm = s.tempoConfidence >= 0.5f ? s.tempoBpm : 0.0f;
    const float crest = song ? s.crestSong : s.crestNow;

    // the low end peak: where a kick is tuned, or where a bass line has most of its weight
    const auto peak = kick ? findKickNote (fine) : findLowPeak (fine, 30.0, 250.0);

    // the other WWMA instances: the bass sees the kick, the kick sees the bass
    const TrackPrint* partner = nullptr;
    for (const auto& o : others)
        if ((kick && o.role == TrackRole::bass) || (bass && o.role == TrackRole::kick))
        {
            partner = &o;
            break;
        }

    // ---- kick
    if (kick && peak.found)
    {
        const auto value = tr (lang, "Low peak ", "Matala huippu ") + noteText (peak.hz);
        add.info ("kick-note", 0, tr (lang, "Kick is tuned to ", "Kick on viritetty sävelelle ") + noteName (peak.hz), value,
                  tr (lang, "This is where the kick's body rings, the frequency its pitch drop ends on. A kick in the key of the song (the root or the fifth) sits with the bass instead of fighting it.",
                            "Tässä kickin runko soi: taajuus, johon sen pitch-laskeuma päättyy. Kappaleen sävellajissa oleva kick (perussävel tai kvintti) istuu basson kanssa sen sijaan, että tappelisi sitä vastaan."),
                  tr (lang, "Tune the kick with the pitch / tune knob of your sampler or synth. Most electronic kicks sit between 45 and 65 Hz.",
                            "Viritä kick samplerin tai syntikan pitch-/tune-nupilla. Useimmat elektroniset kickit ovat 45–65 Hz:n välillä."));

        add.add ("kick-low", { 40.0f - peak.hz, 6.0f }, 0.4f, tagPhone | tagCar, -1,
                 tr (lang, "Kick is tuned very low", "Kick on viritetty hyvin matalalle"), value,
                 tr (lang, "Below 40 Hz the kick is felt more than heard: phones, laptops and many car systems don't play it, so the kick nearly disappears, and it eats headroom.",
                           "Alle 40 Hz kick tuntuu enemmän kuin kuuluu: puhelin, läppäri ja moni autostereo eivät toista sitä, joten kick melkein katoaa, ja se syö tilaa."),
                 tr (lang, "1. Tune the kick up a few semitones, towards 45-60 Hz.\n2. Or keep it and make sure it has a clear click and some 100-200 Hz punch so small speakers still show it.",
                           "1. Viritä kickiä muutama puolisävel ylöspäin, kohti 45–60 Hz:ä.\n2. Tai pidä se ja varmista, että siinä on selvä klikki ja vähän 100–200 Hz:n iskua, jotta pienetkin kaiuttimet näyttävät sen."),
                 true);
        add.add ("kick-high", { peak.hz - 100.0f, 25.0f }, 0.4f, tagClub | tagCar, 1,
                 tr (lang, "Kick has no low body", "Kickistä puuttuu matala runko"), value,
                 tr (lang, "The kick's strongest low part is above 100 Hz. It will sound like a knock rather than a thump on a club system or in a car.",
                           "Kickin vahvin matala osa on yli 100 Hz:ssä. Klubilla tai autossa se kuulostaa koputukselta eikä tömähdykseltä."),
                 tr (lang, "1. Check you haven't high-passed the kick too high.\n2. Pick a kick sample with a lower body, or layer a short sine (45-60 Hz) under it.",
                           "1. Tarkista, ettei kickin ylipäästösuodin ole liian korkealla.\n2. Valitse kick-sample, jossa on matalampi runko, tai kerrosta sen alle lyhyt siniaalto (45–60 Hz)."),
                 true);

        // the tail: does it ring into the off-beat bass?
        const bool decayValid = song ? s.kickDecaySongValid : s.kickDecayNowValid;
        const float decay = song ? s.kickDecaySong : s.kickDecayNow;
        m.kickValid = m.kickValid || decayValid;
        if (decayValid && ! longKickStyle (style))
        {
            const float beat = bpm > 0.0f ? 60000.0f / bpm : 0.0f;
            const float limit = beat > 0.0f ? (psyStyle (style) ? 0.3f : 0.5f) * beat : 350.0f;
            const auto lengthValue = tr (lang, "Tail ", "Häntä ") + juce::String (juce::roundToInt (decay)) + " ms"
                                     + (beat > 0.0f ? " (" + tr (lang, "off-beat at ", "offbeat ") + juce::String (juce::roundToInt (0.5f * beat)) + " ms, "
                                                          + juce::String (juce::roundToInt (bpm)) + " BPM)"
                                                    : juce::String());
            add.add ("kick-long", { decay - limit, 0.35f * limit }, 0.7f, tagCar | tagClub, 1,
                     tr (lang, "Kick tail rings into the bass", "Kickin häntä soi basson päälle"), lengthValue,
                     psyStyle (style)
                         ? tr (lang, "In psytrance the bass plays the three sixteenths after every kick, so the kick has to be gone within the first sixteenth or the two smear into one boom.",
                                     "Psytrancessa basso soittaa kolme kuudestoistaosaa jokaisen kickin jälkeen, joten kickin täytyy loppua ensimmäisen kuudestoistaosan aikana, muuten ne sulautuvat yhdeksi jumputukseksi.")
                         : tr (lang, "The kick's low end is still ringing when the off-beat bass comes in. The two pile up in the same frequencies, the low end gets muddy and the limiter has to work harder.",
                                     "Kickin bassopää soi vielä, kun offbeat-basso alkaa. Ne kasautuvat samoille taajuuksille, bassopää mutaantuu ja limitteri joutuu tekemään enemmän töitä."),
                     tr (lang, "1. Shorten the kick's decay or release (amp envelope, or a transient shaper's sustain) until the tail is about ",
                               "1. Lyhennä kickin decayta tai releasea (vahvistimen verhokäyrä tai transienttimuokkaimen sustain), kunnes häntä on noin ")
                         + juce::String (juce::roundToInt (0.8f * limit)) + " ms.\n"
                         + tr (lang, "2. Or duck the bass with the kick (sidechain) so the tail has the low end to itself.\nTrap: a long kick sounds huge in solo. Judge it with the bass playing.",
                                     "2. Tai duckaa bassoa kickillä (sidechain), jolloin hännällä on bassopää yksin.\nSudenkuoppa: pitkä kick kuulostaa soolona valtavalta. Arvioi se basson soidessa."));
            add.add ("kick-short", { 50.0f - decay, 25.0f }, 0.3f, tagClub, -1,
                     tr (lang, "Kick is very short", "Kick on hyvin lyhyt"), lengthValue,
                     tr (lang, "The kick fades in under 50 ms: it clicks but has little weight. Fine for some styles, but usually the body needs 100-250 ms.",
                               "Kick häviää alle 50 ms:ssa: se naksahtaa, mutta painoa on vähän. Joihinkin tyyleihin sopii, mutta yleensä runko tarvitsee 100–250 ms."),
                     tr (lang, "Lengthen the decay a little, or check that a gate or transient shaper isn't cutting the body.",
                               "Pidennä decayta hieman tai tarkista, ettei gate tai transienttimuokkain leikkaa runkoa."),
                     true);
        }

        // the click: what phones play of a kick
        const float body = bandsDb (rel, 40.0, 125.0);
        const float click = bandsDb (rel, 2000.0, 6300.0) - body;
        const auto clickValue = tr (lang, "Click (2-6 kHz) ", "Klikki (2–6 kHz) ") + dB (click) + tr (lang, " vs body", " runkoon nähden");
        add.add ("kick-noclick", { -42.0f - click, 8.0f }, 0.6f, tagPhone, -1,
                 tr (lang, "Kick has no click: it vanishes on phones", "Kickissä ei ole klikkiä: se katoaa puhelimesta"), clickValue,
                 tr (lang, "A phone speaker plays almost nothing below 150 Hz. What you hear of a kick there is its click and attack. Without it, the beat is gone on small speakers.",
                           "Puhelimen kaiutin ei toista juuri mitään alle 150 Hz:n. Kickistä kuuluu siellä vain klikki ja isku. Ilman niitä biitti katoaa pienistä kaiuttimista."),
                 tr (lang, "1. Boost 2-3 dB around 3-4 kHz on the kick, or layer a short click/top sample.\n2. Or a transient shaper: a little more attack.\n3. Check on your phone: the beat should still be there.",
                           "1. Korosta kickiä 2–3 dB noin 3–4 kHz:stä tai kerrosta lyhyt klikki-/top-sample.\n2. Tai transienttimuokkain: hieman lisää attackia.\n3. Tarkista puhelimesta: biitin pitää kuulua yhä."));
        add.add ("kick-clicky", { click + 6.0f, 6.0f }, 0.4f, tagPhone | tagHeadphones, 1,
                 tr (lang, "Kick click is very sharp", "Kickin klikki on hyvin terävä"), clickValue,
                 tr (lang, "The click is almost as loud as the body. On phones and earbuds it pokes out and tires the ear.",
                           "Klikki on lähes yhtä kova kuin runko. Puhelimessa ja nappikuulokkeissa se pistää esiin ja väsyttää korvaa."),
                 tr (lang, "Cut 2-4 dB around 3-5 kHz on the kick, or turn the click layer down.",
                           "Leikkaa kickistä 2–4 dB noin 3–5 kHz:stä tai laske klikkikerrosta."),
                 true);

        const float box = bandsDb (rel, 200.0, 500.0) - body;
        add.add ("kick-mud", { box + 7.0f, 4.0f }, 0.5f, tagPhone | tagCar, 1,
                 tr (lang, "Boxy kick (200-500 Hz)", "Pahvinen kick (200–500 Hz)"),
                 tr (lang, "200-500 Hz ", "200–500 Hz ") + dB (box) + tr (lang, " vs body", " runkoon nähden"),
                 tr (lang, "A lot of low mids makes the kick sound like cardboard, and this is exactly where the bass harmonics, the snare and the vocal live.",
                           "Paljon alakeskiääntä saa kickin kuulostamaan pahvilta, ja juuri tällä alueella elävät basson harmoniset, virveli ja vokaali."),
                 tr (lang, "Sweep a narrow cut (Q 3-5) between 200 and 500 Hz on the kick and cut 3-6 dB where it sounds boxy.",
                           "Pyyhkäise kapeaa leikkausta (Q 3–5) 200–500 Hz:n välillä ja leikkaa kickistä 3–6 dB kohdasta, jossa se kuulostaa pahviselta."));

        add.add ("kick-squashed", { 7.0f - crest, 3.0f }, 0.5f, tagClub | tagCar, -1,
                 tr (lang, "Kick is squashed: little punch left", "Kick on litistetty: iskua on vähän jäljellä"),
                 tr (lang, "Crest factor ", "Crest-kerroin ") + num (lang, crest) + " dB",
                 tr (lang, "The kick's peaks hardly rise above its average level. Heavy clipping or limiting on the kick makes it loud but flat, and the master limiter then has nothing to work with.",
                           "Kickin huiput tuskin nousevat sen keskitason yläpuolelle. Kova klippaus tai limitointi kickissä tekee siitä kovan mutta litteän, eikä masterin limitterillä ole sen jälkeen mitään, millä työskennellä."),
                 tr (lang, "Back off the clipper or compressor on the kick by 2-3 dB, or use a slower attack (10-30 ms) so the first hit gets through.",
                           "Höllennä kickin klipperiä tai kompressoria 2–3 dB tai käytä hitaampaa attackia (10–30 ms), jotta ensimmäinen isku pääsee läpi."),
                 true);
    }

    // ---- bass
    if (bass && peak.found)
    {
        add.info ("bass-note", 0, tr (lang, "Bass weight centres on ", "Basson paino on sävelessä ") + noteName (peak.hz),
                  tr (lang, "Low peak ", "Matala huippu ") + noteText (peak.hz),
                  tr (lang, "The strongest part of the bass line over what you played: usually the root note of the song. If one note is much louder than the others, a car or a club will make it boom.",
                            "Bassolinjan vahvin kohta soitetulla alueella: yleensä kappaleen perussävel. Jos yksi nuotti on paljon muita kovempi, auto tai klubi saa sen jumputtamaan."));

        const float fundamental = bandsDb (rel, 31.5, 125.0);
        const float harm = bandsDb (rel, 200.0, 800.0) - fundamental;
        const auto harmValue = tr (lang, "Harmonics (200-800 Hz) ", "Harmoniset (200–800 Hz) ") + dB (harm) + tr (lang, " vs fundamental", " perustaajuuteen nähden");
        add.add ("bass-noharm", { -24.0f - harm, 8.0f }, 0.9f, tagPhone, -1,
                 tr (lang, "Bass will disappear on phones", "Basso katoaa puhelimesta"), harmValue,
                 tr (lang, "The bass is almost a pure low tone. Phones and laptops can't play it, and the ear only finds a bass line on small speakers through its harmonics.",
                           "Basso on lähes puhdas matala ääni. Puhelin ja läppäri eivät toista sitä, ja korva löytää bassolinjan pienistä kaiuttimista vain sen harmonisten kautta."),
                 tr (lang, "1. Add saturation or distortion to the bass (or a parallel copy), then low-pass it at 1-2 kHz.\n2. Or layer a mid bass (saw/square an octave up), quieter, high-passed at 150 Hz.\n3. Check on your phone: you should still hear the bass line, not just guess it.",
                           "1. Lisää bassoon saturaatiota tai säröä (tai rinnakkaiskopioon) ja alipäästösuodata 1–2 kHz:iin.\n2. Tai kerrosta keskibasso (saha/kantti oktaavia ylempänä) hiljaisempana, ylipäästösuodatettuna 150 Hz:iin.\n3. Tarkista puhelimesta: bassolinjan pitää kuulua, ei vain arvailla."));
        add.add ("bass-fizzy", { harm + 4.0f, 6.0f }, 0.4f, tagPhone | tagHeadphones, 1,
                 tr (lang, "Bass is mostly mids", "Basso on enimmäkseen keskiääntä"), harmValue,
                 tr (lang, "The 200-800 Hz part is louder than the low end of the bass. It will fight the chords, the vocal and the snare, and the low end may feel thin.",
                           "200–800 Hz:n osa on kovempi kuin basson matala pää. Se tappelee sointujen, vokaalin ja virvelin kanssa, ja bassopää voi tuntua ohuelta."),
                 tr (lang, "Low-pass the bass a little lower, or cut 2-3 dB around 300-600 Hz. If it's a distorted bass, lower the distortion's mix.",
                           "Alipäästösuodata bassoa hieman matalammalta tai leikkaa 2–3 dB noin 300–600 Hz:stä. Jos basso on säröytetty, laske särön mix-arvoa."),
                 true);

        // sub vs bass: the style's own balance, loosely (the kick also adds sub)
        const float subVsBass = bandsDb (rel, 20.0, 50.0) - bandsDb (rel, 63.0, 125.0);
        const float styleRatio = bandsDb (style.target, 20.0, 50.0) - bandsDb (style.target, 63.0, 125.0);
        const auto ratioValue = tr (lang, "Sub vs bass ", "Sub vs. basso ") + dB (subVsBass) + " (" + tr (lang, "style ", "tyyli ") + dB (styleRatio) + ")";
        add.add ("bass-sub+", { subVsBass - styleRatio - 7.0f, 4.0f }, 0.5f, tagCar | tagPhone, 1,
                 tr (lang, "Bass is mostly sub", "Basso on enimmäkseen subia"), ratioValue,
                 tr (lang, "Most of the bass sits below 50 Hz. A car cabin booms there and a phone plays none of it; the bass line becomes a feeling instead of notes.",
                           "Suurin osa bassosta on alle 50 Hz:ssä. Auton matkustamo jumputtaa siellä eikä puhelin toista siitä mitään; bassolinjasta tulee tunne eikä nuotteja."),
                 tr (lang, "Lower 30-50 Hz by 2-4 dB with a low shelf, or move the bass up an octave and keep a quieter sine sub under it.",
                           "Laske 30–50 Hz:ä 2–4 dB alahyllyllä tai siirrä bassoa oktaavi ylöspäin ja pidä sen alla hiljaisempi siniaaltosubi."),
                 true);

        add.add ("bass-uneven", { crest - 17.0f, 5.0f }, 0.5f, tagCar | tagPhone, 1,
                 tr (lang, "Bass notes jump in level", "Bassonuottien tasot pomppivat"),
                 tr (lang, "Crest factor ", "Crest-kerroin ") + num (lang, crest) + " dB",
                 tr (lang, "Some notes or attacks are much louder than the rest. In a car the loud ones boom and the quiet ones vanish under road noise.",
                           "Jotkin nuotit tai iskut ovat paljon muita kovempia. Autossa kovat jumputtavat ja hiljaiset katoavat tienäänen alle."),
                 tr (lang, "1. Even out velocities first.\n2. Then a compressor on the bass: ratio 4:1, attack 10-30 ms, release 50-100 ms, 3-6 dB gain reduction.",
                           "1. Tasaa ensin velocityt.\n2. Sitten kompressori bassoon: suhde 4:1, attack 10–30 ms, release 50–100 ms, 3–6 dB vaimennus."));
    }

    // ---- kick + bass together on one bus
    if (bus)
    {
        if (m.kickValid && style.lowContrastMin > 0.0f)
            add.add ("kickbass", { style.lowContrastMin - m.lowContrast, 3.0f }, 1.0f, tagClub | tagCar, -1,
                     tr (lang, "Kick drowns in the bass", "Kick hukkuu bassoon"),
                     tr (lang, "Kick rises ", "Kick nousee ") + num (lang, m.lowContrast) + tr (lang, " dB above the bass (aim ", " dB basson yli (tavoite ")
                         + num (lang, style.lowContrastMin) + "+)",
                     tr (lang, "When the bass fills the same low end the kick hits, the kick only adds a little on top: the groove loses its thump, mostly on big systems and in cars.",
                               "Kun basso täyttää saman bassopään, johon kick iskee, kick lisää päälle vain vähän: groove menettää töminänsä, etenkin isoilla laitteilla ja autossa."),
                     tr (lang, "1. Sidechain-duck the bass with the kick: a compressor on the bass keyed from the kick, ratio 4:1 or more, attack 0-2 ms, release about ",
                               "1. Duckaa bassoa kickillä: bassoon kompressori, jota kick ohjaa (sidechain), suhde vähintään 4:1, attack 0–2 ms, release noin ")
                         + juce::String (juce::roundToInt (bpm > 0.0f ? 30000.0f / bpm : 200.0f))
                         + tr (lang, " ms (half a beat), 3-6 dB gain reduction. A volume shaper drawn to the beat works the same way.\n2. Or give them different peaks: kick 50-60 Hz, bass body 80-110 Hz (or the other way round).\n3. Or turn the bass's sub down 2-3 dB.",
                                     " ms (puoli iskua), 3–6 dB vaimennus. Tahtiin piirretty volume shaper toimii samoin.\n2. Tai anna niille eri huiput: kick 50–60 Hz, basson runko 80–110 Hz (tai toisin päin).\n3. Tai laske basson subia 2–3 dB."));

        const float harm = bandsDb (rel, 200.0, 800.0) - bandsDb (rel, 31.5, 125.0);
        add.add ("bass-noharm", { -26.0f - harm, 8.0f }, 0.8f, tagPhone, -1,
                 tr (lang, "Low end will disappear on phones", "Bassopää katoaa puhelimesta"),
                 tr (lang, "Harmonics (200-800 Hz) ", "Harmoniset (200–800 Hz) ") + dB (harm),
                 tr (lang, "Kick and bass are almost only low tones. On a phone the groove disappears, because it's carried there by the click of the kick and the harmonics of the bass.",
                           "Kick ja basso ovat lähes pelkkiä matalia ääniä. Puhelimessa groove katoaa, koska siellä sitä kantavat kickin klikki ja basson harmoniset."),
                 tr (lang, "Saturate the bass a little (or a parallel copy) and give the kick some click around 3-4 kHz.",
                           "Saturoi bassoa hieman (tai rinnakkaiskopiota) ja anna kickille vähän klikkiä noin 3–4 kHz:stä."));

        const float subVsBass = bandsDb (rel, 20.0, 50.0) - bandsDb (rel, 63.0, 125.0);
        const float styleRatio = bandsDb (style.target, 20.0, 50.0) - bandsDb (style.target, 63.0, 125.0);
        const auto ratioValue = tr (lang, "Sub vs bass ", "Sub vs. basso ") + dB (subVsBass) + " (" + tr (lang, "style ", "tyyli ") + dB (styleRatio) + ")";
        add.add ("bus-sub+", { subVsBass - styleRatio - 4.0f, 3.0f }, 0.7f, tagCar | tagPhone, 1,
                 tr (lang, "Too much sub in kick + bass", "Liikaa subia kickissä ja bassossa"), ratioValue,
                 tr (lang, "The sub (below 50 Hz) outweighs the bass range for this style. Cars boom, phones play nothing of it, and it eats the master's headroom.",
                           "Subi (alle 50 Hz) painaa tähän tyyliin nähden enemmän kuin bassoalue. Autot jumputtavat, puhelin ei toista siitä mitään, ja se syö masterin tilaa."),
                 tr (lang, "Decide which one owns the sub, the kick or the bass, and lower the other's sub 3-4 dB with a low shelf at 50 Hz.",
                           "Päätä, kumpi omistaa subin, kick vai basso, ja laske toisen subia 3–4 dB alahyllyllä 50 Hz:ssä."));
        add.add ("bus-sub-", { styleRatio - subVsBass - 5.0f, 3.0f }, 0.5f, tagClub | tagCar, -1,
                 tr (lang, "Little sub in kick + bass", "Vähän subia kickissä ja bassossa"), ratioValue,
                 tr (lang, "For this style the sub (below 50 Hz) is light. On a club system and in a car the low end will feel thin.",
                           "Tähän tyyliin subi (alle 50 Hz) on kevyt. Klubilla ja autossa bassopää tuntuu ohuelta."),
                 tr (lang, "Check high-pass filters on the kick and bass, or add a quiet sine sub under the bass.",
                           "Tarkista kickin ja basson ylipäästösuotimet tai lisää basson alle hiljainen siniaaltosubi."),
                 true);

        add.add ("bus-squashed", { 6.0f - crest, 2.5f }, 0.4f, tagClub | tagCar, -1,
                 tr (lang, "Kick and bass are squashed together", "Kick ja basso on litistetty yhteen"),
                 tr (lang, "Crest factor ", "Crest-kerroin ") + num (lang, crest) + " dB",
                 tr (lang, "The kick hardly rises above the bass and the bus has no peaks left: a hard clipper or limiter on the bus is flattening the groove.",
                           "Kick tuskin nousee basson yli eikä väylällä ole huippuja jäljellä: kova klipperi tai limitteri väylällä litistää groovea."),
                 tr (lang, "Back off the bus clipper or limiter 2-3 dB; get the level from the balance and the ducking instead.",
                           "Höllennä väylän klipperiä tai limitteriä 2–3 dB; hae taso mieluummin tasapainosta ja duckauksesta."),
                 true);
    }

    // ---- kick vs bass on separate tracks, through the other WWMA instance
    if (partner != nullptr && peak.found && partner->peakHz > 0.0f)
    {
        const float kickHz = kick ? peak.hz : partner->peakHz;
        const float bassHz = kick ? partner->peakHz : peak.hz;
        const float apart = semitones (kickHz, bassHz);
        const int shared = sharedPoints (kick ? fine : partner->fine, kick ? partner->fine : fine);
        const auto value = tr (lang, "Kick ", "Kick ") + noteText (kickHz) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + tr (lang, "bass ", "basso ") + noteText (bassHz);
        const float near = juce::jmax (2.5f - std::abs (apart), (float) (shared - 8) / 8.0f);
        add.add ("clash", { near, 1.5f }, 0.9f, tagCar | tagClub, 0,
                 tr (lang, "Kick and bass peak at the same spot", "Kickin ja basson huiput osuvat samaan kohtaan"), value,
                 tr (lang, "Both have their strongest low end in the same place. Whenever they play together they add up into one boom, and neither is clear. Cars and clubs make it worse.",
                           "Molempien vahvin bassopää on samassa kohdassa. Aina kun ne soivat yhdessä, ne summautuvat yhdeksi jumputukseksi eikä kumpikaan kuulu selvästi. Autot ja klubit pahentavat tätä."),
                 tr (lang, "Choose one:\n1. Sidechain-duck the bass with the kick (attack 0-2 ms, release half a beat, 3-6 dB).\n2. Separate them: e.g. kick at 50-60 Hz and a bass that lives at 80-110 Hz, or a deep sub bass and a kick tuned higher.\n3. Cut a narrow 2-4 dB dip in the bass at the kick's frequency (",
                           "Valitse yksi:\n1. Duckaa bassoa kickillä sidechainilla (attack 0–2 ms, release puoli iskua, 3–6 dB).\n2. Erota ne: esim. kick 50–60 Hz:ssä ja basso, joka elää 80–110 Hz:ssä, tai syvä subibasso ja korkeammalle viritetty kick.\n3. Leikkaa bassoon kapea 2–4 dB:n kuoppa kickin taajuudelle (")
                     + hzText (kickHz) + ").");

        // tuning: the kick against the bass's main note
        const int interval = ((int) std::lround (apart) % 12 + 12) % 12;
        const bool clashing = std::abs (apart) < 2.5f;
        const bool dissonant = interval == 1 || interval == 2 || interval == 6 || interval == 10 || interval == 11;
        if (! clashing && dissonant)
        {
            Finding f;
            f.id = "kick-tune";
            f.severity = Severity::watch;
            f.tags = tagClub | tagCar;
            f.excess = 0.8f;
            f.penalty = 0.3f * 14.0f * 0.8f;
            penalty += f.penalty;
            f.title = tr (lang, "Kick is out of tune with the bass", "Kick on epävireessä basson kanssa");
            f.value = value;
            f.why = tr (lang, "The kick's note and the bass's main note are a dissonant interval apart (a semitone, a whole tone or a tritone). In the low end that sounds like a dull wobble or a boom that doesn't belong.",
                              "Kickin sävel ja basson pääsävel ovat riitasointuisen välimatkan päässä toisistaan (puolisävel, kokosävel tai tritonus). Bassopäässä se kuulostaa tylsältä huojunnalta tai jumputukselta, joka ei kuulu kappaleeseen.");
            f.howTo = tr (lang, "Tune the kick to the bass's note (", "Viritä kick basson sävelelle (") + noteName (bassHz)
                      + tr (lang, ") or its fifth: usually only one or two semitones.", ") tai sen kvintille: yleensä vain yksi tai kaksi puolisäveltä.");
            a.findings.push_back (f);
        }
    }
    else if ((kick || bass) && peak.found && others.empty())
    {
        add.info ("partner", 0,
                  kick ? tr (lang, "Add WWMA to the bass too", "Lisää WWMA myös bassoon")
                       : tr (lang, "Add WWMA to the kick too", "Lisää WWMA myös kickiin"),
                  {},
                  tr (lang, "Put another WWMA on the other track and set it to its role. The two then see each other and tell you if the kick and the bass collide or are out of tune. (Some DAWs run every plugin separately; then this can't work, and a WWMA on a kick + bass bus is the way.)",
                            "Laita toinen WWMA toiselle raidalle ja valitse sille sen rooli. Silloin ne näkevät toisensa ja kertovat, törmäävätkö kick ja basso tai ovatko ne epävireessä. (Jotkin DAWit ajavat jokaista pluginia erikseen; silloin tämä ei toimi, ja ratkaisu on WWMA kick + basso -väylällä.)"));
    }

    // ---- all roles: rumble, mono, gain staging
    if (peak.found)
    {
        const auto r = findLowPeak (fine, 20.0, 28.0);
        const float rumble = r.found ? r.db - peak.db : -100.0f;
        add.add ("rumble", { rumble + 12.0f, 6.0f }, 0.5f, tagCar | tagClub, 1,
                 tr (lang, "Rumble below 30 Hz", "Jyrinää alle 30 Hz:ssä"),
                 tr (lang, "20-28 Hz ", "20–28 Hz ") + dB (rumble) + tr (lang, " vs the low peak", " matalaan huippuun nähden"),
                 tr (lang, "Energy this low is hardly heard on any system but it moves speaker cones, eats headroom and makes the master limiter pump.",
                           "Näin matalaa energiaa ei kuule juuri miltään laitteelta, mutta se liikuttaa kaiutinkartioita, syö tilaa ja saa masterin limitterin pumppaamaan."),
                 tr (lang, "High-pass this track at 25-30 Hz (12-24 dB/oct). Keep the cut below the low peak at ", "Ylipäästösuodata tämä raita 25–30 Hz:iin (12–24 dB/okt). Pidä leikkaus matalan huipun alapuolella (")
                     + hzText (peak.hz) + tr (lang, ".", ")."));
    }

    if (m.correlation >= 0.0f)
        add.add ("lowwidth", { m.lowWidth + 20.0f, 6.0f }, 0.9f, tagClub | tagPhone, 1,
                 tr (lang, "Low end is not mono", "Bassopää ei ole mono"),
                 tr (lang, "Side vs mid below 140 Hz: ", "Sivu vs. keski alle 140 Hz: ") + dB (m.lowWidth),
                 tr (lang, "Kick and bass belong in the centre. Stereo low end is lost or uneven on club systems (mono subs), on phones and in cars, and makes vinyl and some limiters misbehave.",
                           "Kick ja basso kuuluvat keskelle. Stereo-bassopää katoaa tai on epätasainen klubeilla (mono-subit), puhelimissa ja autoissa, ja se sotkee vinyylin ja jotkin limitterit."),
                 tr (lang, "1. Turn off stereo chorus, unison spread or a widener on this track, at least below 150 Hz.\n2. Or use a 'mono below' / bass mono tool at 120-150 Hz.",
                           "1. Kytke pois stereochorus, unison-levitys tai leventäjä tältä raidalta, ainakin alle 150 Hz:n.\n2. Tai käytä 'mono below' / bass mono -työkalua 120–150 Hz:ssä."));
    if (m.monoLossBass > 1.5f)
        add.add ("monoloss", { m.monoLossBass - 1.5f, 2.0f }, 0.9f, tagClub | tagPhone, 1,
                 tr (lang, "Low end gets weaker in mono", "Bassopää heikkenee monona"),
                 tr (lang, "Loss below 140 Hz in mono: ", "Häviö alle 140 Hz:ssä monona: ") + dB (-m.monoLossBass),
                 tr (lang, "Left and right partly cancel each other in the low end. In mono (clubs, phones) the track gets thinner.",
                           "Vasen ja oikea kumoavat toisiaan osittain bassopäässä. Monona (klubit, puhelimet) raita ohenee."),
                 tr (lang, "Check for a phase-flipped layer or a stereo effect; make everything below 150 Hz mono.",
                           "Tarkista, onko jokin kerros vastavaiheessa tai stereoefektin takana; tee kaikesta alle 150 Hz:n monoa."));

    {
        const float pk = juce::jmax (m.truePeak, m.samplePeak);
        add.add ("track-hot", { pk + 1.0f, 2.0f }, 0.5f, 0, 1,
                 tr (lang, "Track peaks too hot", "Raidan huiput liian kuumia"),
                 tr (lang, "Peak ", "Huippu ") + num (lang, pk) + " dBFS",
                 tr (lang, "This track alone is peaking near 0 dB. Once the other tracks are added the bus and the master clip, and analogue-style plugins after it distort.",
                           "Tämä raita yksinään piikkaa lähellä nollaa. Kun muut raidat lisätään, väylä ja master klippaavat, ja analogimalliset pluginit sen jälkeen säröytyvät."),
                 tr (lang, "Turn the track's gain (or the instrument's output) down so the peaks sit around -10 to -6 dBFS. Rebalance with the faders afterwards.",
                           "Laske raidan gainia (tai instrumentin ulostuloa), kunnes huiput ovat noin −10…−6 dBFS. Tasapainota faderit sen jälkeen uudelleen."));
        if (pk < -30.0f)
            add.info ("track-quiet", 0, tr (lang, "Track is very quiet", "Raita on hyvin hiljainen"),
                      tr (lang, "Peak ", "Huippu ") + num (lang, pk) + " dBFS",
                      tr (lang, "Fine for the sound, but compressors, saturators and gates on the track react to level: their thresholds need to go far down.",
                                "Äänen kannalta kunnossa, mutta raidan kompressorit, saturaattorit ja gatet reagoivat tasoon: niiden kynnykset pitää laskea paljon alas."),
                      tr (lang, "Aim for peaks around -10 to -6 dBFS on each track.", "Tavoittele raidoille noin −10…−6 dBFS:n huippuja."));
    }
    if (m.clipEvents > 0)
        add.add ("clipping", { (float) m.clipEvents, 3.0f }, 0.6f, 0, 1,
                 tr (lang, "Flat-topped, clipped peaks", "Litteiksi klipattuja huippuja"),
                 juce::String (m.clipEvents) + tr (lang, " clipped runs", " klippausta"),
                 tr (lang, "Samples stuck at full scale. If that's not a clipper you chose on purpose, it is distortion.",
                           "Näytteitä jumissa täydessä tasossa. Jos et käytä klipperiä tarkoituksella, se on säröä."),
                 tr (lang, "Lower the gain before this point.", "Laske gainia ennen tätä kohtaa."));

    // ---- against a reference track (a whole mix): its low end is mostly kick and bass
    if (reference != nullptr && reference->spectrumSongValid && peak.found)
    {
        const auto refPeak = kick ? findKickNote (reference->lowFineSong) : findLowPeak (reference->lowFineSong, 30.0, 250.0);
        if (refPeak.found && (kick || bus))
        {
            const float diff = semitones (refPeak.hz, peak.hz);
            add.add ("ref-peak", { std::abs (diff) - 3.0f, 3.0f }, 0.3f, tagClub | tagCar, diff > 0.0f ? 1 : -1,
                     kick ? tr (lang, "Kick sits in a different place than the reference's low end", "Kick on eri kohdassa kuin referenssin bassopää")
                          : tr (lang, "Low peak differs from the reference", "Matala huippu eroaa referenssistä"),
                     tr (lang, "You ", "Sinä ") + noteText (peak.hz) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "ref " + noteText (refPeak.hz),
                     tr (lang, "The reference's strongest low end, usually its kick, is ", "Referenssin vahvin bassopää, yleensä sen kick, on ")
                         + num (lang, std::abs (diff), 0) + (diff > 0.0f ? tr (lang, " semitones lower than yours.", " puolisäveltä omaasi matalammalla.")
                                                                         : tr (lang, " semitones higher than yours.", " puolisäveltä omaasi korkeammalla.")),
                     tr (lang, "Not a rule: songs in different keys tune differently. But if the reference's low end is what you like, try your kick a few semitones towards ",
                               "Ei sääntö: eri sävellajien kappaleet viritetään eri tavoin. Mutta jos pidät referenssin bassopäästä, kokeile kickiä muutama puolisävel kohti ")
                         + hzText (refPeak.hz) + ".",
                     true);
        }
        if (bus)
        {
            std::array<float, Bands::count> refTotal {};
            for (int b = 0; b < Bands::count; ++b)
                refTotal[(size_t) b] = 10.0f * std::log10 (power (reference->midSong[(size_t) b]) + power (reference->sideSong[(size_t) b]) + 1.0e-30f);
            const auto refRel = relativeToTotal (refTotal);
            const float ours = bandsDb (rel, 20.0, 50.0) - bandsDb (rel, 63.0, 125.0);
            const float theirs = bandsDb (refRel, 20.0, 50.0) - bandsDb (refRel, 63.0, 125.0);
            const auto value = tr (lang, "Sub vs bass: you ", "Sub vs. basso: sinä ") + dB (ours) + ", ref " + dB (theirs);
            add.add ("ref-sub+", { ours - theirs - 3.0f, 3.0f }, 0.5f, tagCar, 1,
                     tr (lang, "More sub than the reference", "Enemmän subia kuin referenssissä"), value,
                     tr (lang, "Compared with the bass range, your kick and bass carry more sub than the reference's low end.",
                               "Bassoalueeseen verrattuna kickissäsi ja bassossasi on enemmän subia kuin referenssin bassopäässä."),
                     tr (lang, "Lower 30-50 Hz on the bass or the kick (whichever owns the sub) by about ", "Laske 30–50 Hz:ä bassosta tai kickistä (kumpi omistaa subin) noin ")
                         + num (lang, ours - theirs - 1.0f, 0) + " dB.", true);
            add.add ("ref-sub-", { theirs - ours - 3.0f, 3.0f }, 0.5f, tagClub, -1,
                     tr (lang, "Less sub than the reference", "Vähemmän subia kuin referenssissä"), value,
                     tr (lang, "Compared with the bass range, the reference carries more sub than your kick and bass.",
                               "Bassoalueeseen verrattuna referenssissä on enemmän subia kuin kickissäsi ja bassossasi."),
                     tr (lang, "Raise 35-50 Hz on the one that owns the sub by about ", "Nosta 35–50 Hz:ä siitä, joka omistaa subin, noin ")
                         + num (lang, theirs - ours - 1.0f, 0) + tr (lang, " dB, or check the high-pass filters.", " dB tai tarkista ylipäästösuotimet."), true);
            if (m.kickValid && reference->kickSongValid)
                add.add ("ref-kick", { reference->lowContrastSong - m.lowContrast - 3.0f, 3.0f }, 0.5f, tagClub | tagCar, -1,
                         tr (lang, "Kick stands out less than in the reference", "Kick erottuu vähemmän kuin referenssissä"),
                         tr (lang, "Kick over bass: you ", "Kick basson yli: sinä ") + dB (m.lowContrast) + ", ref " + dB (reference->lowContrastSong),
                         tr (lang, "In the reference the kick punches further above the bass.", "Referenssissä kick iskee selvemmin basson yläpuolelle."),
                         tr (lang, "More ducking on the bass, a shorter bass attack, or the kick 1-2 dB up.", "Enemmän duckausta bassoon, lyhyempi basson attack tai kick 1–2 dB ylös."),
                         true);
            if (reference->duckSong > 2.0f)
                add.add ("ref-duck", { reference->duckSong - m.duck - 2.5f, 2.0f }, 0.4f, tagClub, -1,
                         tr (lang, "Less ducking than the reference", "Vähemmän duckausta kuin referenssissä"),
                         tr (lang, "Ducking: you ", "Duckaus: sinä ") + num (lang, m.duck) + " dB, ref " + num (lang, reference->duckSong) + " dB",
                         tr (lang, "The reference's bass dips clearly at every kick; that's a big part of its groove and its clean low end.",
                                   "Referenssin basso notkahtaa selvästi jokaisella kickillä; se on iso osa sen groovea ja puhdasta bassopäätä."),
                         tr (lang, "Deepen the sidechain on the bass by about ", "Syvennä basson sidechainia noin ")
                             + num (lang, reference->duckSong - m.duck, 0) + " dB.", true);
        }
    }

    // ---- order, score, passed checks, playback
    std::stable_sort (a.findings.begin(), a.findings.end(), [] (const Finding& x, const Finding& y)
    {
        if (x.severity != y.severity)
            return (int) x.severity > (int) y.severity;
        return x.excess > y.excess;
    });
    a.score = juce::jlimit (0.0f, 100.0f, 100.0f * std::exp (-penalty / 60.0f));

    {
        auto present = [&a] (std::initializer_list<const char*> ids)
        {
            for (const auto& f : a.findings)
                for (auto* id : ids)
                    if (f.id == id)
                        return true;
            return false;
        };
        auto pass = [&] (const char* id, juce::String title, juce::String value)
        {
            Finding f;
            f.id = id;
            f.severity = Severity::good;
            f.title = std::move (title);
            f.value = std::move (value);
            a.passed.push_back (std::move (f));
        };
        if (! present ({ "lowwidth", "monoloss" }))
            pass ("ok-mono", tr (lang, "Low end is mono", "Bassopää on mono"),
                  m.lowWidth < -60.0f ? tr (lang, "fully mono", "täysin mono") : tr (lang, "side ", "sivu ") + num (lang, m.lowWidth) + " dB");
        if (! present ({ "track-hot", "clipping" }))
            pass ("ok-headroom", tr (lang, "Headroom left on the track", "Raidalla on tilaa"), num (lang, juce::jmax (m.truePeak, m.samplePeak)) + " dBFS");
        if (! present ({ "rumble" }))
            pass ("ok-rumble", tr (lang, "No rumble below 30 Hz", "Ei jyrinää alle 30 Hz:ssä"), {});
        if (kick && ! present ({ "kick-noclick" }))
            pass ("ok-click", tr (lang, "Kick has a click for small speakers", "Kickissä on klikki pieniä kaiuttimia varten"), {});
        if (kick && (song ? s.kickDecaySongValid : s.kickDecayNowValid) && ! present ({ "kick-long", "kick-short" }))
            pass ("ok-length", tr (lang, "Kick length fits the groove", "Kickin pituus sopii grooveen"),
                  juce::String (juce::roundToInt (song ? s.kickDecaySong : s.kickDecayNow)) + " ms");
        if ((bass || bus) && ! present ({ "bass-noharm" }))
            pass ("ok-phone", tr (lang, "Bass carries on small speakers", "Basso kantaa pienistä kaiuttimista"),
                  tr (lang, "enough harmonics at 200-800 Hz", "tarpeeksi harmonisia 200–800 Hz:ssä"));
        if (bus && m.kickValid && ! present ({ "kickbass" }))
            pass ("ok-kick", tr (lang, "Kick stands out from the bass", "Kick erottuu bassosta"),
                  num (lang, m.lowContrast) + tr (lang, " dB above the bass", " dB basson yläpuolella"));
        if (partner != nullptr && ! present ({ "clash", "kick-tune" }))
            pass ("ok-clash", tr (lang, "Kick and bass have their own places", "Kickillä ja bassolla on omat paikkansa"), {});
    }

    {
        const int tags[] = { tagPhone, tagCar, tagHeadphones, tagClub };
        const char* goodEn[] = { "Click and harmonics carry on small speakers.", "Low end under control, nothing booms.",
                                 "Centred and clean.", "Mono low end with room for the kick." };
        const char* goodFi[] = { "Klikki ja harmoniset kantavat pienistä kaiuttimista.", "Bassopää hallussa, mikään ei jumputa.",
                                 "Keskellä ja puhdas.", "Mono-bassopää, jossa kickillä on tilaa." };
        for (int i = 0; i < (int) Playback::count; ++i)
        {
            float pen = 0.0f;
            juce::StringArray worst;
            for (const auto& f : a.findings)
                if ((f.tags & tags[i]) != 0 && (f.severity == Severity::fix || f.severity == Severity::watch))
                {
                    pen += f.penalty;
                    if (worst.size() < 2)
                        worst.add (f.title);
                }
            auto& t = a.translation[(size_t) i];
            t.score = 100.0f * std::exp (-pen / 35.0f);
            t.note = worst.isEmpty() ? tr (lang, goodEn[i], goodFi[i]) : worst.joinIntoString (". ") + ".";
        }
    }

    {
        int count = 0;
        for (const auto& f : a.findings)
            if (f.severity == Severity::fix || f.severity == Severity::watch)
                ++count;
        a.nextStep = count == 0 ? tr (lang, "This track looks fine on the meter. Now judge it with the rest of the mix playing.",
                                            "Raita näyttää mittarissa hyvältä. Arvioi se nyt muun miksin soidessa.")
                                : tr (lang, "Fix the top finding on this track, then listen to it with the whole mix.",
                                            "Korjaa tämän raidan ylin havainto ja kuuntele sitten koko miksin kanssa.");
    }

    if (a.findings.empty() || std::all_of (a.findings.begin(), a.findings.end(), [] (const Finding& f) { return f.severity == Severity::info; }))
    {
        Finding f;
        f.id = "good";
        f.severity = Severity::good;
        f.title = tr (lang, "Every track check is in range", "Kaikki raidan tarkistukset ovat kohdallaan");
        f.why = tr (lang, "Numbers on one track are only half the story: solo sounds that measure well can still mask each other. Listen with the whole mix, quietly, and on a phone.",
                          "Yhden raidan luvut ovat vain puoli totuutta: soolona hyvin mittautuvat äänet voivat silti peittää toisiaan. Kuuntele koko miksin kanssa, hiljaa ja puhelimesta.");
        a.findings.insert (a.findings.begin(), f);
    }
    return a;
}
} // namespace wwma
