#include "MixReport.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
namespace
{
    float outside (float v, float lo, float hi) { return std::max ({ 0.0f, lo - v, v - hi }); }

    const char* rangeIds[] = { "range0", "range1", "range2", "range3", "range4", "range5" };

    juce::String number (float v, int decimals, Lang lang, bool sign = false)
    {
        auto s = decimals == 0 ? juce::String (juce::roundToInt (std::abs (v))) : juce::String (std::abs (v), decimals);
        if (lang == Lang::finnish)
            s = s.replaceCharacter ('.', ',');
        if (v < 0.0f && s != "0" && s.containsAnyOf ("123456789"))
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + s;
        return (sign && v > 0.0f && s.containsAnyOf ("123456789") ? "+" : "") + s;
    }

    juce::String mdEscape (const juce::String& s) { return s.replace ("|", "/"); }
} // namespace

std::vector<KeyMetric> keyMetrics (const Assessment& a, const Profile& p, Stage stage)
{
    const auto& m = a.measures;
    std::vector<KeyMetric> out;
    if (! m.valid)
        return out;

    const bool master = stage == Stage::mastering;
    out.push_back ({ "score", a.score, 100.0f - a.score });
    out.push_back ({ "loudness", m.loudness, master ? outside (m.loudness, p.lufsMin, p.lufsMax) : 0.0f });
    out.push_back ({ "truepeak", m.truePeak, master ? std::max (0.0f, m.truePeak - p.truePeakMax) : std::max (0.0f, m.truePeak + 3.0f) });
    out.push_back ({ "plr", m.plr, outside (m.plr, p.plrMin, p.plrMax) });
    out.push_back ({ "lra", m.lra, std::max (0.0f, m.lra - p.lraMax) });
    out.push_back ({ "kick", m.lowContrast, std::max (0.0f, p.lowContrastMin - m.lowContrast), m.kickValid });
    out.push_back ({ "snap", m.highContrast, outside (m.highContrast, p.highContrastMin, p.highContrastMax), m.contrastValid });
    out.push_back ({ "phonebass", m.phoneBass, std::max (0.0f, -m.phoneBass) });
    out.push_back ({ "lowwidth", m.lowWidth, std::max (0.0f, m.lowWidth - p.lowWidthMax) });
    out.push_back ({ "width", m.width, outside (m.width, p.widthMin, p.widthMax) });
    out.push_back ({ "correlation", m.correlation, std::max (0.0f, 0.5f - m.correlation) });
    out.push_back ({ "monoloss", m.monoLoss, std::max (0.0f, m.monoLoss - 3.0f) });
    out.push_back ({ "balance", m.balance, std::max (0.0f, std::abs (m.balance) - 1.0f) });
    for (int r = 0; r < numRanges; ++r)
    {
        const float d = m.rangeDeviation[(size_t) r];
        out.push_back ({ rangeIds[r], d, std::max (0.0f, std::abs (d) - p.tolerance) });
    }
    return out;
}

juce::String metricName (const std::string& id, Lang lang)
{
    for (int r = 0; r < numRanges; ++r)
        if (id == rangeIds[r])
            return tr (lang, "Tone: ", "Sävy: ") + rangeNickname ((Range) r, lang);
    if (id == "score")       return tr (lang, "Score", "Pisteet");
    if (id == "loudness")    return tr (lang, "Loudness", "Äänekkyys");
    if (id == "truepeak")    return tr (lang, "True peak", "True peak");
    if (id == "plr")         return tr (lang, "Punch (PLR)", "Iskevyys (PLR)");
    if (id == "lra")         return tr (lang, "Loudness range", "Äänekkyysvaihtelu (LRA)");
    if (id == "kick")        return tr (lang, "Kick above the bass", "Kick basson yläpuolella");
    if (id == "snap")        return tr (lang, "Drum snap", "Rumpujen terävyys");
    if (id == "phonebass")   return tr (lang, "Bass on phones", "Basso puhelimessa");
    if (id == "lowwidth")    return tr (lang, "Low end width", "Bassopään leveys");
    if (id == "width")       return tr (lang, "Stereo width", "Stereoleveys");
    if (id == "correlation") return tr (lang, "Phase (correlation)", "Vaihe (korrelaatio)");
    if (id == "monoloss")    return tr (lang, "Loss in mono", "Häviö monona");
    if (id == "balance")     return tr (lang, "Left/right balance", "Vasen/oikea-tasapaino");
    return juce::String (id);
}

juce::String metricValueText (const std::string& id, float v, Lang lang)
{
    if (id == "score")       return number (v, 0, lang);
    if (id == "loudness")    return number (v, 1, lang) + " LUFS";
    if (id == "truepeak")    return number (v, 1, lang) + " dBTP";
    if (id == "plr" || id == "lra") return number (v, 1, lang) + " LU";
    if (id == "correlation") return number (v, 2, lang);
    if (id == "monoloss")    return number (-v, 1, lang) + " dB";
    if (id.rfind ("range", 0) == 0 || id == "phonebass" || id == "balance") return number (v, 1, lang, true) + " dB";
    return number (v, 1, lang) + " dB";
}

float metricStep (const std::string& id)
{
    if (id == "score") return 3.0f;
    if (id == "correlation") return 0.05f;
    return 0.5f;
}

std::map<std::string, std::pair<float, float>> toMap (const std::vector<KeyMetric>& metrics)
{
    std::map<std::string, std::pair<float, float>> out;
    for (const auto& k : metrics)
        if (k.valid)
            out[k.id] = { k.value, k.off };
    return out;
}

std::vector<MetricChange> compareMetrics (const std::map<std::string, std::pair<float, float>>& before,
                                          const std::vector<KeyMetric>& now)
{
    std::vector<MetricChange> out;
    for (const auto& k : now)
    {
        if (! k.valid)
            continue;
        const auto it = before.find (k.id);
        if (it == before.end())
            continue;
        MetricChange c;
        c.id = k.id;
        c.before = it->second.first;
        c.now = k.value;
        const float delta = it->second.second - k.off; // positive = closer to the target now
        c.verdict = delta >= metricStep (k.id) ? 1 : (delta <= -metricStep (k.id) ? -1 : 0);
        out.push_back (c);
    }
    std::stable_sort (out.begin(), out.end(), [] (const MetricChange& a, const MetricChange& b)
    {
        auto rank = [] (int v) { return v > 0 ? 0 : (v < 0 ? 1 : 2); };
        return rank (a.verdict) < rank (b.verdict);
    });
    return out;
}

// ------------------------------------------------------------------------------------------
juce::var reportJson (const ReportInput& in)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "wwma-report-1");
    root->setProperty ("time", in.time.toISO8601 (true));
    root->setProperty ("preset", in.presetName);
    root->setProperty ("stage", in.stage == Stage::mixing ? "mix" : "master");
    root->setProperty ("window", in.window == Window::song ? "song" : "now");
    root->setProperty ("secondsAnalysed", in.secondsAnalysed);
    root->setProperty ("tempoBpm", in.tempoBpm);
    root->setProperty ("score", in.assessment.score);

    auto* metrics = new juce::DynamicObject();
    for (const auto& k : keyMetrics (in.assessment, in.profile, in.stage))
    {
        if (! k.valid)
            continue;
        auto* one = new juce::DynamicObject();
        one->setProperty ("value", k.value);
        one->setProperty ("off", k.off);
        metrics->setProperty (juce::Identifier (k.id), juce::var (one));
    }
    root->setProperty ("metrics", juce::var (metrics));

    juce::Array<juce::var> findings;
    for (const auto& f : in.assessment.findings)
        if (f.id != "tip")
            findings.add (juce::String (f.id));
    root->setProperty ("findings", findings);

    auto* chain = new juce::DynamicObject();
    chain->setProperty ("eqOn", in.chain.eqOn);
    juce::Array<juce::var> bands;
    for (size_t b = 0; b < (size_t) ChainSettings::numBands; ++b)
    {
        auto* band = new juce::DynamicObject();
        band->setProperty ("freq", in.chain.freq[b]);
        band->setProperty ("gain", in.chain.gain[b]);
        band->setProperty ("q", in.chain.q[b]);
        bands.add (juce::var (band));
    }
    chain->setProperty ("eqBands", bands);
    chain->setProperty ("limiterOn", in.chain.limiterOn);
    chain->setProperty ("limiterGain", in.chain.limiterGain);
    chain->setProperty ("ceiling", in.chain.ceiling);
    chain->setProperty ("releaseMs", in.chain.releaseMs);
    root->setProperty ("chain", juce::var (chain));
    return juce::var (root);
}

std::map<std::string, std::pair<float, float>> metricsFromJson (const juce::var& json)
{
    std::map<std::string, std::pair<float, float>> out;
    if (auto* metrics = json.getProperty ("metrics", {}).getDynamicObject())
        for (const auto& p : metrics->getProperties())
            out[p.name.toString().toStdString()] = { (float) p.value.getProperty ("value", 0.0), (float) p.value.getProperty ("off", 0.0) };
    return out;
}

juce::String reportMarkdown (const ReportInput& in, Lang lang, const juce::var& previous, const juce::String& previousName)
{
    const auto& a = in.assessment;
    juce::String md;
    const auto nl = juce::String ("\n");

    md << "# " << tr (lang, "WWMA mix report", "WWMA-miksausraportti") << nl << nl;
    md << "- " << tr (lang, "Date: ", "Päivä: ") << in.time.formatted ("%Y-%m-%d %H:%M") << nl;
    md << "- " << tr (lang, "Preset: ", "Preset: ") << in.presetName << nl;
    md << "- " << tr (lang, "Stage: ", "Vaihe: ") << (in.stage == Stage::mixing ? tr (lang, "mixing", "miksaus") : tr (lang, "mastering", "masterointi")) << nl;
    md << "- " << tr (lang, "Measured: ", "Mitattu: ")
       << (in.window == Window::song ? tr (lang, "whole song, ", "koko kappale, ") : tr (lang, "last 3 seconds, ", "viimeiset 3 sekuntia, "))
       << number (in.secondsAnalysed, 0, lang) << " s" << nl;
    if (in.tempoBpm > 0.0f)
        md << "- " << tr (lang, "Tempo: ", "Tempo: ") << number (in.tempoBpm, 0, lang) << " BPM" << nl;
    if (in.styleGuess.isNotEmpty())
        md << "- " << tr (lang, "Style guess: ", "Tyyliarvaus: ") << in.styleGuess << nl;
    md << nl;

    if (! a.enoughData)
    {
        md << tr (lang, "Not enough music was analysed for a report yet. Play the song and save the report again.",
                        "Musiikkia ei ole vielä analysoitu tarpeeksi raporttia varten. Soita kappale ja tallenna raportti uudelleen.") << nl;
        return md;
    }

    md << "## " << tr (lang, "How close to a good mix", "Kuinka lähellä hyvää miksiä") << nl << nl;
    md << "**" << number (a.score, 0, lang) << " / 100: " << verdictFor (a.score, lang) << "**" << nl << nl;
    if (a.nextStep.isNotEmpty())
        md << a.nextStep << nl << nl;

    md << "| " << tr (lang, "Playback", "Toisto") << " | " << tr (lang, "Estimate", "Arvio") << " | |" << nl << "|---|---|---|" << nl;
    for (int i = 0; i < (int) Playback::count; ++i)
        md << "| " << playbackName ((Playback) i, lang) << " | " << number (a.translation[(size_t) i].score, 0, lang) << " | "
           << mdEscape (a.translation[(size_t) i].note) << " |" << nl;
    md << nl;

    md << "## " << tr (lang, "What to fix", "Mitä korjata") << nl << nl;
    int n = 0;
    for (const auto& f : a.findings)
    {
        if (f.id == "tip" || f.severity == Severity::good)
            continue;
        md << "### " << ++n << ". " << f.title << nl << nl;
        if (f.value.isNotEmpty())
            md << "*" << f.value << "*" << nl << nl;
        md << f.why << nl << nl;
        md << f.howTo << nl << nl;
    }
    if (n == 0)
        md << tr (lang, "Nothing to fix. Check on a phone and in a car, then trust your ears.",
                        "Ei korjattavaa. Tarkista puhelimesta ja autossa, ja luota sitten korviisi.") << nl << nl;

    const auto metrics = keyMetrics (a, in.profile, in.stage);
    md << "## " << tr (lang, "Numbers", "Luvut") << nl << nl;
    md << "| " << tr (lang, "Measure", "Mittari") << " | " << tr (lang, "Value", "Arvo") << " | " << tr (lang, "On target", "Tavoitteessa") << " |"
       << (in.referenceName.isNotEmpty() ? " " + in.referenceName + " |" : juce::String()) << nl;
    md << "|---|---|---|" << (in.referenceName.isNotEmpty() ? "---|" : "") << nl;
    const auto refMetrics = in.referenceName.isNotEmpty() ? toMap (keyMetrics (in.referenceAssessment, in.profile, in.stage))
                                                          : std::map<std::string, std::pair<float, float>> {};
    for (const auto& k : metrics)
    {
        if (! k.valid)
            continue;
        md << "| " << metricName (k.id, lang) << " | " << metricValueText (k.id, k.value, lang) << " | "
           << (k.off < metricStep (k.id) ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")) : tr (lang, "off by ", "poikkeama ") + number (k.off, 1, lang)) << " |";
        if (in.referenceName.isNotEmpty())
        {
            const auto it = refMetrics.find (k.id);
            md << " " << (it != refMetrics.end() ? metricValueText (k.id, it->second.first, lang) : juce::String ("-")) << " |";
        }
        md << nl;
    }
    md << nl;

    md << "## " << tr (lang, "Mastering EQ and limiter", "Masterointi-EQ ja limitteri") << nl << nl;
    if (! in.chain.anyOn())
        md << tr (lang, "Both off: WWMA only measured.", "Molemmat pois päältä: WWMA vain mittasi.") << nl << nl;
    else
    {
        const auto& adv = in.chainAdvice;
        md << "| " << tr (lang, "Knob", "Nuppi") << " | " << tr (lang, "Your setting", "Sinun asetus") << " | " << tr (lang, "Suggested", "Ehdotus") << " |" << nl
           << "|---|---|---|" << nl;
        auto row = [&] (const juce::String& name, const juce::String& yours, const Suggestion& s)
        {
            md << "| " << name << " | " << yours << " | " << (adv.ready && s.shown ? s.text : juce::String ("-")) << " |" << nl;
        };
        auto db = [&] (float v) { return number (v, 1, lang, true) + " dB"; };
        if (in.chain.eqOn)
            for (size_t b = 0; b < (size_t) ChainSettings::numBands; ++b)
            {
                const auto name = eqBandName ((int) b, lang);
                row (name, db (in.chain.gain[b]), adv.gain[b]);
                row (name + " Hz", number (in.chain.freq[b], 0, lang) + " Hz", adv.freq[b]);
                if (b == ChainSettings::lowMid || b == ChainSettings::highMid)
                    row (name + " Q", number (in.chain.q[b], 2, lang), adv.q[b]);
            }
        if (in.chain.limiterOn)
        {
            row (tr (lang, "Limiter gain", "Limitterin gain"), db (in.chain.limiterGain), adv.limiterGain);
            row (tr (lang, "Ceiling", "Katto"), number (in.chain.ceiling, 1, lang) + " dBTP", adv.ceiling);
            row ("Release", number (in.chain.releaseMs, 0, lang) + " ms", adv.release);
        }
        md << nl << tr (lang, "The suggestions were only shown; every value above was set by hand.",
                              "Ehdotukset vain näytettiin; jokainen arvo yllä on säädetty käsin.") << nl << nl;
    }

    if (! in.snapshots.empty())
    {
        md << "## " << tr (lang, "Snapshots", "Snapshotit") << nl << nl;
        md << "| # | " << tr (lang, "Time", "Aika") << " | " << tr (lang, "Score", "Pisteet") << " | " << tr (lang, "New DAW version saved", "Uusi DAW-versio tallennettu") << " |" << nl
           << "|---|---|---|---|" << nl;
        int i = 0;
        for (const auto& s : in.snapshots)
            md << "| " << ++i << " | " << s.time.formatted ("%Y-%m-%d %H:%M") << " | " << number (s.score, 0, lang) << " | "
               << (s.confirmed ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 ")) + s.confirmedAt.formatted ("%H:%M") : tr (lang, "not confirmed", "ei kuitattu")) << " |" << nl;
        md << nl;
    }

    if (! previous.isVoid())
    {
        const auto changes = compareMetrics (metricsFromJson (previous), metrics);
        md << "## " << tr (lang, "Since the previous report", "Edellisen raportin jälkeen") << nl << nl;
        md << tr (lang, "Compared with ", "Verrattu raporttiin ") << "`" << previousName << "`";
        const auto prevTime = previous.getProperty ("time", {}).toString();
        if (prevTime.isNotEmpty())
            md << " (" << juce::Time::fromISO8601 (prevTime).formatted ("%Y-%m-%d %H:%M") << ")";
        md << "." << nl << nl;
        md << "| " << tr (lang, "Measure", "Mittari") << " | " << tr (lang, "Before", "Ennen") << " | " << tr (lang, "Now", "Nyt") << " | |" << nl << "|---|---|---|---|" << nl;
        int better = 0, worse = 0;
        for (const auto& c : changes)
        {
            better += c.verdict > 0;
            worse += c.verdict < 0;
            md << "| " << metricName (c.id, lang) << " | " << metricValueText (c.id, c.before, lang) << " | " << metricValueText (c.id, c.now, lang) << " | "
               << (c.verdict > 0 ? tr (lang, "better", "parempi") : (c.verdict < 0 ? tr (lang, "worse", "huonompi") : juce::String ("="))) << " |" << nl;
        }
        md << nl << juce::String (better) << tr (lang, " better, ", " parempaa, ") << juce::String (worse) << tr (lang, " worse.", " huonompaa.") << nl << nl;
    }

    md << "---" << nl << nl;
    md << tr (lang, "Your ears decide. These numbers are advice, not rules. If it sounds good on a phone and in a car, it is good.",
                    "Korva ratkaisee. Luvut ovat neuvoja, eivät sääntöjä. Jos se kuulostaa hyvältä puhelimessa ja autossa, se on hyvä.") << nl;
    return md;
}
} // namespace wwma
