#include "AiPrompt.h"
#include <cmath>

namespace wwma::ai
{
namespace
{
    const juce::String arrow = juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "));

    juce::String db (float v, Lang lang)
    {
        auto s = juce::String (std::abs (v), 1);
        s = (v < -0.05f ? "-" : (v > 0.05f ? "+" : "")) + s;
        return lang == Lang::finnish ? s.replaceCharacter ('.', ',') : s;
    }

    juce::String hz (float v, Lang lang)
    {
        if (v >= 1000.0f)
        {
            auto s = juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2);
            return (lang == Lang::finnish ? s.replaceCharacter ('.', ',') : s) + " kHz";
        }
        return juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String plain (float v, int decimals) { return juce::String (v, decimals); }

    std::optional<float> number (const juce::var& v)
    {
        if (v.isDouble() || v.isInt() || v.isInt64())
        {
            const auto d = (double) v;
            if (std::isfinite (d))
                return (float) d;
        }
        if (v.isString())
        {
            const auto s = v.toString().trim();
            if (s.containsOnly ("+-.0123456789") && s.containsAnyOf ("0123456789"))
                return s.getFloatValue();
        }
        return std::nullopt;
    }

    juce::String text (const juce::var& v, int maxLength = 400)
    {
        if (v.isVoid() || v.isUndefined() || (v.isString() && v.toString().isEmpty()))
            return {};
        if (! (v.isString() || v.isDouble() || v.isInt() || v.isInt64()))
            return {};
        const auto s = v.toString().trim();
        return s == "null" ? juce::String() : s.substring (0, maxLength);
    }

    /** The JSON block: the last ```json fence, else the last {...} that mentions "suggestions". */
    bool findBlock (const juce::String& answer, int& start, int& end, juce::String& json)
    {
        const int fence = answer.lastIndexOfIgnoreCase ("```json");
        if (fence >= 0)
        {
            const int open = fence + 7;
            const int close = answer.indexOf (open, "```");
            start = fence;
            end = close >= 0 ? close + 3 : answer.length();
            json = answer.substring (open, close >= 0 ? close : answer.length());
            return true;
        }
        const int key = answer.lastIndexOf ("\"suggestions\"");
        if (key < 0)
            return false;
        const int open = answer.substring (0, key).lastIndexOfChar ('{');
        const int close = answer.lastIndexOfChar ('}');
        if (open < 0 || close < key)
            return false;
        start = open;
        end = close + 1;
        json = answer.substring (open, close + 1);
        return true;
    }

    ChainChange chainFrom (const juce::var& c)
    {
        ChainChange ch;
        if (! c.isObject())
            return ch;
        if (const auto* eq = c.getProperty ("eq", {}).getArray())
            for (const auto& b : *eq)
            {
                const auto band = number (b.getProperty ("band", {}));
                if (! band.has_value() || *band < 0.0f || *band > 3.0f || std::abs (*band - std::round (*band)) > 0.01f)
                    continue; // not one of the four bands: ignored
                const auto i = (size_t) std::lround (*band);
                ch.freq[i] = number (b.getProperty ("freq_hz", {}));
                ch.gain[i] = number (b.getProperty ("gain_db", {}));
                if (i == 1 || i == 2)
                    ch.q[i] = number (b.getProperty ("q", {}));
            }
        const auto lim = c.getProperty ("limiter", {});
        if (lim.isObject())
        {
            ch.limiterGain = number (lim.getProperty ("gain_db", {}));
            ch.ceiling = number (lim.getProperty ("ceiling_dbtp", {}));
            ch.release = number (lim.getProperty ("release_ms", {}));
        }
        return ch;
    }
}

bool ChainChange::touchesEq() const
{
    for (size_t b = 0; b < ChainSettings::numBands; ++b)
        if (freq[b].has_value() || gain[b].has_value() || q[b].has_value())
            return true;
    return false;
}

bool ChainChange::touchesLimiter() const
{
    return limiterGain.has_value() || ceiling.has_value() || release.has_value();
}

juce::Range<float> freqRange (int band)
{
    switch (band)
    {
        case 0: return { 20.0f, 300.0f };
        case 1: return { 60.0f, 1500.0f };
        case 2: return { 400.0f, 12000.0f };
        default: break;
    }
    return { 2000.0f, 20000.0f };
}

juce::String buildPrompt (const PromptInput& in)
{
    juce::String p = in.question;
    p << "\n\n"
      << "At the very end of your answer, add a short block for WWMA (the plugin), as a code block marked json, in exactly this shape:\n"
         "```json\n"
         "{\"suggestions\": [{\"finding_id\": null, \"part\": \"B\", \"at\": \"1:05\", \"title\": \"...\", \"step\": \"...\", \"listen_for\": \"...\"";
    if (in.masterChanges)
        p << ", \"chain\": null";
    p << "}]}\n```\n"
         "Rules for the block: at most 3 suggestions, most important first, the same ones as in your answer. "
         "\"title\": a few words. \"step\": one concrete thing to do now, in one or two sentences. "
         "\"listen_for\": what to listen for to tell whether it helped. \"part\" and \"at\": the part letter and song time from the file, or null. "
         "Write title, step and listen_for in the same language as the rest of your answer.\n";
    if (! in.findings.empty())
    {
        p << "\"finding_id\": when the suggestion is about one of WWMA's own findings, its id from this list, else null:\n";
        for (const auto& [id, title] : in.findings)
            p << "- " << juce::String (id) << ": " << title << "\n";
    }
    else
        p << "\"finding_id\": null (WWMA has no findings right now).\n";

    if (in.masterChanges)
    {
        const auto& c = in.chain;
        p << "\"chain\": only when the fix belongs on the whole master and can be done with WWMA's own mastering EQ or limiter "
             "(the user can then try it with one click and undo it). Otherwise null: a problem in one instrument belongs on its own track, not on the master. Shape:\n"
             "{\"eq\": [{\"band\": 0, \"freq_hz\": 80, \"gain_db\": 1.5}], \"limiter\": {\"gain_db\": 3.0, \"ceiling_dbtp\": -1.0, \"release_ms\": 100}}\n"
             "Give the new absolute values, only for what changes. Keep EQ moves small: at most 3 dB per band from where it is now. The ceiling must stay at -0.3 dBTP or lower.\n"
             "The EQ bands and where they are now (EQ is " << (c.eqOn ? "on" : "off") << "):\n";
        const char* names[] = { "low shelf", "low-mid bell", "high-mid bell", "high shelf" };
        for (int b = 0; b < ChainSettings::numBands; ++b)
        {
            const auto r = freqRange (b);
            p << "- band " << b << ", " << names[b] << " (" << plain (r.getStart(), 0) << "-" << plain (r.getEnd(), 0) << " Hz"
              << ((b == 1 || b == 2) ? ", q 0.3-4" : "") << "): " << plain (c.freq[(size_t) b], 0) << " Hz, "
              << plain (c.gain[(size_t) b], 1) << " dB" << ((b == 1 || b == 2) ? ", q " + plain (c.q[(size_t) b], 2) : juce::String()) << "\n";
        }
        p << "- limiter (" << (c.limiterOn ? "on" : "off") << "): gain " << plain (c.limiterGain, 1) << " dB (-6 to 24), ceiling "
          << plain (c.ceiling, 1) << " dBTP, release " << plain (c.releaseMs, 0) << " ms (10-500)\n";
    }
    else
        p << "Leave \"chain\" out: " << (in.stage == Stage::mixing ? "the song is still being mixed, so fixes belong on the tracks, not on the master."
                                                                     : "master changes are not available now.") << "\n";
    return p;
}

std::vector<Suggestion> parseSuggestions (const juce::String& answer, juce::String* answerText)
{
    std::vector<Suggestion> out;
    if (answerText != nullptr)
        *answerText = answer.trim();
    int start = 0, end = 0;
    juce::String json;
    if (! findBlock (answer, start, end, json))
        return out;
    if (answerText != nullptr)
        *answerText = (answer.substring (0, start) + answer.substring (end)).trim();

    const auto parsed = juce::JSON::parse (json.trim());
    const auto* list = parsed.getProperty ("suggestions", {}).getArray();
    if (list == nullptr)
        return out;
    for (const auto& s : *list)
    {
        if (! s.isObject())
            continue;
        Suggestion g;
        g.findingId = text (s.getProperty ("finding_id", {}), 40).toStdString();
        g.part = text (s.getProperty ("part", {}), 12);
        g.at = text (s.getProperty ("at", {}), 12);
        g.title = text (s.getProperty ("title", {}), 120);
        g.step = text (s.getProperty ("step", {}), 600);
        g.listenFor = text (s.getProperty ("listen_for", {}), 400);
        g.chain = chainFrom (s.getProperty ("chain", {}));
        if (g.title.isEmpty() && g.step.isEmpty())
            continue;
        if (g.title.isEmpty())
            g.title = g.step.upToFirstOccurrenceOf (".", false, false).substring (0, 60);
        out.push_back (std::move (g));
        if (out.size() == 3)
            break;
    }
    return out;
}

ChainSettings applyChange (const ChainSettings& current, const ChainChange& change, const ChainLimits& limits,
                           juce::StringArray* notes, Lang lang)
{
    ChainSettings s = current;
    auto limit = [&] (float wanted, float lo, float hi, const juce::String& what, const juce::String& unit)
    {
        const float v = juce::jlimit (lo, hi, wanted);
        if (notes != nullptr && std::abs (v - wanted) > 0.05f)
            notes->add (what + ": " + tr (lang, "suggested ", "ehdotettiin ") + db (wanted, lang).trimCharactersAtStart ("+") + " " + unit
                        + tr (lang, ", limited to ", ", rajattiin ") + db (v, lang).trimCharactersAtStart ("+") + " " + unit);
        return v;
    };
    for (int b = 0; b < ChainSettings::numBands; ++b)
    {
        const auto i = (size_t) b;
        const auto name = eqBandName (b, lang);
        if (change.freq[i].has_value())
        {
            const auto r = freqRange (b);
            s.freq[i] = juce::jlimit (r.getStart(), r.getEnd(), *change.freq[i]);
        }
        if (change.gain[i].has_value())
        {
            float lo = -12.0f, hi = 12.0f;
            if (limits.maxEqStepDb > 0.0f)
            {
                lo = juce::jmax (lo, current.gain[i] - limits.maxEqStepDb);
                hi = juce::jmin (hi, current.gain[i] + limits.maxEqStepDb);
            }
            s.gain[i] = limit (*change.gain[i], lo, hi, name, "dB");
        }
        if (change.q[i].has_value() && (b == 1 || b == 2))
            s.q[i] = juce::jlimit (0.3f, 4.0f, *change.q[i]);
    }
    if (change.touchesEq())
        s.eqOn = true;

    if (change.limiterGain.has_value())
    {
        float hi = 24.0f;
        if (limits.maxLimiterStepDb > 0.0f)
            hi = juce::jmin (hi, current.limiterGain + limits.maxLimiterStepDb);
        s.limiterGain = limit (*change.limiterGain, -6.0f, hi, tr (lang, "Limiter gain", "Limitterin gain"), "dB");
    }
    if (change.ceiling.has_value())
        s.ceiling = limit (*change.ceiling, -3.0f, limits.maxCeiling, tr (lang, "Ceiling", "Katto"), "dBTP");
    if (change.release.has_value())
        s.releaseMs = juce::jlimit (10.0f, 500.0f, *change.release);
    if (change.touchesLimiter())
    {
        s.limiterOn = true;
        // a limiter turned on keeps its ceiling safe even when only the gain was asked for
        s.ceiling = juce::jmin (s.ceiling, limits.maxCeiling);
    }
    return s;
}

juce::StringArray describeChange (const ChainSettings& before, const ChainSettings& after, Lang lang)
{
    juce::StringArray lines;
    if (after.eqOn && ! before.eqOn)
        lines.add (tr (lang, "EQ on", "EQ päälle"));
    for (int b = 0; b < ChainSettings::numBands; ++b)
    {
        const auto i = (size_t) b;
        const bool f = std::abs (after.freq[i] - before.freq[i]) >= 0.5f;
        const bool g = std::abs (after.gain[i] - before.gain[i]) >= 0.05f;
        const bool q = std::abs (after.q[i] - before.q[i]) >= 0.005f && (b == 1 || b == 2);
        if (! (f || g || q))
            continue;
        juce::String line = eqBandName (b, lang) + " ";
        line << (f ? hz (before.freq[i], lang) + arrow + hz (after.freq[i], lang) : hz (after.freq[i], lang));
        line << ": " << (g ? db (before.gain[i], lang) + arrow + db (after.gain[i], lang) : db (after.gain[i], lang)) << " dB";
        if (q)
            line << ", Q " << juce::String (before.q[i], 2) << arrow << juce::String (after.q[i], 2);
        if (g && b == 0)
            line << (after.gain[i] > before.gain[i] ? tr (lang, " (more bass)", " (lisää bassoa)") : tr (lang, " (less bass)", " (vähemmän bassoa)"));
        if (g && b == 3)
            line << (after.gain[i] > before.gain[i] ? tr (lang, " (more air)", " (lisää ilmavuutta)") : tr (lang, " (softer top)", " (pehmeämpi yläpää)"));
        lines.add (line);
    }
    if (after.limiterOn && ! before.limiterOn)
        lines.add (tr (lang, "Limiter on", "Limitteri päälle"));
    if (std::abs (after.limiterGain - before.limiterGain) >= 0.05f)
        lines.add (tr (lang, "Limiter gain ", "Limitterin gain ") + db (before.limiterGain, lang) + arrow + db (after.limiterGain, lang) + " dB");
    if (std::abs (after.ceiling - before.ceiling) >= 0.05f)
        lines.add (tr (lang, "Ceiling ", "Katto ") + db (before.ceiling, lang) + arrow + db (after.ceiling, lang) + " dBTP");
    if (std::abs (after.releaseMs - before.releaseMs) >= 0.5f)
        lines.add ("Release " + juce::String (juce::roundToInt (before.releaseMs)) + arrow + juce::String (juce::roundToInt (after.releaseMs)) + " ms");
    return lines;
}

ChainChange changeFromAdvice (const ChainAdvice& advice, bool eq, bool limiter)
{
    ChainChange c;
    if (! advice.ready)
        return c;
    if (eq)
        for (size_t b = 0; b < ChainSettings::numBands; ++b)
        {
            if (advice.gain[b].shown) c.gain[b] = advice.gain[b].value;
            if (advice.freq[b].shown) c.freq[b] = advice.freq[b].value;
            if (advice.q[b].shown && (b == 1 || b == 2)) c.q[b] = advice.q[b].value;
        }
    if (limiter)
    {
        if (advice.limiterGain.shown) c.limiterGain = advice.limiterGain.value;
        if (advice.ceiling.shown) c.ceiling = advice.ceiling.value;
        if (advice.release.shown) c.release = advice.release.value;
    }
    return c;
}
} // namespace wwma::ai
