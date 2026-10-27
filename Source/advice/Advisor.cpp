#include "Advisor.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
juce::String tr (Lang lang, const char* english, const char* finnish)
{
    return lang == Lang::finnish ? juce::String (juce::CharPointer_UTF8 (finnish)) : juce::String (english);
}

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

    juce::String freq (Lang lang, double hz)
    {
        if (hz >= 1000.0)
            return num (lang, (float) (hz / 1000.0), hz >= 10000.0 ? 0 : 1) + " kHz";
        return juce::String ((int) std::lround (hz)) + " Hz";
    }

    float power (float db) { return std::pow (10.0f, db / 10.0f); }

    /** Severity from how far a value is outside its range: over <= 0 is fine,
        up to one "scale" is slightly off, beyond that clearly off. */
    struct Check
    {
        float over, scale;
        bool outside() const { return over > 0.0f; }
        float excess() const { return over / scale; }
        Severity severity() const { return excess() > 1.0f ? Severity::fix : Severity::watch; }
    };

    int directionOf (const std::string& id)
    {
        if (! id.empty() && id.back() == '+') return 1;
        if (! id.empty() && id.back() == '-') return -1;
        for (const char* up : { "loud", "peak", "squashed", "lra", "lowwidth", "wide", "spikydrums", "boomnote", "piercing",
                                "clipping", "headroom", "hotmix", "rumble", "sibilance", "sidehigh", "monoloss" })
            if (id == up) return 1;
        for (const char* down : { "quiet", "dynamic", "phonebass", "narrow", "kickbass", "flatdrums", "notch" })
            if (id == down) return -1;
        return 0;
    }

    struct Builder
    {
        Lang lang;
        std::vector<Finding>& out;
        float& penalty;

        void add (const char* id, Check c, float weight, int tags, juce::String title, juce::String value,
                  juce::String why, juce::String howTo, bool watchOnly = false)
        {
            if (! c.outside())
                return;
            Finding f;
            f.id = id;
            f.excess = c.excess();
            f.severity = watchOnly ? Severity::watch : c.severity();
            f.tags = tags;
            f.title = std::move (title);
            f.value = std::move (value);
            f.why = std::move (why);
            f.howTo = std::move (howTo);
            f.penalty = weight * 14.0f * std::min (1.6f, c.excess());
            f.direction = directionOf (f.id);
            penalty += f.penalty;
            out.push_back (std::move (f));
        }
    };

    struct RangeText { const char *moreEn, *moreFi, *lessEn, *lessFi, *whyMoreEn, *whyMoreFi, *howMoreEn, *howMoreFi,
                                   *whyLessEn, *whyLessFi, *howLessEn, *howLessFi; int tagsMore, tagsLess; };

    const RangeText rangeTexts[numRanges] = {
        { "Too much sub (below 56 Hz)", "Liikaa subbassoa (alle 56 Hz)",
          "Not enough sub (below 56 Hz)", "Liian vähän subbassoa (alle 56 Hz)",
          "Phones can't play this range at all and a car cabin boosts it. It eats headroom, so the limiter works harder and the whole mix gets quieter and muddier.",
          "Puhelin ei toista tätä aluetta lainkaan ja auton matkustamo korostaa sitä. Se syö tilaa, joten limitteri tekee enemmän töitä ja koko miksaus muuttuu hiljaisemmaksi ja mutaisemmaksi.",
          "1. Decide which sound owns the sub: the kick or the bass/808. Only one should dominate it at a time.\n2. High-pass everything else (pads, synths, vocals, FX) at 80-150 Hz.\n3. Lower the sub of the bass 2-3 dB with a low shelf, or turn the sub layer down.\nTrap: small monitors and most rooms can't show the sub honestly, so it's easy to pile on more. Check on good headphones and in a car.",
          "1. Päätä, kumpi omistaa subin: kick vai basso/808. Vain toinen saa hallita sitä kerrallaan.\n2. Ylipäästösuodata kaikki muu (padit, syntikat, vokaalit, efektit) 80–150 Hz:iin.\n3. Laske basson subia 2–3 dB alahyllyllä tai laske sub-kerroksen tasoa.\nSudenkuoppa: pienet monitorit ja useimmat huoneet eivät näytä subia rehellisesti, joten sitä tulee helposti lisättyä. Tarkista hyvillä kuulokkeilla ja autossa.",
          "On a club system, good headphones and in a car the low end will feel thin and weak.",
          "Klubilla, hyvissä kuulokkeissa ja autossa bassopää kuulostaa ohuelta ja voimattomalta.",
          "1. Check that the kick and the bass have content around 40-55 Hz (solo them and look at the spectrum).\n2. Add a sine sub layer under the bass, or a gentle low shelf (+2 dB at 50 Hz).\n3. Make sure high-pass filters on the kick and bass aren't set too high.\nTrap: a room that booms makes you turn the sub down too far. Trust the meter and headphones here.",
          "1. Tarkista, että kickissä ja bassossa on sisältöä noin 40–55 Hz:ssä (soolaa ne ja katso spektriä).\n2. Lisää basson alle siniaaltosubi tai loiva alahylly (+2 dB 50 Hz:ssä).\n3. Varmista, ettei kickin ja basson ylipäästösuodin ole liian korkealla.\nSudenkuoppa: jumputtava huone saa laskemaan subia liikaa. Luota tässä mittariin ja kuulokkeisiin.",
          tagCar | tagPhone, tagClub | tagCar },
        { "Too much bass (56-140 Hz)", "Liikaa bassoa (56–140 Hz)",
          "Thin bass (56-140 Hz)", "Ohut basso (56–140 Hz)",
          "This is exactly where a car cabin booms, and where the kick and the bass fight for space.",
          "Juuri tällä alueella auton matkustamo jumputtaa, ja täällä kick ja basso tappelevat tilasta.",
          "1. Cut 2-3 dB around 80-120 Hz on the bass, or turn the bass down a little.\n2. Let the kick duck the bass (see the kick vs bass check).\n3. Give kick and bass different peaks: e.g. kick fundamental at 55-60 Hz, bass body at 90-110 Hz, or the other way round.",
          "1. Leikkaa bassosta 2–3 dB noin 80–120 Hz:stä tai laske bassoa hieman.\n2. Anna kickin duckata bassoa (katso kick vs. basso -tarkistus).\n3. Anna kickille ja bassolle eri huiput: esim. kickin perustaajuus 55–60 Hz ja basson runko 90–110 Hz, tai toisin päin.",
          "Kick and bass lack weight. This is the part of the low end small speakers can still partly play.",
          "Kickistä ja bassosta puuttuu painoa. Tämä on se osa bassoa, jota pienetkin kaiuttimet vielä osittain toistavat.",
          "1. Raise the body of the kick/bass around 80-120 Hz by 2-3 dB.\n2. Saturation on the bass adds harmonics that also help small speakers.\n3. Check the bass isn't masked by pads or chords in the same range.",
          "1. Nosta kickin/basson runkoa noin 80–120 Hz:stä 2–3 dB.\n2. Saturaatio bassossa lisää harmonisia, jotka auttavat myös pienissä kaiuttimissa.\n3. Tarkista, etteivät padit tai soinnut peitä bassoa samalla alueella.",
          tagCar, tagCar | tagClub },
        { "Muddy low mids (140-450 Hz)", "Mutaiset alakeskiäänet (140–450 Hz)",
          "Hollow low mids (140-450 Hz)", "Ontot alakeskiäänet (140–450 Hz)",
          "Too much here sounds boxy and muddy. On phones and in cars this range masks the vocal and the snare.",
          "Liika tällä alueella kuulostaa laatikkomaiselta ja mutaiselta. Puhelimessa ja autossa se peittää vokaalin ja virvelin.",
          "1. Sweep a narrow boost between 200 and 400 Hz to find the boxy spot, then cut 2-4 dB there.\n2. Usual suspects: pads, guitars, bass harmonics and reverb returns.\n3. High-pass reverbs and delays at 200-300 Hz.\nTrap: every sound with a full low end sounds great in solo; together they turn to mud. Decide which parts really need this range.",
          "1. Pyyhkäise kapeaa korostusta 200–400 Hz:n välillä, etsi laatikkomainen kohta ja leikkaa siitä 2–4 dB.\n2. Tavalliset epäillyt: padit, kitarat, basson harmoniset ja kaikupaluut.\n3. Ylipäästösuodata kaiut ja viiveet 200–300 Hz:iin.\nSudenkuoppa: jokainen täyteläinen ääni kuulostaa soolona hienolta, yhdessä ne muuttuvat mudaksi. Päätä, mitkä osat oikeasti tarvitsevat tätä aluetta.",
          "The mix loses warmth and body; the snare and the vocal sound thin.",
          "Miksaus menettää lämpöä ja runkoa; virveli ja vokaali kuulostavat ohuilta.",
          "1. Bring up the body of the snare (~200 Hz), the vocal or the chords by 1-3 dB.\n2. Check you haven't high-passed too much: 'high-pass everything' is good advice only up to a point.",
          "1. Nosta virvelin runkoa (~200 Hz), vokaalia tai sointuja 1–3 dB.\n2. Tarkista, ettet ole suodattanut liikaa: “ylipäästö kaikkeen” on hyvä neuvo vain tiettyyn rajaan asti.",
          tagPhone | tagCar, 0 },
        { "Honky, forward mids (450 Hz-2.2 kHz)", "Tukkoinen keskiääni (450 Hz – 2,2 kHz)",
          "Scooped mids (450 Hz-2.2 kHz)", "Kuopallinen keskiääni (450 Hz – 2,2 kHz)",
          "Sounds nasal and tiring, especially on laptop and phone speakers that already emphasise this range.",
          "Kuulostaa nenäkkäältä ja väsyttävältä, etenkin läppärin ja puhelimen kaiuttimista, jotka jo valmiiksi korostavat tätä aluetta.",
          "1. Find the loudest element in the mids (lead synth, vocal, chords).\n2. Turn it down 1-2 dB, or dip 2-3 dB around 800 Hz-1.5 kHz on it.",
          "1. Etsi keskialueen kovin elementti (liidisyntikka, vokaali, soinnut).\n2. Laske sitä 1–2 dB tai leikkaa siitä 2–3 dB noin 800 Hz – 1,5 kHz:stä.",
          "Phone speakers live almost only in this range. A scooped mix sounds big on monitors but empty on a phone: the lead and the vocal get lost.",
          "Puhelimen kaiutin toistaa lähes pelkästään tätä aluetta. Kuopallinen miksaus kuulostaa isolta monitoreista mutta tyhjältä puhelimesta: liidi ja vokaali katoavat.",
          "1. Turn the vocal or lead up 1-2 dB, or give it 1-2 dB around 1 kHz.\n2. Avoid a big 'smile' EQ on the master.\n3. Listen on your phone or in mono at low volume.",
          "1. Nosta vokaalia tai liidiä 1–2 dB tai korosta sitä 1–2 dB noin 1 kHz:stä.\n2. Vältä isoa “hymyä”-EQ:ta masterissa.\n3. Kuuntele puhelimesta tai monona hiljaisella.",
          tagPhone, tagPhone },
        { "Harsh upper mids (2.2-5.6 kHz)", "Kirkuvat yläkeskiäänet (2,2–5,6 kHz)",
          "Dull: missing presence (2.2-5.6 kHz)", "Tylsä: läsnäolo puuttuu (2,2–5,6 kHz)",
          "The ear is most sensitive here. Phone speakers and car tweeters exaggerate it, so it gets tiring fast.",
          "Korva on tällä herkimmillään. Puhelimen kaiutin ja auton diskanttikaiuttimet korostavat sitä, joten kuuntelu väsyttää nopeasti.",
          "1. Find the culprit: bright synths, hats, distorted leads, vocal 's' sounds.\n2. Cut it 2-3 dB around 3-4 kHz, or use a dynamic EQ / de-esser so it only cuts when it gets harsh.\nTrap: after hours of loud listening your ears go dull here, so you keep adding more. Take a break and check again at a lower volume.",
          "1. Etsi syyllinen: kirkkaat syntikat, hihatit, säröliidit, vokaalin s-äänteet.\n2. Leikkaa siitä 2–3 dB noin 3–4 kHz:stä tai käytä dynaamista EQ:ta / de-esseriä, joka leikkaa vain kun ääni kiristyy.\nSudenkuoppa: tuntien kovan kuuntelun jälkeen korva turtuu tälle alueelle, joten sitä tulee lisättyä. Pidä tauko ja tarkista hiljaisemmalla.",
          "Vocals and the snare lose definition and the mix sounds distant, especially at low volume.",
          "Vokaalit ja virveli menettävät tarkkuutensa ja miksaus kuulostaa kaukaiselta, etenkin hiljaisella.",
          "1. Add 1-2 dB around 3 kHz on the vocal/lead or the snare.\n2. Bring up a clap or hat layer slightly.",
          "1. Korosta vokaalia/liidiä tai virveliä 1–2 dB noin 3 kHz:stä.\n2. Nosta clap- tai hihat-kerrosta hieman.",
          tagPhone | tagCar, tagPhone },
        { "Too much top end (above 5.6 kHz)", "Liikaa diskanttia (yli 5,6 kHz)",
          "Dark, closed top end (above 5.6 kHz)", "Tumma, suljettu diskantti (yli 5,6 kHz)",
          "Hats, sibilance and noise get sharp and tiring, and lossy encoding (Bluetooth, streaming) smears this range.",
          "Hihatit, sihinä ja kohina muuttuvat teräviksi ja väsyttäviksi, ja häviöllinen pakkaus (Bluetooth, striimaus) sotkee tätä aluetta.",
          "1. Turn hats and cymbals down a little.\n2. De-ess the vocal.\n3. Or a gentle high shelf cut (-1...-2 dB at 10 kHz).",
          "1. Laske hihatteja ja symbaaleja hieman.\n2. De-essaa vokaali.\n3. Tai loiva ylähyllyleikkaus (−1…−2 dB 10 kHz:ssä).",
          "The mix sounds dull and lacks space, and it won't stand up next to other tracks.",
          "Miksaus kuulostaa tunkkaiselta ja ahtaalta, eikä se pärjää muiden kappaleiden rinnalla.",
          "1. A gentle high shelf (+1...+2 dB at 10-12 kHz) on the master or on the hats/vocal bus.\n2. Check you aren't low-passing too many sounds.",
          "1. Loiva ylähylly (+1…+2 dB 10–12 kHz:ssä) masteriin tai hihat-/vokaaliväylään.\n2. Tarkista, ettet alipäästösuodata liian montaa ääntä.",
          tagPhone | tagStreaming, 0 },
    };
}

juce::String rangeName (Range r, Lang lang)
{
    switch (r)
    {
        case Range::sub:      return tr (lang, "Sub", "Sub");
        case Range::bass:     return tr (lang, "Bass", "Basso");
        case Range::lowMid:   return tr (lang, "Low mids", "Alakeski");
        case Range::mid:      return tr (lang, "Mids", "Keski");
        case Range::presence: return tr (lang, "Presence", "Läsnäolo");
        case Range::air:      return tr (lang, "Air", "Ilma");
        case Range::count:    break;
    }
    return {};
}

juce::String tagName (Tag t, Lang lang)
{
    switch (t)
    {
        case tagPhone:      return tr (lang, "PHONE", "PUHELIN");
        case tagCar:        return tr (lang, "CAR", "AUTO");
        case tagClub:       return tr (lang, "CLUB", "KLUBI");
        case tagStreaming:  return tr (lang, "STREAMING", "STRIIMAUS");
        case tagHeadphones: return tr (lang, "HEADPHONES", "KUULOKKEET");
        default:            return {};
    }
}

juce::String playbackName (Playback p, Lang lang)
{
    switch (p)
    {
        case Playback::phone:      return tr (lang, "Phone", "Puhelin");
        case Playback::car:        return tr (lang, "Car", "Auto");
        case Playback::headphones: return tr (lang, "Headphones", "Kuulokkeet");
        case Playback::club:       return tr (lang, "Club", "Klubi");
        case Playback::count:      break;
    }
    return {};
}

juce::String verdictFor (float score, Lang lang)
{
    if (score >= 90.0f) return tr (lang, "Looks perfect", "Näyttää täydelliseltä");
    if (score >= 70.0f) return tr (lang, "Close enough", "Riittävän lähellä");
    if (score >= 40.0f) return tr (lang, "Not quite there yet", "Ei vielä ihan perillä");
    return tr (lang, "Needs improvement", "Kaipaa parannusta");
}

juce::String rangeNickname (Range r, Lang lang)
{
    switch (r)
    {
        case Range::sub:      return tr (lang, "Sub", "Sub");
        case Range::bass:     return tr (lang, "Bass", "Basso");
        case Range::lowMid:   return tr (lang, "Mud", "Muta");
        case Range::mid:      return tr (lang, "Mids", "Keski");
        case Range::presence: return tr (lang, "Harsh", "Särähdys");
        case Range::air:      return tr (lang, "Air", "Ilma");
        case Range::count:    break;
    }
    return {};
}

Assessment assess (const Snapshot& s, const Profile& p, Window w, Lang lang, Stage stage)
{
    const bool mastering = stage == Stage::mastering;
    Assessment a;
    auto& m = a.measures;
    const bool song = w == Window::song;

    const auto& mid = song ? s.midSong : s.midNow;
    const auto& side = song ? s.sideSong : s.sideNow;
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

    m.valid = spectrumValid && m.loudness > -50.0f && (! song || s.secondsAnalysed >= 5.0f);
    a.enoughData = m.valid;

    if (! m.valid)
    {
        Finding f;
        f.id = "listening";
        f.severity = Severity::info;
        f.title = tr (lang, "Play your track", "Soita kappaletta");
        f.why = song ? tr (lang, "I measure the whole song since the last reset. Play at least the loudest part (a drop or chorus); the longer, the better.",
                               "Mittaan koko kappaleen viimeisestä nollauksesta lähtien. Soita ainakin kovin osa (drop tai kertosäe); mitä pidempään, sen parempi.")
                     : tr (lang, "In NOW mode I listen to the last 3 seconds. Loop the part you are working on.",
                               "NYT-tilassa kuuntelen viimeiset 3 sekuntia. Looppaa kohtaa, jota työstät.");
        a.findings.push_back (f);
        return a;
    }

    // ---- tonal balance: combine mid and side (= L + R power), compare with the target
    std::array<float, Bands::count> total {};
    for (int b = 0; b < Bands::count; ++b)
        total[(size_t) b] = 10.0f * std::log10 (power (mid[(size_t) b]) + power (side[(size_t) b]) + 1.0e-30f);
    const auto rel = relativeToTotal (total);

    std::array<float, numRanges> dev {};
    for (int r = 0; r < numRanges; ++r)
    {
        const auto info = rangeBands ((Range) r);
        dev[(size_t) r] = sumBands (rel, info.firstBand, info.lastBand) - sumBands (p.target, info.firstBand, info.lastBand);
    }
    // One very loud range would make every other range look too quiet when everything is
    // relative to the total. Centre the deviations on their median to avoid that.
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

    // phone bass: how much 90-280 Hz carries compared to 28-88 Hz, versus the target
    const float measRatio = sumBands (rel, 7, 11) - sumBands (rel, 2, 6);
    const float targetRatio = sumBands (p.target, 7, 11) - sumBands (p.target, 2, 6);
    m.phoneBass = measRatio - targetRatio;

    m.lowWidth = sumBands (side, 0, 8) - sumBands (mid, 0, 8);
    m.width = sumBands (side, 0, Bands::count - 1) - sumBands (mid, 0, Bands::count - 1);

    // mono check: how much the mix loses when it's summed to mono (phones, club PAs, Bluetooth speakers).
    // Stereo power is mid + side; mono keeps only the mid. Three neighbouring bands at a time, loud ones only.
    {
        const float loudest = *std::max_element (total.begin(), total.end());
        for (int b = 10; b <= 26; ++b)
        {
            const float stereoDb = sumBands (total, b - 1, b + 1), monoDb = sumBands (mid, b - 1, b + 1);
            const float loss = stereoDb - monoDb;
            if (stereoDb > loudest - 20.0f && loss > m.monoLoss)
            {
                m.monoLoss = loss;
                m.monoLossBand = b;
            }
        }
        m.monoLossBass = sumBands (total, 0, 8) - sumBands (mid, 0, 8);
    }

    float penalty = 0.0f;
    Builder add { lang, a.findings, penalty };
    const auto dB = [lang] (float v) { return num (lang, v, 1, true) + " dB"; };
    const auto range = [lang] (float lo, float hi) { return num (lang, lo) + juce::String (juce::CharPointer_UTF8 ("…")) + num (lang, hi); };

    // ---- loudness
    {
        const auto label = song ? tr (lang, "Integrated", "Integroitu") : tr (lang, "Short-term", "Lyhytaikainen");
        const auto value = label + " " + num (lang, m.loudness) + " LUFS (" + tr (lang, "style", "tyyli") + " " + range (p.lufsMin, p.lufsMax) + ")";
        if (mastering)
        add.add ("loud", { m.loudness - p.lufsMax - 0.5f, 2.5f }, 0.8f, tagStreaming | tagPhone,
                 tr (lang, "Louder than needed", "Turhan kova"), value,
                 tr (lang, "Extra loudness costs punch and clarity. Streaming services turn loud masters down to about -14 LUFS anyway, so only the damage is left.",
                           "Ylimääräinen äänekkyys syö iskevyyttä ja selkeyttä. Striimauspalvelut laskevat kovat masterit joka tapauksessa noin −14 LUFS:iin, joten jäljelle jää vain haitta."),
                 tr (lang, "1. Lower the input gain of your final limiter (or raise its threshold) until you are inside the range.\n2. Compare at matched loudness: the louder version always sounds 'better' for a moment.",
                           "1. Laske viimeisen limitterin sisääntulovahvistusta (tai nosta kynnystä), kunnes olet alueella.\n2. Vertaa samalla äänenvoimakkuudella: kovempi versio kuulostaa aina hetken “paremmalta”."));
        if (song && mastering)
            add.add ("quiet", { p.lufsMin - m.loudness - 0.5f, 3.0f }, 0.5f, tagClub,
                     tr (lang, "Quieter than usual for this style", "Hiljaisempi kuin tyylille tavallista"), value,
                     tr (lang, "In a DJ set, or a playlist without loudness normalisation, it will sound weaker than the tracks around it. On streaming it's fine.",
                               "DJ-setissä tai soittolistalla ilman normalisointia se kuulostaa heikommalta kuin ympäröivät kappaleet. Striimauksessa tämä ei haittaa."),
                     tr (lang, "1. Raise the limiter input a little at a time.\n2. If the limiter starts to pump before you get there, control peaks earlier: a bus compressor or clipper on the drums, or tame the loudest sub notes.",
                               "1. Nosta limitterin sisääntuloa vähän kerrallaan.\n2. Jos limitteri alkaa pumpata ennen sitä, hallitse huippuja aiemmin: väyläkompressori tai klipperi rumpuihin, tai kesytä kovimmat subinuotit."));
    }

    // ---- true peak
    if (mastering)
    add.add ("peak", { m.truePeak - p.truePeakMax, std::max (0.3f, -p.truePeakMax) }, 0.6f, tagPhone | tagCar | tagStreaming,
             tr (lang, "Peaks too close to 0 dB", "Huiput liian lähellä nollaa"),
             tr (lang, "True peak ", "True peak ") + num (lang, m.truePeak) + " dBTP (" + tr (lang, "ceiling", "katto") + " " + num (lang, p.truePeakMax) + ")",
             tr (lang, "MP3, AAC and Bluetooth encoding add overshoots. Peaks above the ceiling distort on phones, in cars and on streaming services.",
                       "MP3-, AAC- ja Bluetooth-pakkaus lisäävät ylityksiä. Katon ylittävät huiput säröytyvät puhelimissa, autoissa ja striimauksessa."),
             tr (lang, "1. Set the ceiling (output) of your final limiter to ", "1. Aseta viimeisen limitterin katto (output) arvoon ")
                 + num (lang, p.truePeakMax) + " dBTP.\n"
                 + tr (lang, "2. Turn on its true peak / ISP mode.\nTrap: normalising, resampling or adding a fade after the limiter can push peaks back over. Make the limiter the very last thing.", "2. Kytke sen true peak- / ISP-tila päälle.\nSudenkuoppa: normalisointi, näytetaajuusmuunnos tai feidi limitterin jälkeen voi nostaa huiput taas yli. Pidä limitteri aivan viimeisenä."));

    // ---- dynamics
    {
        const float lo = song ? p.plrMin : p.plrMin - 1.0f, hi = song ? p.plrMax : p.plrMax - 1.0f;
        const auto value = "PLR " + num (lang, m.plr) + " LU (" + tr (lang, "style", "tyyli") + " " + range (lo, hi) + ")";
        add.add ("squashed", { lo - m.plr, 2.0f }, 1.0f, tagPhone | tagCar,
                 tr (lang, "Over-compressed: punch is gone", "Liian puristettu: iskevyys katoaa"), value,
                 tr (lang, "When peaks hardly rise above the average level, the kick and the snare lose their impact and everything sounds flat and tiring, especially on small speakers.",
                           "Kun huiput tuskin nousevat keskitason yläpuolelle, kick ja virveli menettävät iskunsa ja kaikki kuulostaa litteältä ja väsyttävältä, etenkin pienistä kaiuttimista."),
                 tr (lang, "1. Back off the limiter by 1-3 dB.\n2. Use a slower attack (10-30 ms) on the mix bus or drum bus compressor so transients get through.\n3. Don't stack many compressors all working hard: let each do 1-3 dB.\nTrap: the louder version always sounds better for a moment. Compare at matched loudness before you decide (the loudness war loses on streaming anyway).",
                           "1. Höllennä limitteriä 1–3 dB.\n2. Käytä hitaampaa attackia (10–30 ms) master- tai rumpuväylän kompressorissa, jotta transientit pääsevät läpi.\n3. Älä pinoa monta kovaa kompressoria: anna kunkin tehdä 1–3 dB.\nSudenkuoppa: kovempi versio kuulostaa aina hetken paremmalta. Vertaa samalla äänekkyydellä ennen päätöstä (äänekkyyssota häviää striimauksessa joka tapauksessa)."));
        add.add ("dynamic", { m.plr - hi, 3.0f }, 0.6f, tagCar | tagPhone,
                 tr (lang, "Very dynamic for this style", "Tyyliin nähden hyvin dynaaminen"), value,
                 tr (lang, "Quiet details vanish under road noise in a car and on phone speakers, and the peaks limit how loud the track can be.",
                           "Hiljaiset yksityiskohdat katoavat autossa tienäänen alle ja puhelimen kaiuttimesta, ja huiput rajoittavat sitä, kuinka kova kappale voi olla."),
                 tr (lang, "1. Gentle mix bus compression: ratio 2:1, attack 10-30 ms, auto or ~100 ms release, 1-3 dB gain reduction.\n2. Then a little more limiting.\n3. Check the quiet parts in a car, or with a car simulator such as DDR.",
                           "1. Kevyt masterväylän kompressio: suhde 2:1, attack 10–30 ms, release auto tai ~100 ms, 1–3 dB vaimennus.\n2. Sitten hieman lisää limitointia.\n3. Tarkista hiljaiset kohdat autossa tai autosimulaattorilla, kuten DDR:llä."));
    }

    if (song && s.secondsAnalysed >= 30.0f)
        add.add ("lra", { m.lra - p.lraMax, 4.0f }, 0.4f, tagCar,
                 tr (lang, "Big level jumps between sections", "Suuret tasoerot osien välillä"),
                 "LRA " + num (lang, m.lra) + " LU (" + tr (lang, "style max ", "tyylin maksimi ") + num (lang, p.lraMax) + ")",
                 tr (lang, "People set the volume once in a car or on the go. Quiet intros and breakdowns disappear, and the loud parts jump out.",
                           "Autossa ja liikkeellä äänenvoimakkuus säädetään kerran. Hiljaiset introt ja breakdownit katoavat ja kovat osat hyppäävät esiin."),
                 tr (lang, "1. Automate quiet sections up a few dB.\n2. Or use gentle mix bus compression. Breakdowns can be softer, but rarely more than 6-8 LU.",
                           "1. Automatisoi hiljaisia osia muutama dB ylöspäin.\n2. Tai käytä kevyttä masterväylän kompressiota. Breakdown saa olla hiljaisempi, mutta harvoin yli 6–8 LU."));

    // ---- tonal ranges
    for (int r = 0; r < numRanges; ++r)
    {
        const auto& t = rangeTexts[r];
        const float d = dev[(size_t) r];
        const float tol = p.tolerance * ((Range) r == Range::sub || (Range) r == Range::air ? 1.3f : 1.0f);
        const auto value = num (lang, d, 1, true) + tr (lang, " dB vs target", " dB tavoitteesta");
        const auto id = std::string ("range") + std::to_string (r);
        if (d > 0.0f)
            add.add ((id + "+").c_str(), { d - tol, tol }, 1.0f, t.tagsMore, tr (lang, t.moreEn, t.moreFi), value,
                     tr (lang, t.whyMoreEn, t.whyMoreFi), tr (lang, t.howMoreEn, t.howMoreFi));
        else
            add.add ((id + "-").c_str(), { -d - tol, tol }, 1.0f, t.tagsLess, tr (lang, t.lessEn, t.lessFi), value,
                     tr (lang, t.whyLessEn, t.whyLessFi), tr (lang, t.howLessEn, t.howLessFi));
    }

    // ---- small speakers: does the bass have harmonics?
    add.add ("phonebass", { -m.phoneBass - 3.0f, 3.0f }, 1.0f, tagPhone,
             tr (lang, "Bass will disappear on phones", "Basso katoaa puhelimessa"),
             tr (lang, "90-280 Hz vs sub: ", "90–280 Hz suhteessa subiin: ") + dB (m.phoneBass) + tr (lang, " vs target", " tavoitteesta"),
             tr (lang, "Phone and laptop speakers can't play below ~150 Hz. If the kick and the bass live only in the sub, nothing is left on small speakers.",
                       "Puhelimen ja läppärin kaiuttimet eivät toista alle ~150 Hz. Jos kick ja basso elävät vain subissa, pieniin kaiuttimiin ei jää mitään."),
             tr (lang, "1. Saturate or distort the bass in parallel (mix 20-40 %) to add harmonics, or add a layer an octave up.\n2. Give the kick a click or body around 100-200 Hz.\n3. Check on your phone or in mono at low volume.",
                       "1. Saturoi tai säröytä bassoa rinnakkain (mix 20–40 %) harmonisten lisäämiseksi tai lisää kerros oktaavia ylempää.\n2. Anna kickille klikkiä tai runkoa noin 100–200 Hz:iin.\n3. Tarkista puhelimesta tai monona hiljaisella."));

    // ---- stereo
    add.add ("lowwidth", { m.lowWidth - p.lowWidthMax, 6.0f }, 0.8f, tagPhone | tagClub,
             tr (lang, "Low end too wide", "Bassopää liian leveä"),
             tr (lang, "Side below 140 Hz: ", "Sivusignaali alle 140 Hz: ") + num (lang, m.lowWidth) + " dB (max " + num (lang, p.lowWidthMax) + ")"
                     + (m.monoLossBass >= 0.5f ? tr (lang, ", bass ", ", basso ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + num (lang, m.monoLossBass) + tr (lang, " dB in mono", " dB monona") : juce::String()),
             tr (lang, "Stereo bass collapses or cancels on mono phone speakers, club systems and Bluetooth speakers, and it blurs the low end.",
                       "Stereobasso romahtaa tai kumoutuu monokaiuttimissa (puhelin, klubi, Bluetooth) ja sumentaa bassopäätä."),
             tr (lang, "1. Keep everything below ~120 Hz mono: use a 'bass mono' / mono-maker tool on the bass bus or the master.\n2. Avoid wideners, chorus and stereo reverbs on bass sounds.\nTrap: a stereo widener or 'imager' on the master widens the bass too. Use one with a low-frequency mono option, or none.",
                       "1. Pidä kaikki alle ~120 Hz monona: käytä “bass mono”- / mono-maker-työkalua bassoväylässä tai masterissa.\n2. Vältä leventäjiä, chorusta ja stereokaikuja bassosäänissä.\nSudenkuoppa: stereoleventäjä tai “imager” masterissa levittää myös basson. Käytä sellaista, jossa on matalien mono-asetus, tai älä käytä ollenkaan."));

    add.add ("phase", { 0.3f - m.correlation, 0.3f }, 1.2f, tagPhone | tagClub,
             tr (lang, "Phase problems: parts cancel in mono", "Vaiheongelma: osia katoaa monona"),
             tr (lang, "Correlation ", "Korrelaatio ") + num (lang, m.correlation, 2),
             tr (lang, "Phones, many Bluetooth speakers and club systems play mono. Out-of-phase parts get quieter or vanish.",
                       "Puhelimet, monet Bluetooth-kaiuttimet ja klubit soittavat monona. Vastavaiheiset osat hiljenevät tai katoavat."),
             tr (lang, "1. Press mono and listen for what disappears.\n2. Usual causes: stereo wideners, Haas delays, phase-inverted layers, wide chorus.\n3. Reduce the width of that element or flip its polarity.",
                       "1. Kytke mono ja kuuntele, mikä katoaa.\n2. Tavalliset syyt: stereoleventäjät, Haas-viiveet, vastavaiheiset kerrokset, leveä chorus.\n3. Kavenna sen elementin leveyttä tai käännä sen polariteetti."));

    if (m.correlation >= 0.3f && m.monoLossBand >= 0)
    {
        const auto where = freq (lang, Bands::centre (m.monoLossBand));
        add.add ("monoloss", { m.monoLoss - 4.0f, 3.0f }, 0.6f, tagPhone | tagClub,
                 tr (lang, "Gets quieter in mono around ", "Hiljenee monona noin ") + where,
                 juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + num (lang, m.monoLoss) + tr (lang, " dB in mono", " dB monona"),
                 tr (lang, "Something here is mostly on the sides, so it drops when the mix is played in mono: on a phone, a Bluetooth speaker or a club PA. Often a wide synth, a stereo widener or a chorus.",
                           "Jokin tässä on enimmäkseen sivuilla, joten se hiljenee, kun miksausta soitetaan monona: puhelimessa, Bluetooth-kaiuttimessa tai klubin PA:ssa. Usein leveä syna, stereoleventäjä tai chorus."),
                 tr (lang, "1. Press mono on your monitor controller or master and listen for what drops around ",
                           "1. Kytke mono kuuntelusta tai masterista ja kuuntele, mikä hiljenee noin ")
                     + where + tr (lang, ".\n2. Narrow that sound (width 60-80 %), or keep a mono copy of it in the centre and blend the wide one on top.\n3. Check again in mono: the part should stay, just less wide.\nTrap: a 'super wide' preset or a Haas delay sounds huge on headphones and is the first thing to vanish in a club.",
                                      ".\n2. Kavenna sitä ääntä (leveys 60–80 %), tai pidä siitä monokopio keskellä ja sekoita leveä päälle.\n3. Tarkista uudelleen monona: osan pitää jäädä, vain kapeampana.\nSudenkuoppa: ”super wide” -preset tai Haas-viive kuulostaa valtavalta kuulokkeilla ja katoaa klubilla ensimmäisenä."));
    }

    if (m.correlation >= 0.3f)
        add.add ("wide", { m.width - p.widthMax, 4.0f }, 0.5f, tagPhone,
                 tr (lang, "Very wide stereo image", "Hyvin leveä stereokuva"),
                 tr (lang, "Side vs mid: ", "Sivu vs. keski: ") + num (lang, m.width) + " dB",
                 tr (lang, "Wide parts get quieter in mono, so the balance on a phone changes from what you hear on monitors.",
                           "Leveät osat hiljenevät monona, joten tasapaino puhelimessa muuttuu siitä, mitä kuulet monitoreista."),
                 tr (lang, "1. Narrow the widest elements (pads, FX, wide synths) a little.\n2. Check in mono that the lead and the vocal stay in place.",
                           "1. Kavenna leveimpiä elementtejä (padit, efektit, leveät syntikat) hieman.\n2. Tarkista monona, että liidi ja vokaali pysyvät paikallaan."));

    add.add ("narrow", { p.widthMin - m.width, 4.0f }, 0.3f, tagHeadphones | tagCar,
             tr (lang, "Narrow, almost mono mix", "Kapea, lähes mono miksaus"),
             tr (lang, "Side vs mid: ", "Sivu vs. keski: ") + num (lang, m.width) + " dB",
             tr (lang, "Fine on a phone, but on headphones and in a car it sounds small.",
                       "Puhelimessa ok, mutta kuulokkeilla ja autossa se kuulostaa pieneltä."),
             tr (lang, "1. Pan supporting parts (hats, percussion, chords, doubled synths) left and right.\n2. Keep the kick, bass, snare and lead vocal in the centre.",
                       "1. Panoroi tukisoittimia (hihatit, perkussiot, soinnut, tuplatut syntikat) vasemmalle ja oikealle.\n2. Pidä kick, basso, virveli ja päävokaali keskellä."),
             true);

    // ---- level balance between left and right
    {
        const float bal = song ? s.balanceSong : s.balanceNow;
        m.balance = bal;
        const bool left = bal > 0.0f;
        add.add ("balance", { std::abs (bal) - 1.5f, 1.5f }, 0.4f, tagHeadphones | tagClub,
                 left ? tr (lang, "Mix leans to the left", "Miksaus kallistuu vasemmalle") : tr (lang, "Mix leans to the right", "Miksaus kallistuu oikealle"),
                 tr (lang, "Left vs right: ", "Vasen vs. oikea: ") + dB (bal),
                 tr (lang, "One side is clearly louder. On headphones it pulls your head to one side, and in a club only half the room gets the full mix. A common cause is a loud hat, shaker or guitar panned to one side with nothing to balance it.",
                           "Toinen puoli on selvästi kovempi. Kuulokkeilla se vetää päätä sivulle, ja klubilla vain puolet salista saa koko miksauksen. Tavallinen syy on yhdelle puolelle panoroitu kova hihat, shaker tai kitara, jolla ei ole vastapainoa."),
                 tr (lang, "1. Solo each panned part and check which one is loud on the heavy side.\n2. Pan something of similar weight to the other side, or bring the panned part closer to the centre.\n3. Check the master bus: a stereo effect or a channel volume left at the wrong value tilts everything.",
                           "1. Soolaa panoroidut osat yksi kerrallaan ja etsi, mikä on kova painavammalla puolella.\n2. Panoroi jotain yhtä painavaa toiselle puolelle tai tuo panoroitu osa lähemmäs keskiötä.\n3. Tarkista masterväylä: stereoefekti tai väärään arvoon jäänyt kanavan voimakkuus kallistaa kaiken."));
    }

    // ---- DC offset, clipping, headroom
    m.dcOffset = s.dcOffset;
    m.clipEvents = s.clipEvents;
    m.samplePeak = s.samplePeakMax;
    if (song)
        add.add ("dcoffset", { s.dcOffset + 42.0f, 8.0f }, 0.3f, tagClub,
                 tr (lang, "DC offset", "DC-siirtymä"),
                 "DC " + num (lang, s.dcOffset) + " dBFS",
                 tr (lang, "The waveform sits off centre. It wastes headroom, makes the limiter react unevenly and can click at cuts and edits. Usually it comes from one sample, a distortion plugin or an analog-modelled processor.",
                           "Aaltomuoto ei ole keskellä. Se hukkaa tilaa, saa limitterin reagoimaan epätasaisesti ja voi naksua leikkauksissa. Yleensä syy on yksi sample, säröplugin tai analogia mallintava efekti."),
                 tr (lang, "1. Find the track that causes it (watch its waveform: it is not centred on the zero line).\n2. Put a high-pass filter at 10-20 Hz or a DC filter on that track.",
                           "1. Etsi raita, joka aiheuttaa sen (sen aaltomuoto ei ole nollaviivan keskellä).\n2. Laita sille raidalle ylipäästösuodin 10–20 Hz:iin tai DC-suodin."));

    add.add ("clipping", { (float) std::log2 (1.0 + s.clipEvents) - 1.5f, 2.0f }, 0.8f, tagPhone | tagCar | tagStreaming | tagHeadphones,
             tr (lang, "Clipped, flat-topped peaks", "Klipatut, latistuneet huiput"),
             juce::String (s.clipEvents) + tr (lang, " clipped runs since reset", " klippausta nollauksen jälkeen"),
             tr (lang, "Samples are stuck at full scale: the peaks are cut flat, which adds harsh distortion. Lossy encoding (Spotify, YouTube, Bluetooth) makes clipped audio sound even worse. A soft clipper on purpose is fine if you can't hear it, but hard clipping at the output is almost always an accident.",
                       "Näytteet jäävät täyteen tasoon: huiput leikkautuvat litteiksi, mikä lisää kireää säröä. Häviöllinen pakkaus (Spotify, YouTube, Bluetooth) saa klipatun äänen kuulostamaan vielä pahemmalta. Tarkoituksellinen soft clipper on ok, jos sitä ei kuule, mutta kova klippaus ulostulossa on lähes aina vahinko."),
             tr (lang, "1. Check every bus and the master for a fader or plugin output pushed above 0 dBFS.\n2. Lower the level before your limiter and let the limiter, not the converter, catch the peaks.\n3. If you print stems or a bounce, export as 24-bit or 32-bit float with headroom.",
                       "1. Tarkista jokainen väylä ja master: onko fader tai pluginin ulostulo yli 0 dBFS.\n2. Laske tasoa ennen limitteriä ja anna limitterin, ei muuntimen, ottaa huiput kiinni.\n3. Kun bouncaat tai teet stemmit, vie 24-bittisenä tai 32-bit float -muodossa ja jätä tilaa."));

    if (! mastering)
    {
        add.add ("headroom", { m.truePeak + 1.0f, 2.0f }, 0.4f, tagStreaming,
                 tr (lang, "No headroom left for mastering", "Masteroinnille ei jää tilaa"),
                 tr (lang, "Peak ", "Huippu ") + num (lang, m.truePeak) + " dBTP" + tr (lang, " (aim -6...-3 while mixing)", " (tavoite −6…−3 miksatessa)"),
                 tr (lang, "A mix that already peaks at 0 dB leaves the mastering stage nothing to work with, and plugins after the fader may clip without you noticing.",
                           "Jos miksaus jo piikkaa 0 dB:ssä, masteroinnille ei jää mitään työstettävää, ja faderin jälkeiset pluginit voivat klipata huomaamatta."),
                 tr (lang, "1. Pull down the master fader, or better, lower all channel faders the same amount (select all, drag).\n2. Aim for peaks around -6...-3 dBFS before mastering.\n3. Don't worry about the mix sounding quieter: loudness comes at the mastering stage.",
                           "1. Laske masterfaderia, tai mieluummin kaikkia kanavafadereita yhtä paljon (valitse kaikki, vedä).\n2. Tavoittele huippuja noin −6…−3 dBFS ennen masterointia.\n3. Älä huolestu, että miksaus kuulostaa hiljaisemmalta: äänekkyys tulee masteroinnissa."));
        add.add ("hotmix", { m.loudness + 11.0f, 3.0f }, 0.4f, tagStreaming,
                 tr (lang, "Mixing into a limiter?", "Miksaatko limitterin läpi?"),
                 (song ? tr (lang, "Integrated ", "Integroitu ") : tr (lang, "Short-term ", "Lyhytaikainen ")) + num (lang, m.loudness) + " LUFS",
                 tr (lang, "The mix is already as loud as a finished master. A limiter on the mix bus while mixing hides balance problems, flattens the drums and makes every EQ decision sound different once it's removed.",
                           "Miksaus on jo yhtä kova kuin valmis master. Limitteri masterväylällä miksatessa piilottaa tasapaino-ongelmia, litistää rummut ja saa jokaisen EQ-päätöksen kuulostamaan erilaiselta, kun se poistetaan."),
                 tr (lang, "1. Bypass the limiter now and then and match the level to compare honestly.\n2. Better: mix without it, peaks at -6 dBFS, and add the limiter only when mastering.\n3. If you mix into a bus compressor on purpose, keep it gentle (1-2 dB).",
                           "1. Ohita limitteri välillä ja tasaa taso, jotta vertailu on rehellinen.\n2. Parempi: miksaa ilman sitä, huiput −6 dBFS:ssä, ja lisää limitteri vasta masteroinnissa.\n3. Jos miksaat tarkoituksella väyläkompressorin läpi, pidä se kevyenä (1–2 dB)."),
                 true);
    }

    // ---- drums vs bass
    if (m.kickValid && p.lowContrastMin > 0.0f)
        add.add ("kickbass", { p.lowContrastMin - m.lowContrast, 3.0f }, 0.8f, tagCar | tagClub,
                 tr (lang, "Kick drowns in the bass", "Kick hukkuu bassoon"),
                 tr (lang, "Kick above the bass: ", "Kick basson yläpuolella: ") + num (lang, m.lowContrast) + " dB (" + tr (lang, "aim ", "tavoite ") + juce::String (juce::CharPointer_UTF8 ("≥ ")) + num (lang, p.lowContrastMin) + ")",
                 tr (lang, "The low end is a steady wall: the bass covers the kick, so the groove loses its punch. A car's cabin boom makes it worse.",
                           "Bassopää on tasainen seinä: basso peittää kickin, joten groove menettää iskunsa. Auton matkustamon jumputus pahentaa tätä."),
                 tr (lang, "1. Turn the bass down 1-3 dB, or cut it 2-3 dB where the kick's fundamental is (often 50-70 Hz) so the kick gets its own spot.\n2. Sidechain ducking: a compressor on the bass with the kick as sidechain input. Ratio 4:1, attack 0-5 ms, release 100-200 ms (to the tempo), 3-6 dB gain reduction. A volume-shaper / ducking plugin does the same. The meter sees the level gap; the clarity that ducking gives is best judged by ear.\n3. Shorter bass notes that leave space for the kick also help.\nTrap: a long 808 or kick tail and a long bass note on the same beat always mask each other, whatever the EQ. Pick one to carry the low note.",
                           "1. Laske bassoa 1–3 dB tai leikkaa siitä 2–3 dB kickin perustaajuudelta (usein 50–70 Hz), jotta kick saa oman paikkansa.\n2. Sidechain-duckaus: kompressori bassoon ja kick sivuketjusisääntuloon. Suhde 4:1, attack 0–5 ms, release 100–200 ms (tempon mukaan), 3–6 dB vaimennus. Volume shaper- / ducking-plugin tekee saman. Mittari näkee tasoeron; duckauksen tuoman selkeyden arvioi parhaiten korvalla.\n3. Myös lyhyemmät bassonuotit, jotka jättävät tilaa kickille, auttavat.\nSudenkuoppa: pitkä 808- tai kick-häntä ja pitkä bassonuotti samalla iskulla peittävät aina toisensa, EQ:sta riippumatta. Valitse kumpi kantaa matalan nuotin."));

    // drum advice only when there are drums: regular kicks, or a clear beat
    const bool drumsPresent = m.kickValid || s.tempoConfidence >= 0.5f;
    if (m.contrastValid && drumsPresent)
    {
        add.add ("flatdrums", { p.highContrastMin - m.highContrast, 3.0f }, 0.7f, tagPhone,
                 tr (lang, "Drums lack snap", "Rummuista puuttuu terävyys"),
                 tr (lang, "Snare/hat contrast ", "Virveli-/hihat-kontrasti ") + num (lang, m.highContrast) + " dB (" + tr (lang, "aim ", "tavoite ") + juce::String (juce::CharPointer_UTF8 ("≥ ")) + num (lang, p.highContrastMin) + ")",
                 tr (lang, "Snare and hat hits barely stand out from the rest. Often the limiter or a bus compressor is flattening the transients, or synths cover the snare.",
                           "Virvelin ja hihatin iskut tuskin erottuvat muusta. Usein limitteri tai väyläkompressori litistää transientit, tai syntikat peittävät virvelin."),
                 tr (lang, "1. Slower attack (10-30 ms) on drum bus and mix bus compressors, and push the limiter less.\n2. Transient shaper on the snare/clap: more attack.\n3. Cut pads and synths 2 dB around 2-5 kHz so the snare gets through.",
                           "1. Hitaampi attack (10–30 ms) rumpu- ja masterväylän kompressoreihin, ja vähemmän limitointia.\n2. Transienttimuokkain virveliin/clappiin: lisää attackia.\n3. Leikkaa padeista ja syntikoista 2 dB noin 2–5 kHz:stä, jotta virveli pääsee läpi."));

        add.add ("spikydrums", { m.highContrast - p.highContrastMax, 4.0f }, 0.5f, tagPhone,
                 tr (lang, "Drum hits poke out too much", "Rumpuiskut pistävät liikaa esiin"),
                 tr (lang, "Snare/hat contrast ", "Virveli-/hihat-kontrasti ") + num (lang, m.highContrast) + " dB (max " + num (lang, p.highContrastMax) + ")",
                 tr (lang, "Very spiky snare and hat hits feel separate from the mix and get harsh on small speakers.",
                           "Hyvin piikikkäät virveli- ja hihat-iskut irtoavat miksauksesta ja muuttuvat kireiksi pienistä kaiuttimista."),
                 tr (lang, "1. Turn the snare and hats down 1-2 dB.\n2. Glue the drum bus: compressor ratio 2-4:1, attack 10-30 ms, 2-3 dB gain reduction.\n3. Parallel compression on the drums also helps.",
                           "1. Laske virveliä ja hihatteja 1–2 dB.\n2. Liimaa rumpuväylä: kompressorin suhde 2–4:1, attack 10–30 ms, 2–3 dB vaimennus.\n3. Myös rinnakkaiskompressio rumpuihin auttaa."),
                 true);
    }

    // ---- narrow resonances: one third-octave band sticking out of its neighbours
    {
        std::array<float, Bands::count> d {};
        for (int b = 0; b < Bands::count; ++b)
            d[(size_t) b] = m.spectrum[(size_t) b] - p.target[(size_t) b];

        auto peakIn = [&] (int first, int last, int& where, float minLevel = 1.5f) -> float
        {
            float best = 0.0f;
            for (int b = first; b <= last; ++b)
            {
                const float bump = d[(size_t) b] - 0.5f * (d[(size_t) b - 1] + d[(size_t) b + 1]);
                if (bump > best && d[(size_t) b] > minLevel)
                {
                    best = bump;
                    where = b;
                }
            }
            return best;
        };

        int lowBand = 0, highBand = 0;
        const float lowBump = peakIn (3, 12, lowBand);
        const float highBump = peakIn (20, 23, highBand);
        const auto f1 = freq (lang, Bands::centre (lowBand)), f2 = freq (lang, Bands::centre (highBand));

        add.add ("boomnote", { lowBump - 7.0f, 3.0f }, 0.6f, tagCar,
                 tr (lang, "Booming note around ", "Jumittava nuotti noin ") + f1,
                 "+" + num (lang, lowBump) + tr (lang, " dB above its neighbours", " dB naapureita korkeammalla"),
                 tr (lang, "One frequency sticks out of the low end, so single bass notes will jump out. A car cabin makes it worse.",
                           "Yksi taajuus nousee bassopäästä esiin, joten yksittäiset bassonuotit hyppäävät esiin. Auton matkustamo pahentaa tätä."),
                 tr (lang, "1. Narrow EQ cut (Q 4-8, -3...-6 dB) at ", "1. Kapea EQ-leikkaus (Q 4–8, −3…−6 dB) taajuudelle ")
                     + f1 + tr (lang, " on the bass or the kick.\n2. Or a dynamic EQ that only cuts when that note plays.",
                                      " bassoon tai kickiin.\n2. Tai dynaaminen EQ, joka leikkaa vain kun se nuotti soi."));

        add.add ("piercing", { highBump - 4.0f, 3.0f }, 0.6f, tagPhone,
                 tr (lang, "Piercing peak around ", "Pistävä piikki noin ") + f2,
                 "+" + num (lang, highBump) + tr (lang, " dB above its neighbours", " dB naapureita korkeammalla"),
                 tr (lang, "A narrow peak in the upper mids or highs sounds shrill, especially on phones and earbuds.",
                           "Kapea piikki yläkeskialueella tai diskantissa kuulostaa kirkuvalta, etenkin puhelimessa ja nappikuulokkeissa."),
                 tr (lang, "1. Sweep a narrow boost around ", "1. Pyyhkäise kapeaa korostusta noin ")
                     + f2 + tr (lang, " to find the culprit.\n2. Cut it 2-4 dB with a narrow EQ (Q 4-8).",
                                      " ympärillä ja etsi syyllinen.\n2. Leikkaa siitä 2–4 dB kapealla EQ:lla (Q 4–8)."));

        // sibilance: a bump in the 5-10 kHz region
        // (above both neighbours, so the edge of a broad presence lift doesn't count)
        int sibBand = 24;
        float sibBump = 0.0f;
        for (int b = 24; b <= 27; ++b)
        {
            const float bump = std::min (d[(size_t) b] - d[(size_t) b - 1], d[(size_t) b] - d[(size_t) b + 1]);
            if (bump > sibBump && d[(size_t) b] > 1.0f)
            {
                sibBump = bump;
                sibBand = b;
            }
        }
        const auto f3 = freq (lang, Bands::centre (sibBand));
        add.add ("sibilance", { sibBump - 3.0f, 3.0f }, 0.6f, tagPhone | tagHeadphones | tagStreaming,
                 tr (lang, "Sharp 's' sounds and hiss around ", "Terävät s-äänteet ja sihinä noin ") + f3,
                 "+" + num (lang, sibBump) + tr (lang, " dB above its neighbours", " dB naapureita korkeammalla"),
                 tr (lang, "Sibilance, bright hats or noise stick out at 5-10 kHz. Earbuds, Bluetooth and lossy encoding turn it into a lisp-like splash, and it gets tiring fast.",
                           "Sibilanssi, kirkkaat hihatit tai kohina nousevat esiin 5–10 kHz:ssä. Nappikuulokkeet, Bluetooth ja häviöllinen pakkaus muuttavat sen roiskeeksi, ja kuuntelu väsyttää nopeasti."),
                 tr (lang, "1. De-esser on the vocal (or the hats) at ", "1. De-esser vokaaliin (tai hihatteihin) taajuudelle ")
                     + f3 + tr (lang, ", 3-6 dB reduction on the 's' sounds only.\n2. Watch out for bright reverbs and delays: they spread the 's' around. De-ess or low-pass the reverb send.\n3. Don't fix it with a wide high cut on the master: that dulls everything.",
                                      ", 3–6 dB vaimennus vain s-äänteisiin.\n2. Varo kirkkaita kaikuja ja viiveitä: ne levittävät s-äänteitä. De-essaa tai alipäästösuodata kaikulähetys.\n3. Älä korjaa sitä laajalla diskanttileikkauksella masterissa: se tylsistää kaiken."));

        // a deep, narrow dip: often an over-eager EQ cut, or two layers cancelling
        int dipBand = 0;
        float dip = 0.0f;
        for (int b = 4; b <= 25; ++b)
        {
            const float depth = 0.5f * (d[(size_t) b - 1] + d[(size_t) b + 1]) - d[(size_t) b];
            if (depth > dip && d[(size_t) b] < -1.5f)
            {
                dip = depth;
                dipBand = b;
            }
        }
        const auto f4 = freq (lang, Bands::centre (dipBand));
        add.add ("notch", { dip - 6.0f, 3.0f }, 0.3f, 0,
                 tr (lang, "Hole in the spectrum around ", "Kuoppa spektrissä noin ") + f4,
                 num (lang, -dip, 1) + tr (lang, " dB below its neighbours", " dB naapureita matalammalla"),
                 tr (lang, "One narrow range is missing. Usually it's an EQ cut that went too deep, or two layers (kick and bass, two synths) cancelling each other because their phase doesn't line up.",
                           "Yksi kapea alue puuttuu. Yleensä syy on liian syvä EQ-leikkaus tai kaksi kerrosta (kick ja basso, kaksi syntikkaa), jotka kumoavat toisiaan, koska niiden vaihe ei osu kohdalleen."),
                 tr (lang, "1. Check your EQs around ", "1. Tarkista EQ:t noin ") + f4
                     + tr (lang, ": cuts deeper than 6 dB are rarely needed.\n2. If two layers overlap there, flip the polarity of one or nudge it a few milliseconds and listen for the fuller version.",
                                 ":n kohdalla: yli 6 dB:n leikkauksia tarvitaan harvoin.\n2. Jos kaksi kerrosta menee siinä päällekkäin, käännä toisen polariteetti tai siirrä sitä pari millisekuntia ja kuuntele, kumpi on täyteläisempi."),
                 true);

        // rumble: energy below ~30 Hz that nobody hears but every limiter reacts to
        const float rumble = 10.0f * std::log10 (power (d[0]) * 0.5f + power (d[1]) * 0.5f);
        add.add ("rumble", { rumble - 6.0f, 4.0f }, 0.5f, tagClub | tagCar,
                 tr (lang, "Rumble below 30 Hz", "Jyrinää alle 30 Hz:ssä"),
                 num (lang, rumble, 1, true) + tr (lang, " dB vs target at 20-30 Hz", " dB tavoitteesta 20–30 Hz:ssä"),
                 tr (lang, "Almost no speaker plays this, but it still eats headroom and makes the limiter and compressors pump. Typical sources: a long 808 tail, a bass with no high-pass, wind or handling noise, or a sub-bass synth going too low.",
                           "Juuri mikään kaiutin ei toista tätä, mutta se syö silti tilaa ja saa limitterin ja kompressorit pumppaamaan. Tavallisia lähteitä: pitkä 808-häntä, basso ilman ylipäästöä, tuuli- tai käsittelyäänet tai liian matalalle menevä subisyntikka."),
                 tr (lang, "1. High-pass the bass and the kick at 25-30 Hz (steep, 24 dB/oct). Listen: you should hear no change, only see it.\n2. Check 808 and sub notes in the lowest octave; transpose very low notes up an octave.\n3. On the master, a gentle 20 Hz high-pass is a safety net, not a fix.",
                           "1. Ylipäästösuodata basso ja kick 25–30 Hz:iin (jyrkkä, 24 dB/okt). Kuuntele: muutosta ei pitäisi kuulla, vain nähdä.\n2. Tarkista alimman oktaavin 808- ja subinuotit; transponoi hyvin matalat nuotit oktaavia ylemmäs.\n3. Masterissa loiva 20 Hz:n ylipäästö on turvaverkko, ei korjaus."));
    }

    // ---- wideners on the top end: highs much wider than the centre
    {
        const float hiSide = sumBands (side, 23, 29) - sumBands (mid, 23, 29);
        if (m.correlation >= 0.3f)
            add.add ("sidehigh", { hiSide + 1.0f, 3.0f }, 0.4f, tagPhone,
                     tr (lang, "Top end mostly on the sides", "Diskantti lähes pelkästään sivuilla"),
                     tr (lang, "Side vs mid above 4 kHz: ", "Sivu vs. keski yli 4 kHz: ") + num (lang, hiSide) + " dB",
                     tr (lang, "The highs are wider than they are centred, usually from a stereo widener or a wide reverb on the bright parts. It sounds huge on headphones, but in mono the shine and the air disappear, and it can sound phasey.",
                               "Ylä-äänet ovat leveämmät kuin keskellä, yleensä stereoleventäjän tai leveän kaiun takia kirkkaissa osissa. Kuulokkeilla se kuulostaa valtavalta, mutta monona kirkkaus ja ilma katoavat, ja ääni voi kuulostaa faasiselta."),
                     tr (lang, "1. Lower the width of the widener on hats, pads and FX by 20-30 %, or remove it from the master.\n2. Keep a centred element with top end (hats, shaker, the vocal's air).\n3. Check in mono: the mix should stay bright.",
                               "1. Vähennä leventäjän leveyttä hihateista, padeista ja efekteistä 20–30 % tai poista se masterista.\n2. Pidä keskellä jokin elementti, jossa on diskanttia (hihat, shaker, vokaalin ilma).\n3. Tarkista monona: miksauksen pitää pysyä kirkkaana."),
                     true);
    }

    std::stable_sort (a.findings.begin(), a.findings.end(), [] (const Finding& x, const Finding& y)
    {
        if (x.severity != y.severity)
            return (int) x.severity > (int) y.severity;
        return x.excess > y.excess;
    });

    // one clear problem costs about 20 points, but the score never quite reaches zero,
    // so every fix still moves the needle
    a.score = juce::jlimit (0.0f, 100.0f, 100.0f * std::exp (-penalty / 60.0f));

    // ---- checks that passed, so the good news is visible too
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
        bool tonal = false;
        for (const auto& f : a.findings)
            tonal = tonal || f.id.rfind ("range", 0) == 0;

        if (! tonal)
            pass ("ok-tonal", tr (lang, "Tonal balance fits the style", "Taajuustasapaino sopii tyyliin"),
                  tr (lang, "every range within its corridor", "kaikki alueet käytävän sisällä"));
        if (m.lowWidth < p.lowWidthMax && m.correlation >= 0.3f)
            pass ("ok-mono", tr (lang, "Low end is mono below 140 Hz", "Matalat ovat monona alle 140 Hz"),
                  tr (lang, "side ", "sivu ") + num (lang, m.lowWidth) + " dB · " + tr (lang, "correlation ", "korrelaatio ") + num (lang, m.correlation, 2, true));
        if (mastering && ! present ({ "peak" }))
            pass ("ok-peak", tr (lang, "True peak below the ceiling", "True peak katon alla"),
                  num (lang, m.truePeak) + " dBTP · " + tr (lang, "no distortion after MP3/AAC", "ei säröä MP3/AAC-muunnoksessa"));
        if (mastering && ! present ({ "loud", "quiet" }))
            pass ("ok-loud", tr (lang, "Loudness fits the style", "Äänekkyys sopii tyyliin"), num (lang, m.loudness) + " LUFS");
        if (! present ({ "squashed", "dynamic" }))
            pass ("ok-dyn", tr (lang, "Dynamics fit the style", "Dynamiikka sopii tyyliin"), "PLR " + num (lang, m.plr) + " LU");
        if (! present ({ "phonebass" }))
            pass ("ok-phone", tr (lang, "Bass carries on small speakers", "Basso kantaa pienistä kaiuttimista"),
                  tr (lang, "enough harmonics at 90-280 Hz", "tarpeeksi harmonisia 90–280 Hz:ssä"));
        if (m.kickValid && p.lowContrastMin > 0.0f && ! present ({ "kickbass" }))
            pass ("ok-kick", tr (lang, "Kick stands out from the bass", "Kick erottuu bassosta"),
                  num (lang, m.lowContrast) + tr (lang, " dB above the bass", " dB basson yläpuolella"));
    }

    // ---- how well it should hold up on each kind of playback
    {
        const int tags[] = { tagPhone, tagCar, tagHeadphones, tagClub };
        const char* goodEn[] = { "Mids and bass harmonics carry on small speakers.", "Low end under control, nothing booms.",
                                 "Stereo image works, no phase problems.", "Sub, kick and mono low end on target." };
        const char* goodFi[] = { "Keskiäänet ja basson harmoniset kantavat pienistä kaiuttimista.", "Bassopää hallussa, mikään ei jumputa.",
                                 "Stereokuva toimii, ei vaiheongelmia.", "Sub, kick ja mono bassopää kohdallaan." };
        for (int i = 0; i < (int) Playback::count; ++i)
        {
            float pen = 0.0f;
            juce::StringArray worst;
            for (const auto& f : a.findings)
                if ((f.tags & tags[i]) != 0)
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

    // ---- what the next step is worth
    {
        float top = 0.0f;
        int count = 0;
        for (const auto& f : a.findings)
            if (f.severity == Severity::fix || f.severity == Severity::watch)
                if (count < 2) { top += f.penalty; ++count; }
        const float projected = 100.0f * std::exp (-(penalty - top) / 60.0f);
        if (count == 0 || a.score >= 90.0f)
            a.nextStep = tr (lang, "Nothing big left on the meter. Compare with a reference track by ear.",
                                   "Mittarissa ei ole isoja asioita jäljellä. Vertaa referenssiin korvalla.");
        else if (verdictFor (projected, lang) == verdictFor (a.score, lang))
            a.nextStep = count == 1 ? tr (lang, "Fix the top finding to move the needle.", "Korjaa ylin havainto, niin mittari liikkuu.")
                                    : tr (lang, "Fix the two top findings to move the needle.", "Korjaa kaksi ylintä havaintoa, niin mittari liikkuu.");
        else
            a.nextStep = (count == 1 ? tr (lang, "Fix the top finding and you will probably reach ", "Korjaa ylin havainto, niin pääset todennäköisesti tasolle ")
                                     : tr (lang, "Fix the two top findings and you will probably reach ", "Korjaa kaksi ylintä havaintoa, niin pääset todennäköisesti tasolle "))
                         + verdictFor (projected, lang) + ".";
    }

    if (a.findings.empty())
    {
        Finding f;
        f.id = "good";
        f.severity = Severity::good;
        f.title = tr (lang, "Every check is in range", "Kaikki tarkistukset ovat kohdallaan");
        f.why = tr (lang, "Now trust your ears: compare with a reference track at the same loudness, and listen on a phone and in a car (DDR).",
                          "Luota nyt korviisi: vertaa referenssikappaleeseen samalla äänenvoimakkuudella ja kuuntele puhelimesta ja autossa (DDR).");
        a.findings.push_back (f);
    }

    // ---- one listening tip at a time, changing every 30 seconds: the ear decides
    if (m.valid)
    {
        struct Tip { const char *titleEn, *titleFi, *whyEn, *whyFi; };
        static const Tip tips[] = {
            { "Ear check: listen quietly", "Korvatesti: kuuntele hiljaa",
              "Turn the volume down until you can just talk over it. What still comes through is what matters: kick, bass, vocal or lead. Loud listening makes everything sound exciting and hides balance problems.",
              "Laske äänenvoimakkuus niin hiljaiselle, että pystyt juuri puhumaan sen päälle. Se, mikä vielä kuuluu, on tärkeintä: kick, basso, vokaali tai liidi. Kova kuuntelu saa kaiken kuulostamaan jännittävältä ja piilottaa tasapaino-ongelmat." },
            { "Ear check: mono", "Korvatesti: mono",
              "Switch your monitoring to mono for a minute. If the lead, the vocal or the snare drop back, something is out of phase or too wide. Phones, clubs and many Bluetooth speakers play in mono.",
              "Kytke kuuntelu monoksi minuutiksi. Jos liidi, vokaali tai virveli painuu taakse, jokin on vastavaiheessa tai liian leveä. Puhelimet, klubit ja monet Bluetooth-kaiuttimet soittavat monona." },
            { "Ear check: your phone", "Korvatesti: puhelin",
              "Bounce a quick version and play it on your phone speaker. Can you still hear the bass line and the kick? If not, they need more harmonics (100-300 Hz), not more sub.",
              "Bouncaa nopea versio ja soita se puhelimen kaiuttimesta. Kuuluuko bassolinja ja kick vielä? Jos ei, ne tarvitsevat lisää harmonisia (100–300 Hz), ei lisää subia." },
            { "Ear check: take a break", "Korvatesti: pidä tauko",
              "After 45-60 minutes your ears adapt: highs seem duller, so you keep adding more. Take 10 minutes of silence, then listen again before making big EQ moves.",
              "45–60 minuutin jälkeen korvat tottuvat: diskantti tuntuu tylsemmältä, joten sitä tulee lisättyä. Pidä 10 minuuttia hiljaisuutta ja kuuntele sitten uudestaan ennen isoja EQ-muutoksia." },
            { "Trap: louder sounds better", "Sudenkuoppa: kovempi kuulostaa paremmalta",
              "A version only 1 dB louder sounds 'better' to everyone. When you compare a change, a plugin or a reference, match the loudness first. The A/B button here does that for the reference.",
              "Vain 1 dB kovempi versio kuulostaa kaikista “paremmalta”. Kun vertaat muutosta, pluginia tai referenssiä, tasaa äänekkyys ensin. Tämän pluginin A/B-nappi tekee sen referenssille." },
            { "Trap: EQ in solo", "Sudenkuoppa: EQ soolona",
              "A sound that is perfect in solo is often too big in the mix. Make EQ decisions with everything playing; solo only to find a problem.",
              "Soolona täydellinen ääni on miksauksessa usein liian iso. Tee EQ-päätökset kaiken soidessa; soolaa vain ongelman etsimiseen." },
            { "Trap: your room's bass", "Sudenkuoppa: huoneesi basso",
              "Most rooms boom at some bass notes and swallow others. If the low end sounds different in the middle of the room than at your desk, trust the meter and good headphones for the bass, and check in the car.",
              "Useimmat huoneet jumputtavat joillain bassonuoteilla ja nielevät toisia. Jos bassopää kuulostaa huoneen keskellä erilaiselta kuin pöydän ääressä, luota bassossa mittariin ja hyviin kuulokkeisiin ja tarkista autossa." },
            { "Trap: boost instead of cut", "Sudenkuoppa: korostus leikkauksen sijaan",
              "If something is missing, first ask what is covering it. Cutting the masking sound often works better than boosting the one you want, and it keeps headroom.",
              "Jos jotain puuttuu, kysy ensin, mikä sitä peittää. Peittävän äänen leikkaaminen toimii usein paremmin kuin halutun korostaminen, ja tilaa jää enemmän." },
            { "Trap: everything wide", "Sudenkuoppa: kaikki leveänä",
              "Width only works by contrast. If every sound is wide, nothing feels wide and the centre gets weak. Keep kick, bass, snare and lead centred and let the supporting parts go wide.",
              "Leveys toimii vain kontrastina. Jos kaikki äänet ovat leveitä, mikään ei tunnu leveältä ja keskusta heikkenee. Pidä kick, basso, virveli ja liidi keskellä ja anna tukiosien levitä." },
            { "Trap: fixing it on the master", "Sudenkuoppa: korjaus masterissa",
              "A master EQ or multiband fix affects every sound at once. If one element is the problem (a harsh synth, a boomy bass note), fix it on that track. The master is for small final touches.",
              "Master-EQ tai monikaistakorjaus vaikuttaa kaikkiin ääniin kerralla. Jos ongelma on yksi elementti (kireä syntikka, jumittava bassonuotti), korjaa se sillä raidalla. Master on pieniä viimeistelyjä varten." },
            { "Trap: reverb wash", "Sudenkuoppa: kaikumössö",
              "Long, full-range reverbs fill the gaps between hits and cloud the low mids. High-pass reverb returns at 200-300 Hz, low-pass them at 6-10 kHz, and duck them with the vocal or lead.",
              "Pitkät, täyskaistaiset kaiut täyttävät iskujen välit ja sumentavat alakeskiäänet. Ylipäästösuodata kaikupaluut 200–300 Hz:iin, alipäästösuodata 6–10 kHz:iin ja duckaa ne vokaalilla tai liidillä." },
            { "Trap: the loudness war", "Sudenkuoppa: äänekkyyssota",
              "Spotify, Apple Music and YouTube turn loud masters down to about -14 LUFS. A master squashed to -6 LUFS ends up the same loudness as a -10 LUFS one, but with less punch. Loud enough for the style is enough.",
              "Spotify, Apple Music ja YouTube laskevat kovat masterit noin −14 LUFS:iin. −6 LUFS:iin puristettu master päätyy samaan äänekkyyteen kuin −10 LUFS:n master, mutta vähemmällä iskulla. Tyylille riittävä äänekkyys riittää." },
        };
        const auto& tip = tips[(size_t) ((int) (s.secondsAnalysed / 30.0f) % (int) std::size (tips))];
        Finding f;
        f.id = "tip";
        f.severity = Severity::info;
        f.title = tr (lang, tip.titleEn, tip.titleFi);
        f.why = tr (lang, tip.whyEn, tip.whyFi);
        a.findings.push_back (f);
    }
    return a;
}
} // namespace wwma
