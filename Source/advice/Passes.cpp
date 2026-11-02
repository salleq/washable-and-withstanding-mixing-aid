#include "Passes.h"
#include <cmath>

namespace wwma
{
PassRecord makePassRecord (const Assessment& a, const Profile& p, Stage stage, float seconds, double start)
{
    PassRecord r;
    if (! a.enoughData)
        return r;
    r.start = start;
    r.seconds = seconds;
    r.score = a.score;
    r.metrics = toMap (keyMetrics (a, p, stage));
    for (const auto& f : a.findings)
        if (f.severity == Severity::fix || f.severity == Severity::watch)
        {
            r.excess[f.id] = f.excess;
            r.titles[f.id] = f.title;
        }
    return r;
}

const PassRecord* matchingPass (const std::vector<PassRecord>& passes, double start)
{
    for (auto it = passes.rbegin(); it != passes.rend(); ++it)
        if (start < 0.0 || it->start < 0.0 || std::abs (it->start - start) <= 2.0)
            return &*it;
    return nullptr;
}

PassVerdict comparePasses (const PassRecord& before, const PassRecord& now, Lang lang)
{
    PassVerdict v;
    // compare like with like: wait until about as much of the part has played as last time
    const float need = std::max (5.0f, 0.8f * std::min (before.seconds, 12.0f));
    v.waitSeconds = std::max (0.0f, need - now.seconds);
    v.ready = now.valid() && v.waitSeconds <= 0.0f;
    if (! v.ready)
        return v;

    v.scoreDelta = now.score - before.score;
    std::vector<KeyMetric> current;
    for (const auto& [id, m] : now.metrics)
        current.push_back ({ id, m.first, m.second, true });
    for (const auto& c : compareMetrics (before.metrics, current))
    {
        if (c.id == "score" || c.verdict == 0)
            continue;
        if (c.verdict > 0) { ++v.better; v.betterNames.add (metricName (c.id, lang)); }
        else               { ++v.worse;  v.worseNames.add (metricName (c.id, lang)); }
    }
    const int balance = v.better - v.worse;
    if (v.scoreDelta >= 3.0f || (v.scoreDelta > -1.5f && balance >= 2))
        v.verdict = 1;
    else if (v.scoreDelta <= -3.0f || (v.scoreDelta < 1.5f && balance <= -2))
        v.verdict = -1;
    return v;
}

juce::String firstStep (const juce::String& howTo)
{
    auto line = juce::StringArray::fromLines (howTo)[0].trim();
    if (line.length() > 2 && juce::CharacterFunctions::isDigit (line[0]) && line[1] == '.')
        line = line.substring (2).trim();
    return line;
}

TrialResult judgeTrial (const Trial& t, const PassRecord& now, bool newPass, bool samePart, Lang lang, bool showPoints)
{
    TrialResult r;
    const auto it = t.before.excess.find (t.findingId);
    r.excessBefore = it != t.before.excess.end() ? it->second : 0.0f;
    if (! newPass)
    {
        r.headline = tr (lang, "Make the change, then play the same part again", "Tee muutos ja soita sama kohta uudelleen");
        r.detail = t.step;
        return r;
    }
    if (! samePart)
    {
        r.headline = tr (lang, "Play the same part as before", "Soita sama kohta kuin äsken");
        r.detail = t.before.start >= 0.0
                       ? tr (lang, "The last pass started at ", "Edellinen soitto alkoi kohdasta ")
                             + juce::String ((int) t.before.start / 60) + ":" + juce::String ((int) t.before.start % 60).paddedLeft ('0', 2)
                             + tr (lang, ". Only the same part can be compared fairly.", ". Vain samaa kohtaa voi verrata reilusti.")
                       : juce::String();
        return r;
    }
    r.overall = comparePasses (t.before, now, lang);
    if (! r.overall.ready)
    {
        r.headline = tr (lang, "Listening... ", "Kuuntelen... ") + juce::String ((int) std::ceil (r.overall.waitSeconds)) + " s";
        r.detail = tr (lang, "Comparing with the pass before your change.", "Vertaan muutosta edeltävään soittoon.");
        return r;
    }

    const auto after = now.excess.find (t.findingId);
    const bool gone = after == now.excess.end();
    r.excessAfter = gone ? 0.0f : after->second;
    const bool findingBetter = gone || r.excessAfter < 0.7f * r.excessBefore;
    const bool findingWorse = r.excessAfter > 1.2f * r.excessBefore + 0.1f;
    const float d = r.overall.scoreDelta;
    const auto points = juce::String (juce::roundToInt (d)) ;
    const auto signedPoints = (d >= 0.0f ? "+" : "") + points;
    const auto scoreText = showPoints ? tr (lang, "Score ", "Pisteet ") + signedPoints + ". " : juce::String();

    if (findingWorse || d <= -3.0f || (! findingBetter && r.overall.verdict < 0))
    {
        r.kind = TrialResult::Kind::worse;
        r.headline = tr (lang, "That made it worse: undo it", "Tämä huononsi: peru muutos");
        r.detail = scoreText
                   + (r.overall.worseNames.isEmpty() ? juce::String()
                                                     : tr (lang, "Worse: ", "Huonompi: ") + r.overall.worseNames.joinIntoString (", ") + ". ")
                   + tr (lang, "Undo (Ctrl/Cmd+Z), then try a smaller move, or the next step of the fix.",
                               "Peru (Ctrl/Cmd+Z) ja kokeile pienempää muutosta tai korjauksen seuraavaa askelta.");
    }
    else if (findingBetter && d > -1.5f)
    {
        r.kind = TrialResult::Kind::helped;
        r.headline = gone ? tr (lang, "It helped: fixed. Keep it", "Auttoi: korjattu. Pidä muutos")
                                           : tr (lang, "It helped. Keep it", "Auttoi. Pidä muutos");
        r.detail = scoreText
                   + (r.overall.betterNames.isEmpty() ? juce::String()
                                                      : tr (lang, "Better: ", "Parempi: ") + r.overall.betterNames.joinIntoString (", ") + ". ")
                   + (r.overall.worseNames.isEmpty() ? juce::String()
                                                     : tr (lang, "Watch: ", "Seuraa: ") + r.overall.worseNames.joinIntoString (", ") + ". ")
                   + tr (lang, "Save the DAW project as a new version, and listen once at matched loudness to confirm.",
                               "Tallenna DAW-projekti uutena versiona ja kuuntele kerran tasatulla äänekkyydellä varmistukseksi.");
    }
    else
    {
        r.kind = TrialResult::Kind::noChange;
        r.headline = tr (lang, "No clear change yet", "Ei selvää muutosta vielä");
        r.detail = scoreText
                   + tr (lang, "The change was probably too small, or on the wrong track. Go a bit further (e.g. 2 dB more), or check you changed the sound named in the step.",
                               "Muutos oli luultavasti liian pieni tai väärällä raidalla. Mene vähän pidemmälle (esim. 2 dB lisää) tai tarkista, että muutit askeleessa mainittua ääntä.");
    }
    return r;
}
TrialResult askEars (TrialResult meter, const Trial& t, bool beforeReady, Lang lang)
{
    if (meter.kind == TrialResult::Kind::waiting)
        return meter;
    meter.askEar = true;
    const auto said = meter.kind == TrialResult::Kind::helped ? tr (lang, "The meter: it helped.", "Mittari: auttoi.")
                    : meter.kind == TrialResult::Kind::worse  ? tr (lang, "The meter: it got worse.", "Mittari: huononi.")
                                                              : tr (lang, "The meter: no clear change.", "Mittari: ei selvää muutosta.");
    meter.headline = said + tr (lang, " What do your ears say?", " Mitä korva sanoo?");
    juce::String names;
    if (! meter.overall.betterNames.isEmpty())
        names << tr (lang, "Better: ", "Parempi: ") << meter.overall.betterNames.joinIntoString (", ", 0, 2) << ". ";
    if (! meter.overall.worseNames.isEmpty())
        names << tr (lang, "Worse: ", "Huonompi: ") << meter.overall.worseNames.joinIntoString (", ", 0, 2) << ". ";
    meter.detail = names
                   + (beforeReady ? tr (lang, "Press BEFORE and MIX in turn (same loudness). Which one do you like better?",
                                              "Paina vuorotellen ENNEN ja MIKSI (sama äänekkyys). Kumpi kuulostaa paremmalta?")
                                  : tr (lang, "Compare with your DAW's undo and redo, at the same volume. Which one do you like better?",
                                              "Vertaa DAW:n kumoa/tee uudelleen -toiminnolla samalla äänenvoimakkuudella. Kumpi kuulostaa paremmalta?"));
    juce::ignoreUnused (t);
    return meter;
}

TrialResult judgeWithEars (const TrialResult& meter, const Trial& t, int overrulesSoFar, Lang lang)
{
    TrialResult r = meter;
    r.askEar = false;
    r.overrule = false;
    const bool meterGood = meter.kind == TrialResult::Kind::helped;
    const bool meterBad = meter.kind == TrialResult::Kind::worse;
    const auto keep = tr (lang, "Save the DAW project as a new version.", "Tallenna DAW-projekti uutena versiona.");
    if (t.ear == 2)
    {
        r.kind = TrialResult::Kind::noChange;
        r.headline = tr (lang, "You hear no difference: don't go further this way", "Et kuule eroa: älä jatka tähän suuntaan");
        r.detail = tr (lang, "If you can't hear it, nobody else will either. Undo it or leave it, and move on to the next thing.",
                             "Jos et itse kuule eroa, ei kukaan muukaan kuule. Peru tai jätä, ja siirry seuraavaan.");
    }
    else if (t.ear > 0 && ! meterBad)
    {
        r.kind = TrialResult::Kind::helped;
        r.headline = meterGood ? tr (lang, "Your ears and the meter agree: keep it", "Korva ja mittari ovat samaa mieltä: pidä muutos")
                               : tr (lang, "Your ears hear it better: keep it", "Korva kuulee parannuksen: pidä muutos");
        r.detail = keep;
    }
    else if (t.ear > 0 && meterBad)
    {
        r.kind = TrialResult::Kind::helped;
        r.headline = tr (lang, "Your ears like it, the meter doesn't: your ears win", "Korva pitää, mittari ei: korva voittaa");
        r.detail = tr (lang, "Keep it if it still sounds better quietly and on a phone. The meter only measures; the music is what you hear. ",
                             "Pidä, jos se kuulostaa paremmalta myös hiljaa ja puhelimesta. Mittari vain mittaa; musiikki on se, mitä kuulet. ")
                   + keep;
    }
    else if (t.ear < 0 && meterGood)
    {
        r.kind = TrialResult::Kind::worse;
        r.overrule = true;
        r.headline = tr (lang, "Your ears win: undo it", "Korva voittaa: peru muutos");
        r.detail = tr (lang, "The meter only sees \"", "Mittari näkee vain kohdan \"") + t.title
                   + tr (lang, "\". The change probably took something else away: warmth, punch or space. For this song it may be a choice, so WWMA stops counting it.",
                               "\". Muutos vei luultavasti jotain muuta: lämpöä, iskua tai tilaa. Tässä biisissä se voi olla tyylivalinta, joten WWMA lakkaa laskemasta sitä.");
        if (overrulesSoFar >= 1)
            r.detail << tr (lang, " Your ears have disagreed with the style preset more than once: load a track you love and pick the Reference track preset, so the target is that sound and not a guess.",
                                  " Korva on ollut tyylipresetin kanssa eri mieltä useammin kuin kerran: lataa kappale, jota rakastat, ja valitse Referenssiraita-preset, niin tavoite on se soundi eikä arvaus.");
    }
    else
    {
        r.kind = TrialResult::Kind::worse;
        r.headline = tr (lang, "BEFORE was better: undo it", "ENNEN oli parempi: peru muutos");
        r.detail = tr (lang, "Undo (Ctrl/Cmd+Z), then try a smaller move, or the next step of the fix.",
                             "Peru (Ctrl/Cmd+Z) ja kokeile pienempää muutosta tai korjauksen seuraavaa askelta.");
    }
    return r;
}
namespace
{
    std::array<float, numRanges> rangeLevels (const std::array<float, Bands::count>& spectrum)
    {
        std::array<float, numRanges> out {};
        for (int r = 0; r < numRanges; ++r)
        {
            const auto info = rangeBands ((Range) r);
            out[(size_t) r] = sumBands (spectrum, info.firstBand, info.lastBand);
        }
        return out;
    }

    float distance (const std::array<float, numRanges>& a, const std::array<float, numRanges>& b)
    {
        float d = 0.0f;
        for (int r = 0; r < numRanges; ++r)
            d += std::abs (a[(size_t) r] - b[(size_t) r]);
        return d / (float) numRanges;
    }
}

DriftCheck overOptimised (const SavedSnapshot& snap, const Assessment& now, const Assessment* reference, Lang lang)
{
    DriftCheck c;
    if (! now.enoughData)
        return c;
    c.scoreGain = now.score - snap.score;

    juce::StringArray moved;
    std::array<float, numRanges> before {}, after = rangeLevels (now.measures.spectrum);
    const bool haveCurve = snap.spectrum.size() == (size_t) Bands::count;
    if (haveCurve)
    {
        std::array<float, Bands::count> spec {};
        std::copy (snap.spectrum.begin(), snap.spectrum.end(), spec.begin());
        before = rangeLevels (spec);
        for (int r = 0; r < numRanges; ++r)
            if (std::abs (after[(size_t) r] - before[(size_t) r]) >= 2.0f)
            {
                ++c.rangesMoved;
                moved.add (rangeNickname ((Range) r, lang).toLowerCase());
            }
        if (reference != nullptr && reference->measures.valid)
        {
            const auto ref = rangeLevels (reference->measures.spectrum);
            c.refDistanceChange = distance (after, ref) - distance (before, ref);
        }
    }

    juce::String why;
    if (reference != nullptr && c.refDistanceChange >= 1.0f && c.scoreGain >= 5.0f)
        why = tr (lang, "The meter likes it more, but the tone moved away from your reference track.",
                        "Mittari pitää tästä enemmän, mutta sävy siirtyi kauemmas referenssiraidastasi.");
    else if (c.rangesMoved >= 3)
        why = tr (lang, "The tone changed in ", "Sävy muuttui ") + juce::String (c.rangesMoved)
              + tr (lang, " ranges at once (", " alueella kerralla (") + moved.joinIntoString (", ") + ").";
    else if (c.scoreGain >= 15.0f)
        why = tr (lang, "The meter jumped a whole lot since the snapshot.", "Mittari hyppäsi paljon snapshotin jälkeen.");
    if (why.isEmpty())
        return c;

    c.warn = true;
    c.headline = tr (lang, "Many changes since the snapshot: listen before you go on", "Paljon muutoksia snapshotin jälkeen: kuuntele ennen kuin jatkat");
    c.detail = why + tr (lang, " Press BEFORE and MIX in turn at the same loudness. The meter can't hear music; you can.",
                               " Paina vuorotellen ENNEN ja MIKSI samalla äänekkyydellä. Mittari ei kuule musiikkia, sinä kuulet.");
    return c;
}
} // namespace wwma
