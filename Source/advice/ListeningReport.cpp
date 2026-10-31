#include "ListeningReport.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
namespace
{
    /** Width is measured above 140 Hz: a mono low end (as it should be) would otherwise hide the stereo picture. */
    constexpr int widthFirstBand = 9;

    double toPower (float db) { return std::pow (10.0, db / 10.0); }
    float toDb (double p) { return p > 1.0e-30 ? (float) (10.0 * std::log10 (p)) : -300.0f; }

    juce::String num (float v, int decimals, Lang lang, bool sign = false)
    {
        if (! std::isfinite (v))
            return "-";
        auto s = juce::String (std::abs (v), decimals);
        if (decimals == 0)
            s = juce::String (juce::roundToInt (std::abs (v)));
        if (lang == Lang::finnish)
            s = s.replaceCharacter ('.', ',');
        const bool nonZero = s.containsAnyOf ("123456789");
        if (v < 0.0f && nonZero)
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + s;
        return (sign && v > 0.0f && nonZero ? "+" : "") + s;
    }

    /** mid + side power per band of one point (= left + right). */
    std::array<double, Bands::count> totalPower (const TimelinePoint& p)
    {
        std::array<double, Bands::count> out {};
        for (size_t b = 0; b < (size_t) Bands::count; ++b)
            out[b] = toPower (p.mid[b]) + toPower (p.side[b]);
        return out;
    }

    float rangeOf (const std::array<double, Bands::count>& power, int first, int last)
    {
        double p = 0.0;
        for (int b = first; b <= last; ++b)
            p += power[(size_t) b];
        return toDb (p);
    }

    struct Features { float loudness, low, high, width; };

    Features featuresOf (const TimelinePoint& p)
    {
        const auto power = totalPower (p);
        const float total = rangeOf (power, 0, Bands::count - 1);
        double mid = 0.0, side = 0.0;
        for (size_t b = (size_t) widthFirstBand; b < (size_t) Bands::count; ++b)
        {
            mid += toPower (p.mid[b]);
            side += toPower (p.side[b]);
        }
        return { p.loudness,
                 rangeOf (power, 0, 8) - total,                // sub + bass
                 rangeOf (power, 21, Bands::count - 1) - total, // presence + air
                 juce::jlimit (-40.0f, 0.0f, toDb (side) - toDb (mid)) };
    }

    juce::String nl() { return "\n"; }

    juce::String sectionLabel (const SongSection& s, Lang lang)
    {
        return juce::String::charToString ((juce::juce_wchar) s.letter) + " (" + sectionKindName (s.kind, lang).toLowerCase() + ", " + s.at + ")";
    }

    const SongSection* loudest (const std::vector<SongSection>& sections, SectionKind kind)
    {
        const SongSection* best = nullptr;
        for (const auto& s : sections)
            if (s.kind == kind && (best == nullptr || s.levels.loudness > best->levels.loudness))
                best = &s;
        return best;
    }

    /** Range names for the tables, short. */
    juce::String rangeShort (int r, Lang lang) { return rangeNickname ((Range) r, lang); }
} // namespace

juce::String sectionKindName (SectionKind k, Lang lang)
{
    switch (k)
    {
        case SectionKind::intro:     return tr (lang, "Intro", "Intro");
        case SectionKind::build:     return tr (lang, "Build-up", "Nousu");
        case SectionKind::drop:      return tr (lang, "Drop / full part", "Drop / täysi osa");
        case SectionKind::breakdown: return tr (lang, "Breakdown", "Breakdown");
        case SectionKind::groove:    return tr (lang, "Steady part", "Tasainen osa");
        case SectionKind::outro:     return tr (lang, "Outro", "Outro");
    }
    return {};
}

const char* sectionKindId (SectionKind k)
{
    switch (k)
    {
        case SectionKind::intro:     return "intro";
        case SectionKind::build:     return "build";
        case SectionKind::drop:      return "drop";
        case SectionKind::breakdown: return "breakdown";
        case SectionKind::groove:    return "steady";
        case SectionKind::outro:     return "outro";
    }
    return "";
}

juce::String clockText (float seconds)
{
    const int s = juce::jmax (0, juce::roundToInt (seconds));
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

juce::String songTimeText (const Timeline& t, int i)
{
    if (! juce::isPositiveAndBelow (i, (int) t.points.size()) || t.points[(size_t) i].songSeconds < 0.0f)
        return {};
    return clockText (t.points[(size_t) i].songSeconds);
}

SectionLevels levelsOf (const Timeline& t, int first, int last)
{
    SectionLevels out;
    first = juce::jmax (0, first);
    last = juce::jmin ((int) t.points.size() - 1, last);
    if (last < first)
        return out;

    std::array<double, Bands::count> power {};
    double loud = 0.0, mid = 0.0, side = 0.0, corr = 0.0;
    for (int i = first; i <= last; ++i)
    {
        const auto& p = t.points[(size_t) i];
        const auto tp = totalPower (p);
        for (size_t b = 0; b < (size_t) Bands::count; ++b)
        {
            power[b] += tp[b];
            if (b >= (size_t) widthFirstBand)
            {
                mid += toPower (p.mid[b]);
                side += toPower (p.side[b]);
            }
        }
        loud += toPower (p.loudness);
        corr += p.correlation;
        out.peak = juce::jmax (out.peak, p.peak);
    }
    const double n = last - first + 1;
    out.loudness = toDb (loud / n);
    out.correlation = (float) (corr / n);
    out.width = juce::jlimit (-60.0f, 0.0f, toDb (side) - toDb (mid));
    const float total = rangeOf (power, 0, Bands::count - 1);
    for (int r = 0; r < numRanges; ++r)
    {
        const auto info = rangeBands ((Range) r);
        out.shape[(size_t) r] = rangeOf (power, info.firstBand, info.lastBand) - total;
    }
    return out;
}

std::vector<SongSection> findSections (const Timeline& t)
{
    std::vector<SongSection> out;
    const int n = (int) t.points.size();
    if (n == 0)
        return out;

    const float step = juce::jmax (0.05f, t.step);
    const int window = juce::jmax (2, juce::roundToInt (8.0f / step));    // compare 8 s before with 8 s after
    const int minLen = juce::jmax (2, juce::roundToInt (8.0f / step));    // no part shorter than 8 s

    std::vector<Features> f;
    f.reserve ((size_t) n);
    for (const auto& p : t.points)
        f.push_back (featuresOf (p));

    // prefix sums for quick window means
    std::vector<std::array<double, 4>> pre ((size_t) n + 1);
    for (int i = 0; i < n; ++i)
    {
        const auto& x = f[(size_t) i];
        pre[(size_t) i + 1] = { pre[(size_t) i][0] + x.loudness, pre[(size_t) i][1] + x.low,
                                pre[(size_t) i][2] + x.high, pre[(size_t) i][3] + x.width };
    }
    auto mean = [&] (int from, int to, int k) { return (pre[(size_t) to][(size_t) k] - pre[(size_t) from][(size_t) k]) / (double) (to - from); };

    std::vector<std::pair<float, int>> candidates;
    for (int c = minLen; c <= n - minLen; ++c)
    {
        const int w = juce::jmin (window, c, n - c);
        if (w < 2)
            continue;
        const double dL = mean (c, c + w, 0) - mean (c - w, c, 0);
        const double dLow = mean (c, c + w, 1) - mean (c - w, c, 1);
        const double dHigh = mean (c, c + w, 2) - mean (c - w, c, 2);
        const double dW = mean (c, c + w, 3) - mean (c - w, c, 3);
        const float novelty = (float) (std::abs (dL) + 0.5 * std::abs (dLow) + 0.5 * std::abs (dHigh) + 0.25 * std::abs (dW));
        candidates.push_back ({ novelty, c });
    }
    // strongest changes first; keep local peaks that are far enough from the ones already taken
    std::sort (candidates.begin(), candidates.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<int> cuts;
    for (const auto& [novelty, c] : candidates)
    {
        if (novelty < 3.0f || cuts.size() >= 15)
            break;
        bool farEnough = true;
        for (int other : cuts)
            farEnough = farEnough && std::abs (other - c) >= minLen;
        if (farEnough)
            cuts.push_back (c);
    }
    std::sort (cuts.begin(), cuts.end());

    int from = 0;
    cuts.push_back (n);
    for (int c : cuts)
    {
        SongSection s;
        s.first = from;
        s.last = c - 1;
        s.letter = (char) ('A' + juce::jmin (25, (int) out.size()));
        s.levels = levelsOf (t, s.first, s.last);
        const auto at = songTimeText (t, s.first);
        s.at = at.isNotEmpty() ? at : clockText (heardTime (t, s.first));

        // loudness slope: least squares over the part
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        const int count = s.last - s.first + 1;
        for (int i = s.first; i <= s.last; ++i)
        {
            const double x = (i - s.first) * step, y = f[(size_t) i].loudness;
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double den = count * sxx - sx * sx;
        s.slope = den > 1.0e-9 ? (float) ((count * sxy - sx * sy) / den) : 0.0f;
        out.push_back (s);
        from = c;
    }

    // the song's average shape, to say which part has more or less of each range
    const auto song = levelsOf (t, 0, n - 1);
    for (auto& s : out)
        for (size_t r = 0; r < (size_t) numRanges; ++r)
            s.tone[r] = s.levels.shape[r] - song.shape[r];

    if (out.size() == 1)
        return out;

    // what each part probably is
    const auto top = std::max_element (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.levels.loudness < b.levels.loudness; });
    const float maxLoud = top->levels.loudness;
    auto lowOf = [] (const SongSection& s) { return toDb (toPower (s.levels.shape[0]) + toPower (s.levels.shape[1])); };
    const float topLow = lowOf (*top);
    std::vector<bool> loud (out.size());
    for (size_t i = 0; i < out.size(); ++i)
        loud[i] = out[i].levels.loudness >= maxLoud - 2.5f && lowOf (out[i]) >= topLow - 6.0f;
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& s = out[i];
        const bool lowGone = lowOf (s) < topLow - 6.0f;
        const bool nextLoud = i + 1 < out.size() && loud[i + 1];
        if (nextLoud && s.slope > 0.08f && &s != &*top)
            s.kind = SectionKind::build; // rises into a loud part, even if it gets nearly as loud itself
        else if (loud[i])
            s.kind = SectionKind::drop;
        else if (i == 0)
            s.kind = SectionKind::intro;
        else if (i + 1 == out.size())
            s.kind = SectionKind::outro;
        else if (lowGone || s.levels.loudness < maxLoud - 4.0f)
            s.kind = SectionKind::breakdown;
        else
            s.kind = SectionKind::groove;
    }
    return out;
}

std::vector<juce::String> sectionNotes (const std::vector<SongSection>& sections, const std::vector<SongSection>& refSections,
                                        Stage stage, Lang lang)
{
    std::vector<juce::String> notes;
    if (sections.size() < 2)
        return notes;

    auto lowOf = [] (const SongSection& s) { return toDb (toPower (s.levels.shape[0]) + toPower (s.levels.shape[1])); };
    const auto* drop = loudest (sections, SectionKind::drop);

    // 1. does the drop hit harder than what comes before it?
    if (drop != nullptr)
    {
        const SongSection* rival = nullptr;
        for (const auto& s : sections)
            if (s.kind == SectionKind::build || s.kind == SectionKind::breakdown || s.kind == SectionKind::intro)
                if (rival == nullptr || s.levels.loudness > rival->levels.loudness)
                    rival = &s;
        if (rival != nullptr)
        {
            const float dL = drop->levels.loudness - rival->levels.loudness;
            const float dLow = lowOf (*drop) - lowOf (*rival);
            if (dL < 1.5f && dLow < 3.0f)
                notes.push_back (tr (lang, "The drop ", "Drop ") + sectionLabel (*drop, lang)
                                 + tr (lang, " is only ", " on vain ") + num (dL, 1, lang) + " LU"
                                 + tr (lang, " louder than ", " kovempi kuin ") + sectionLabel (*rival, lang)
                                 + tr (lang, ", and its low end only ", ", ja sen bassopää vain ") + num (dLow, 1, lang, true) + " dB"
                                 + tr (lang, ". It may not feel like a release. Usually it's easier to make the part before it smaller (filter the low end, drop the kick, narrow it) than to push the drop louder.",
                                             ". Se ei välttämättä tunnu purkautumiselta. Yleensä on helpompaa pienentää sitä edeltävää osaa (suodata bassopää, jätä kick pois, kavenna) kuin nostaa droppia kovemmaksi."));
        }
    }

    // 2. a drop or steady part that has clearly more of one range than the rest of the song
    for (const auto& s : sections)
    {
        if (s.kind != SectionKind::drop && s.kind != SectionKind::groove)
            continue;
        for (int r : { (int) Range::lowMid, (int) Range::presence, (int) Range::air, (int) Range::sub })
        {
            const float d = s.tone[(size_t) r];
            if (std::abs (d) < 3.0f)
                continue;
            notes.push_back (tr (lang, "Part ", "Osassa ") + sectionLabel (s, lang) + tr (lang, " has ", " on ")
                             + num (std::abs (d), 1, lang) + " dB " + (d > 0.0f ? tr (lang, "more ", "enemmän ") : tr (lang, "less ", "vähemmän "))
                             + rangeName ((Range) r, lang) + tr (lang, " than the song on average.", " kuin kappaleessa keskimäärin.")
                             + (r == (int) Range::presence && d > 0.0f ? tr (lang, " Check it on a phone: it can get harsh there.", " Tarkista puhelimella: siellä se voi särähtää.") : juce::String())
                             + (r == (int) Range::lowMid && d > 0.0f ? tr (lang, " Check it in a car: it can get muddy there.", " Tarkista autossa: siellä se voi puuroutua.") : juce::String()));
        }
    }

    // 3. the drop much narrower than the quiet parts: it closes in when it should open up
    if (drop != nullptr)
        for (const auto& s : sections)
            if (s.kind == SectionKind::breakdown && s.levels.width - drop->levels.width > 6.0f)
            {
                notes.push_back (tr (lang, "The drop ", "Drop ") + sectionLabel (*drop, lang) + tr (lang, " is much narrower than the breakdown ", " on paljon kapeampi kuin breakdown ")
                                 + sectionLabel (s, lang) + " (" + num (drop->levels.width, 0, lang) + " vs " + num (s.levels.width, 0, lang)
                                 + tr (lang, " dB side vs mid). It can feel smaller when it should open up.", " dB side vs mid). Se voi tuntua pienemmältä, vaikka sen pitäisi aueta."));
                break;
            }

    // 4. phase in one part
    for (const auto& s : sections)
        if (s.levels.correlation < 0.2f)
            notes.push_back (tr (lang, "Part ", "Osan ") + sectionLabel (s, lang) + tr (lang, " has a low correlation (", " korrelaatio on matala (")
                             + num (s.levels.correlation, 2, lang) + tr (lang, "): something in it may vanish in mono, like on a phone speaker or a club system.",
                                                                          "): jokin siinä voi kadota monona, esim. puhelimen kaiuttimesta tai klubin äänentoistosta."));

    // 5. drops that differ from each other
    for (const auto& s : sections)
        if (drop != nullptr && &s != drop && s.kind == SectionKind::drop && drop->levels.loudness - s.levels.loudness > 2.0f)
            notes.push_back (tr (lang, "Drop ", "Drop ") + sectionLabel (s, lang) + tr (lang, " is ", " on ") + num (drop->levels.loudness - s.levels.loudness, 1, lang)
                             + tr (lang, " LU quieter than ", " LU hiljaisempi kuin ") + sectionLabel (*drop, lang)
                             + tr (lang, ". Fine if that's the plan.", ". Hyvä, jos se on tarkoitus."));

    // 6. peaks close to full scale in one part while mastering
    if (stage == Stage::mastering)
        for (const auto& s : sections)
            if (s.levels.peak > -0.3f)
            {
                notes.push_back (tr (lang, "Peaks in ", "Huiput osassa ") + sectionLabel (s, lang) + tr (lang, " reach ", " ulottuvat ") + num (s.levels.peak, 1, lang)
                                 + tr (lang, " dBTP: they can distort after conversion to MP3/AAC.", " dBTP:hen: ne voivat säröytyä MP3/AAC-muunnoksessa."));
                break;
            }

    // 7. against the reference's drop
    const auto* refDrop = loudest (refSections, SectionKind::drop);
    if (drop != nullptr && refDrop != nullptr)
    {
        for (int r = 0; r < numRanges; ++r)
        {
            const float d = drop->levels.shape[(size_t) r] - refDrop->levels.shape[(size_t) r];
            if (std::abs (d) >= 2.5f)
                notes.push_back (tr (lang, "In the drop, ", "Dropissa ") + rangeName ((Range) r, lang) + tr (lang, " is ", " on ") + num (std::abs (d), 1, lang) + " dB "
                                 + (d > 0.0f ? tr (lang, "stronger", "voimakkaampi") : tr (lang, "weaker", "heikompi"))
                                 + tr (lang, " than in the reference's drop (compared with each drop's own total).", " kuin referenssin dropissa (suhteessa kunkin dropin kokonaistasoon)."));
        }
        const SongSection* quiet = nullptr;
        const SongSection* refQuiet = nullptr;
        for (const auto& s : sections)
            if (s.kind == SectionKind::breakdown && (quiet == nullptr || s.levels.loudness < quiet->levels.loudness))
                quiet = &s;
        for (const auto& s : refSections)
            if (s.kind == SectionKind::breakdown && (refQuiet == nullptr || s.levels.loudness < refQuiet->levels.loudness))
                refQuiet = &s;
        if (quiet != nullptr && refQuiet != nullptr)
        {
            const float mine = drop->levels.loudness - quiet->levels.loudness;
            const float theirs = refDrop->levels.loudness - refQuiet->levels.loudness;
            if (std::abs (mine - theirs) >= 2.0f)
                notes.push_back (tr (lang, "In the reference the drop is ", "Referenssissä drop on ") + num (theirs, 1, lang)
                                 + tr (lang, " LU louder than its breakdown; in yours ", " LU breakdownia kovempi; sinun kappaleessasi ") + num (mine, 1, lang)
                                 + (mine < theirs ? tr (lang, " LU. Your drop may feel smaller in comparison.", " LU. Sinun droppisi voi tuntua pienemmältä.")
                                                  : tr (lang, " LU. Your quiet parts may feel thin in comparison.", " LU. Hiljaiset osasi voivat tuntua ohuilta.")));
        }
    }
    return notes;
}

// ------------------------------------------------------------------------------------------
juce::String listeningMarkdown (const ListeningInput& in, Lang lang)
{
    const auto& rep = in.report;
    const auto& a = rep.assessment;
    const auto& t = in.timeline;
    const auto sections = findSections (t);
    const auto refSections = findSections (in.referenceTimeline);
    juce::String md;

    md << "# " << tr (lang, "WWMA listening data", "WWMA-kuunteludata") << nl() << nl();
    md << tr (lang, "This file describes how your song behaves from start to end, in numbers. An AI assistant can't hear audio, "
                    "but it can read these numbers and the spectrogram picture and help you find where in the song a problem is and what to try. "
                    "Nothing was sent anywhere: this is a plain text file on your computer.",
                    "Tämä tiedosto kertoo numeroina, miten kappaleesi käyttäytyy alusta loppuun. Tekoälyavustaja ei kuule ääntä, "
                    "mutta se osaa lukea nämä luvut ja spektrogrammikuvan ja auttaa löytämään, missä kohtaa kappaletta ongelma on ja mitä kokeilla. "
                    "Mitään ei lähetetty minnekään: tämä on tavallinen tekstitiedosto koneellasi.") << nl() << nl();
    md << "**" << tr (lang, "How to use:", "Käyttö:") << "** "
       << tr (lang, "start a chat with an AI assistant, attach this file", "aloita keskustelu tekoälyavustajan kanssa, liitä tämä tiedosto");
    if (in.imageName.isNotEmpty())
        md << tr (lang, " and the picture `", " ja kuva `") << in.imageName << "`";
    if (in.referenceImageName.isNotEmpty())
        md << tr (lang, " (and `", " (ja `") << in.referenceImageName << "`)";
    md << tr (lang, ", and paste the question below. Then ask more: \"what about the bass in the second drop?\"",
                    ", ja liitä alla oleva kysymys. Kysy sitten lisää: \"entä basso toisessa dropissa?\"") << nl() << nl();

    // ---- the question
    md << "## " << tr (lang, "Question to paste", "Liitettävä kysymys") << nl() << nl();
    juce::String q;
    q << tr (lang, "I'm working on a ", "Työstän ") << (rep.presetName.isNotEmpty() ? rep.presetName : juce::String ("electronic"))
      << tr (lang, " track", " -kappaletta");
    if (rep.tempoBpm > 0.0f)
        q << " (" << num (rep.tempoBpm, 0, lang) << " BPM)";
    q << (rep.stage == Stage::mixing ? tr (lang, ", still mixing (not limited yet).", ", vielä miksausvaiheessa (ei limitoitu).")
                                     : tr (lang, ", mastering it.", ", masteroin sitä."));
    q << tr (lang, " I want it to sound good on a phone and in a car, not only in my studio. "
                   "The attached file has measurements from WWMA, an analyser plugin at the end of my chain: loudness, tonal balance in six ranges, "
                   "stereo width and peaks, for the whole song and for each part of it",
                   " Haluan sen kuulostavan hyvältä puhelimessa ja autossa, ei vain studiossani. "
                   "Liitteenä on mittauksia WWMA:sta, analysaattoripluginista ketjuni lopussa: äänekkyys, sävytasapaino kuudella alueella, "
                   "stereoleveys ja huiput koko kappaleelle ja sen jokaiselle osalle");
    if (in.imageName.isNotEmpty())
        q << tr (lang, ", plus a spectrogram picture", ", sekä spektrogrammikuva");
    if (rep.referenceName.isNotEmpty())
        q << tr (lang, ", and the same for a reference track I like", ", ja samat referenssikappaleesta, josta pidän");
    q << ". ";
    q << tr (lang, "Please: 1) name the three most important things to fix and in which part of the song they happen, "
                   "2) for each, tell me what to listen for and what to try in the DAW, step by step, "
                   "3) tell me what is already fine and should be left alone. "
                   "Don't trust any single number, explain when you are unsure, and remember my ears decide.",
                   "Kerro: 1) kolme tärkeintä korjattavaa asiaa ja missä kappaleen osassa ne ovat, "
                   "2) kustakin, mitä kuunnella ja mitä kokeilla DAW:ssa askel askeleelta, "
                   "3) mikä on jo kunnossa eikä sitä kannata koskea. "
                   "Älä luota yksittäiseen lukuun, kerro kun olet epävarma, ja muista että korvani ratkaisevat.");
    if (lang == Lang::finnish)
        q << " Vastaa suomeksi.";
    md << "> " << q << nl() << nl();

    // ---- overview
    md << "## " << tr (lang, "The song", "Kappale") << nl() << nl();
    md << "- " << tr (lang, "Preset (style target): ", "Preset (tyylin tavoite): ") << rep.presetName << nl();
    md << "- " << tr (lang, "Stage: ", "Vaihe: ") << (rep.stage == Stage::mixing ? tr (lang, "mixing", "miksaus") : tr (lang, "mastering", "masterointi")) << nl();
    if (rep.styleGuess.isNotEmpty())
        md << "- " << tr (lang, "Style guess from the music: ", "Tyyliarvaus musiikista: ") << rep.styleGuess << nl();
    md << "- " << tr (lang, "Music heard: ", "Musiikkia kuultu: ") << clockText (t.length())
       << tr (lang, " (silence left out)", " (hiljaisuus jätetty pois)") << nl();
    if (a.enoughData)
        md << "- " << tr (lang, "WWMA score: ", "WWMA-pisteet: ") << num (a.score, 0, lang) << " / 100 (" << verdictFor (a.score, lang) << ")" << nl();
    md << nl();

    if (a.enoughData)
    {
        md << "| " << tr (lang, "Measure", "Mittari") << " | " << tr (lang, "Value", "Arvo") << " | " << tr (lang, "Off the style target by", "Poikkeama tyylin tavoitteesta") << " |";
        if (rep.referenceName.isNotEmpty())
            md << " " << tr (lang, "Reference", "Referenssi") << " |";
        md << nl() << "|---|---|---|" << (rep.referenceName.isNotEmpty() ? "---|" : "") << nl();
        const auto refMetrics = rep.referenceName.isNotEmpty() ? toMap (keyMetrics (rep.referenceAssessment, rep.profile, rep.stage))
                                                               : std::map<std::string, std::pair<float, float>> {};
        for (const auto& k : keyMetrics (a, rep.profile, rep.stage))
        {
            if (! k.valid || k.id == "score")
                continue;
            md << "| " << metricName (k.id, lang) << " | " << metricValueText (k.id, k.value, lang) << " | "
               << (k.off < metricStep (k.id) ? juce::String ("0") : num (k.off, 1, lang)) << " |";
            if (rep.referenceName.isNotEmpty())
            {
                const auto it = refMetrics.find (k.id);
                md << " " << (it != refMetrics.end() ? metricValueText (k.id, it->second.first, lang) : juce::String ("-")) << " |";
            }
            md << nl();
        }
        md << nl();

        md << tr (lang, "WWMA's own findings, most important first:", "WWMA:n omat havainnot, tärkein ensin:") << nl() << nl();
        int count = 0;
        for (const auto& f : a.findings)
            if (f.id != "tip" && f.severity != Severity::good)
                md << ++count << ". " << f.title << (f.value.isNotEmpty() ? " (" + f.value + ")" : juce::String()) << nl();
        if (count == 0)
            md << tr (lang, "- none", "- ei yhtään") << nl();
        md << nl();
    }

    if (t.empty())
    {
        md << tr (lang, "No timeline yet: play the song from the start to the end with WWMA's window open, then save the report again.",
                        "Aikajanaa ei vielä ole: soita kappale alusta loppuun WWMA:n ikkuna auki ja tallenna raportti uudelleen.") << nl();
        return md;
    }

    // ---- sections
    auto sectionTable = [&] (const std::vector<SongSection>& secs, const Timeline& tl)
    {
        md << "| " << tr (lang, "Part", "Osa") << " | " << tr (lang, "Starts", "Alkaa") << " | " << tr (lang, "Length", "Pituus") << " | "
           << tr (lang, "Looks like", "Näyttää") << " | LUFS | " << tr (lang, "Peak dBTP", "Huippu dBTP") << " | " << tr (lang, "Width dB", "Leveys dB") << " | "
           << tr (lang, "Corr.", "Korr.") << " |";
        for (int r = 0; r < numRanges; ++r)
            md << " " << rangeShort (r, lang) << " |";
        md << nl() << "|---|---|---|---|---|---|---|---|";
        for (int r = 0; r < numRanges; ++r)
            md << "---|";
        md << nl();
        for (const auto& s : secs)
        {
            md << "| " << juce::String::charToString ((juce::juce_wchar) s.letter) << " | " << s.at << " | " << clockText ((float) (s.last - s.first + 1) * tl.step) << " | "
               << sectionKindName (s.kind, lang) << " | " << num (s.levels.loudness, 1, lang) << " | " << num (s.levels.peak, 1, lang) << " | "
               << num (s.levels.width, 0, lang) << " | " << num (s.levels.correlation, 2, lang) << " |";
            for (int r = 0; r < numRanges; ++r)
                md << " " << num (s.tone[(size_t) r], 1, lang, true) << " |";
            md << nl();
        }
        md << nl();
    };

    md << "## " << tr (lang, "Parts of the song", "Kappaleen osat") << nl() << nl();
    md << tr (lang, "WWMA split the song where the loudness, low end, top end or width change. The \"looks like\" names are guesses from the levels. "
                    "Range columns: how much more (+) or less (-) of that range the part has than the song on average, in dB.",
                    "WWMA jakoi kappaleen kohdista, joissa äänekkyys, bassopää, diskantti tai leveys muuttuvat. \"Näyttää\"-nimet ovat arvauksia tasojen perusteella. "
                    "Alueiden sarakkeet: kuinka paljon enemmän (+) tai vähemmän (-) osassa on kyseistä aluetta kuin kappaleessa keskimäärin, dB.") << nl() << nl();
    sectionTable (sections, t);

    if (! in.referenceTimeline.empty())
    {
        md << "### " << tr (lang, "The reference, ", "Referenssi, ") << rep.referenceName << nl() << nl();
        sectionTable (refSections, in.referenceTimeline);

        const auto* drop = loudest (sections, SectionKind::drop);
        const auto* refDrop = loudest (refSections, SectionKind::drop);
        if (drop != nullptr && refDrop != nullptr)
        {
            md << tr (lang, "The loudest drop of each, side by side (each range vs that drop's own total, dB):",
                            "Kummankin kovin drop rinnakkain (kukin alue suhteessa dropin kokonaistasoon, dB):") << nl() << nl();
            md << "| | LUFS | " << tr (lang, "Width dB", "Leveys dB") << " |";
            for (int r = 0; r < numRanges; ++r)
                md << " " << rangeShort (r, lang) << " |";
            md << nl() << "|---|---|---|";
            for (int r = 0; r < numRanges; ++r)
                md << "---|";
            md << nl();
            auto row = [&] (const juce::String& name, const SongSection& s)
            {
                md << "| " << name << " | " << num (s.levels.loudness, 1, lang) << " | " << num (s.levels.width, 0, lang) << " |";
                for (int r = 0; r < numRanges; ++r)
                    md << " " << num (s.levels.shape[(size_t) r], 1, lang) << " |";
                md << nl();
            };
            row (tr (lang, "Yours, ", "Sinun, ") + juce::String::charToString ((juce::juce_wchar) drop->letter), *drop);
            row (tr (lang, "Reference, ", "Referenssi, ") + juce::String::charToString ((juce::juce_wchar) refDrop->letter), *refDrop);
            md << nl();
        }
    }

    // ---- what WWMA noticed between the parts
    const auto notes = sectionNotes (sections, refSections, rep.stage, lang);
    md << "## " << tr (lang, "What changes between the parts", "Mitä osien välillä muuttuu") << nl() << nl();
    if (notes.empty())
        md << tr (lang, "Nothing stood out between the parts.", "Osien välillä ei erottunut mitään erityistä.") << nl();
    for (const auto& note : notes)
        md << "- " << note << nl();
    md << nl();

    // ---- the timeline, at most ~150 rows
    const int n = (int) t.points.size();
    const float rowSeconds = t.length() <= 300.0f ? 2.0f : (t.length() <= 600.0f ? 4.0f : 8.0f);
    const int per = juce::jmax (1, juce::roundToInt (rowSeconds / t.step));
    const bool songTimes = songTimeText (t, 0).isNotEmpty();
    const auto song = levelsOf (t, 0, n - 1);
    md << "## " << tr (lang, "Timeline", "Aikajana") << nl() << nl();
    md << tr (lang, "One row per ", "Yksi rivi per ") << num ((float) per * t.step, 0, lang)
       << tr (lang, " s of music. Range columns are dB vs the song's average balance, as above.",
                    " s musiikkia. Alueiden sarakkeet ovat dB suhteessa kappaleen keskimääräiseen tasapainoon, kuten yllä.") << nl() << nl();
    md << "| " << tr (lang, "Heard", "Kuultu") << " |" << (songTimes ? " " + tr (lang, "In project", "Projektissa") + " |" : juce::String())
       << tr (lang, " Part | LUFS | Peak | Width |", " Osa | LUFS | Huippu | Leveys |");
    for (int r = 0; r < numRanges; ++r)
        md << " " << rangeShort (r, lang) << " |";
    md << nl() << "|---|" << (songTimes ? "---|" : "") << "---|---|---|---|";
    for (int r = 0; r < numRanges; ++r)
        md << "---|";
    md << nl();
    for (int i = 0; i < n; i += per)
    {
        const int last = juce::jmin (n - 1, i + per - 1);
        const auto lv = levelsOf (t, i, last);
        char letter = '?';
        for (const auto& s : sections)
            if (i >= s.first && i <= s.last)
                letter = s.letter;
        md << "| " << clockText (heardTime (t, i)) << " |";
        if (songTimes)
        {
            const auto st = songTimeText (t, i);
            md << " " << (st.isNotEmpty() ? st : juce::String ("-")) << " |";
        }
        md << " " << juce::String::charToString ((juce::juce_wchar) letter) << " | " << num (lv.loudness, 1, lang) << " | " << num (lv.peak, 1, lang)
           << " | " << num (lv.width, 0, lang) << " |";
        for (int r = 0; r < numRanges; ++r)
            md << " " << num (lv.shape[(size_t) r] - song.shape[(size_t) r], 1, lang, true) << " |";
        md << nl();
    }
    md << nl();

    // ---- legend
    md << "## " << tr (lang, "How to read the numbers", "Lukujen tulkinta") << nl() << nl();
    md << "- LUFS: " << tr (lang, "loudness (EBU R128). +1 LU is a small but clear step; streaming services turn every song to about -14 LUFS.",
                               "äänekkyys (EBU R128). +1 LU on pieni mutta selvä ero; striimauspalvelut säätävät kaikki kappaleet noin -14 LUFSiin.") << nl();
    md << "- " << tr (lang, "Peak: true peak, dBTP. Above about -1 it can distort when turned into MP3/AAC.",
                            "Huippu: true peak, dBTP. Noin -1:n yläpuolella se voi säröytyä MP3/AAC-muunnoksessa.") << nl();
    md << "- " << tr (lang, "Width: side vs mid level above 140 Hz, dB. -10 is wide, -25 narrow. Correlation near 1 = close to mono, below 0 = parts cancel in mono.",
                            "Leveys: side-tason suhde mid-tasoon 140 Hz:n yläpuolella, dB. -10 on leveä, -25 kapea. Korrelaatio lähellä 1 = lähes mono, alle 0 = osia kumoutuu monona.") << nl();
    md << "- " << tr (lang, "Ranges: ", "Alueet: ");
    for (int r = 0; r < numRanges; ++r)
    {
        const auto info = rangeBands ((Range) r);
        auto hz = [] (double f) { return f >= 1000.0 ? juce::String (f / 1000.0, 1) + "k" : juce::String (juce::roundToInt (f)); };
        md << (r > 0 ? ", " : "") << rangeShort (r, lang) << " " << hz (Bands::lower (info.firstBand)) << "-" << hz (Bands::upper (info.lastBand)) << " Hz";
    }
    md << "." << nl();
    md << "- " << tr (lang, "The spectrogram: time left to right, low notes at the bottom, brighter = louder. Lines below it: loudness and width. Letters mark the parts.",
                            "Spektrogrammi: aika vasemmalta oikealle, matalat äänet alhaalla, kirkkaampi = kovempi. Alla viivoina äänekkyys ja leveys. Kirjaimet merkitsevät osat.") << nl();
    md << nl() << "---" << nl() << nl();
    md << tr (lang, "Your ears decide. Neither WWMA nor an AI can hear your song: check every suggestion by listening, on a phone and in a car too.",
                    "Korva ratkaisee. WWMA tai tekoäly ei kuule kappalettasi: tarkista jokainen ehdotus kuuntelemalla, myös puhelimella ja autossa.") << nl();
    return md;
}
} // namespace wwma
