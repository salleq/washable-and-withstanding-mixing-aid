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

TrialResult judgeTrial (const Trial& t, const PassRecord& now, bool newPass, bool samePart, Lang lang)
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

    if (findingWorse || d <= -3.0f || (! findingBetter && r.overall.verdict < 0))
    {
        r.kind = TrialResult::Kind::worse;
        r.headline = tr (lang, "That made it worse: undo it", "Tämä huononsi: peru muutos");
        r.detail = tr (lang, "Score ", "Pisteet ") + signedPoints + ". "
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
        r.detail = tr (lang, "Score ", "Pisteet ") + signedPoints + ". "
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
        r.detail = tr (lang, "Score ", "Pisteet ") + signedPoints + ". "
                   + tr (lang, "The change was probably too small, or on the wrong track. Go a bit further (e.g. 2 dB more), or check you changed the sound named in the step.",
                               "Muutos oli luultavasti liian pieni tai väärällä raidalla. Mene vähän pidemmälle (esim. 2 dB lisää) tai tarkista, että muutit askeleessa mainittua ääntä.");
    }
    return r;
}
} // namespace wwma
