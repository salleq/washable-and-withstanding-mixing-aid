#include "ChainAdvice.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
namespace
{
    juce::String num (Lang lang, float v, int decimals = 1, bool sign = false)
    {
        auto s = juce::String (std::abs (v), decimals);
        if (v < 0.0f && s.containsAnyOf ("123456789"))
            s = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + s;
        else if (sign && s.containsAnyOf ("123456789"))
            s = "+" + s;
        return lang == Lang::finnish ? s.replaceCharacter ('.', ',') : s;
    }

    juce::String hzText (Lang lang, float hz)
    {
        if (hz >= 1000.0f)
            return num (lang, hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String dbText (Lang lang, float db) { return num (lang, db, 1, true) + " dB"; }

    float roundTo (float v, float step) { return step * std::round (v / step); }

    float power (float db) { return std::pow (10.0f, db / 10.0f); }

    /** A nice round frequency (the kind printed on EQ knobs). */
    float niceHz (double hz)
    {
        const float steps[] = { 20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 120, 150, 180, 200, 250, 300, 350, 400, 500, 600, 700, 800,
                                1000, 1200, 1500, 1800, 2000, 2500, 3000, 3500, 4000, 5000, 6000, 7000, 8000, 10000, 12000, 15000, 18000 };
        float best = steps[0];
        for (auto s : steps)
            if (std::abs (std::log (s / hz)) < std::abs (std::log (best / hz)))
                best = s;
        return best;
    }

    float tolFor (const Profile& p, Range r)
    {
        return p.tolerance * (r == Range::sub || r == Range::air ? 1.3f : 1.0f);
    }

    /** The band inside a range that is furthest off in the same direction as the range. */
    int worstBand (const Measures& m, const Profile& p, Range r, float rangeDev)
    {
        const auto info = rangeBands (r);
        int best = (info.firstBand + info.lastBand) / 2;
        float bestDev = 0.0f;
        for (int b = info.firstBand; b <= info.lastBand; ++b)
        {
            const float d = (m.spectrum[(size_t) b] - p.target[(size_t) b]) * (rangeDev >= 0.0f ? 1.0f : -1.0f);
            if (d > bestDev)
            {
                bestDev = d;
                best = b;
            }
        }
        return best;
    }

    /** Broad bell for a broad problem, narrower when one spot sticks out of its range. */
    float bellQ (const Measures& m, const Profile& p, float rangeDev, int band)
    {
        const float peak = std::abs (m.spectrum[(size_t) band] - p.target[(size_t) band]);
        return peak - std::abs (rangeDev) > 2.5f ? 2.0f : 0.8f;
    }

    struct BandPlan
    {
        bool act = false;      // worth touching at all
        float dev = 0.0f;      // how far off the range is, dB (+ = too much)
        float tol = 2.5f;
        Range range = Range::bass;
        float hz = 100.0f, q = 0.8f;
    };
}

juce::String eqBandName (int band, Lang lang)
{
    switch (band)
    {
        case ChainSettings::lowShelf:  return tr (lang, "Low shelf", "Alahylly");
        case ChainSettings::lowMid:    return tr (lang, "Low mids", "Alakeski");
        case ChainSettings::highMid:   return tr (lang, "High mids", "Yläkeski");
        case ChainSettings::highShelf: return tr (lang, "High shelf", "Ylähylly");
        default: break;
    }
    return {};
}

float eqLoudnessChange (const Snapshot& s, Window w, const ChainSettings& settings)
{
    if (! settings.eqOn)
        return 0.0f;
    const bool song = w == Window::song;
    const auto& mid = song ? s.midSong : s.midNow;
    const auto& side = song ? s.sideSong : s.sideNow;

    // K-weighted power per band, before and after the EQ
    Biquad shelf, high;
    shelf.setKShelf (48000.0);
    high.setKHighPass (48000.0);
    double before = 0.0, after = 0.0;
    for (int b = 0; b < Bands::count; ++b)
    {
        const double hz = Bands::centre (b);
        const double k = power ((float) (shelf.magnitudeDb (48000.0, hz) + high.magnitudeDb (48000.0, hz)));
        const double p = (power (mid[(size_t) b]) + power (side[(size_t) b])) * k;
        before += p;
        after += p * power ((float) MasterEq::responseDb (settings, hz));
    }
    return before > 0.0 && after > 0.0 ? (float) (10.0 * std::log10 (after / before)) : 0.0f;
}

ChainAdvice adviseChain (const Snapshot& input, const Profile& p, Window w, Lang lang, Stage stage, const ChainSettings& cur)
{
    ChainAdvice out;
    const auto a = assess (input, p, w, lang, stage);
    if (! a.enoughData)
    {
        out.eqNote = out.limiterNote = tr (lang, "Play your track: the suggestions come from what goes into the EQ and limiter.",
                                                 "Soita kappaletta: ehdotukset lasketaan siitä, mitä EQ:hun ja limitteriin tulee sisään.");
        return out;
    }
    out.ready = true;
    const auto& m = a.measures;
    const auto& dev = m.rangeDeviation;
    auto devOf = [&dev] (Range r) { return dev[(size_t) r]; };

    // ================= EQ: which range each band looks after
    std::array<BandPlan, ChainSettings::numBands> plan;
    {
        // low shelf: sub and bass together, or the one that is off
        auto& b = plan[ChainSettings::lowShelf];
        const float s = devOf (Range::sub), ba = devOf (Range::bass);
        const float ts = tolFor (p, Range::sub), tb = tolFor (p, Range::bass);
        if (s * ba > 0.0f && std::abs (s) > 0.5f * ts && std::abs (ba) > 0.5f * tb)
            b = { true, 0.5f * (s + ba), 0.5f * (ts + tb), Range::bass, 90.0f, 0.7f };
        else if (std::abs (s) / ts > std::abs (ba) / tb)
            b = { true, s, ts, Range::sub, 50.0f, 0.7f };
        else
            b = { true, ba, tb, Range::bass, 110.0f, 0.7f };
    }
    {
        auto& b = plan[ChainSettings::lowMid];
        const float d = devOf (Range::lowMid);
        const int band = worstBand (m, p, Range::lowMid, d);
        b = { true, d, tolFor (p, Range::lowMid), Range::lowMid, niceHz (Bands::centre (band)), bellQ (m, p, d, band) };
    }
    {
        auto& b = plan[ChainSettings::highMid];
        const float dm = devOf (Range::mid), dp = devOf (Range::presence);
        const Range r = std::abs (dm) / tolFor (p, Range::mid) > std::abs (dp) / tolFor (p, Range::presence) ? Range::mid : Range::presence;
        const float d = devOf (r);
        const int band = worstBand (m, p, r, d);
        b = { true, d, tolFor (p, r), r, juce::jlimit (400.0f, 12000.0f, niceHz (Bands::centre (band))), bellQ (m, p, d, band) };
    }
    {
        auto& b = plan[ChainSettings::highShelf];
        const float d = devOf (Range::air);
        b = { true, d, tolFor (p, Range::air), Range::air, d * devOf (Range::presence) > 0.0f ? 8000.0f : 10000.0f, 0.7f };
    }

    int worst = -1;
    float worstScore = 0.0f;
    for (int i = 0; i < ChainSettings::numBands; ++i)
    {
        const auto& b = plan[(size_t) i];
        const auto idx = (size_t) i;
        const bool small = std::abs (b.dev) < 0.5f * b.tol;
        // a mastering EQ nudges: about 80 % of the gap, never more than 4 dB
        const float g = small ? 0.0f : juce::jlimit (-4.0f, 4.0f, roundTo (-0.8f * b.dev, 0.5f));
        const auto where = rangeName (b.range, lang) + " (" + rangeNickname (b.range, lang).toLowerCase() + ")";

        auto& gs = out.gain[idx];
        gs.shown = true;
        gs.value = g;
        gs.close = std::abs (cur.gain[idx] - g) <= 0.5f;
        gs.text = dbText (lang, g);
        if (small)
            gs.why = where + tr (lang, " is within the style's range before the EQ (", " on tyylin rajoissa ennen EQ:ta (")
                     + dbText (lang, b.dev) + tr (lang, "). Leave this band flat.", "). Jätä tämä kaista nollaan.");
        else
            gs.why = where + tr (lang, " is ", " on ") + dbText (lang, b.dev)
                     + tr (lang, " off the style's curve before the EQ. About ", " sivussa tyylin käyrästä ennen EQ:ta. Noin ")
                     + dbText (lang, g) + tr (lang, " here brings it closer. Turn it slowly and listen: if it sounds worse, trust your ears.",
                                                " tässä tuo sen lähemmäs. Käännä hitaasti ja kuuntele: jos se kuulostaa huonommalta, luota korviisi.");

        auto& fs = out.freq[idx];
        fs.shown = ! small;
        fs.value = b.hz;
        fs.close = std::abs (std::log2 (cur.freq[idx] / b.hz)) <= 1.0f / 3.0f;
        fs.text = hzText (lang, b.hz);
        fs.why = i == ChainSettings::lowShelf || i == ChainSettings::highShelf
                     ? tr (lang, "Where the shelf starts, so it covers ", "Mistä hylly alkaa, jotta se kattaa alueen ") + where + "."
                     : tr (lang, "The spot in ", "Kohta, jossa ") + where + tr (lang, " that is furthest off.", " on eniten sivussa.");

        if (i == ChainSettings::lowMid || i == ChainSettings::highMid)
        {
            auto& qs = out.q[idx];
            qs.shown = ! small;
            qs.value = b.q;
            qs.close = std::abs (std::log2 (cur.q[idx] / b.q)) <= 0.5f;
            qs.text = num (lang, b.q, 1);
            qs.why = b.q > 1.0f ? tr (lang, "One spot sticks out, so a narrower bell.", "Yksi kohta erottuu, joten kapeampi kello.")
                                : tr (lang, "A broad bell: master EQ moves are wide and gentle.", "Leveä kello: masterin EQ-liikkeet ovat leveitä ja loivia.");
        }

        if (std::abs (b.dev) > 2.0f * b.tol || std::abs (b.dev) > 5.0f)
            out.eqTooMuch = true;
        const float score = std::abs (b.dev) / b.tol;
        if (! small && score > worstScore)
        {
            worstScore = score;
            worst = i;
        }
    }

    if (worst < 0)
        out.eqNote = tr (lang, "The tonal balance going in is within the style's range: the EQ can stay flat (or off).",
                               "Sisään tuleva taajuustasapaino on tyylin rajoissa: EQ voi olla nollassa (tai pois päältä).");
    else
    {
        const auto& b = plan[(size_t) worst];
        out.eqNote = rangeName (b.range, lang) + " " + dbText (lang, b.dev) + tr (lang, " vs the style: try ", " tyyliin nähden: kokeile ")
                     + eqBandName (worst, lang).toLowerCase() + " " + out.gain[(size_t) worst].text
                     + tr (lang, " around ", " noin ") + out.freq[(size_t) worst].text + ".";
        if (out.eqTooMuch)
            out.eqNote << tr (lang, " Something is far off: it is better fixed in the mix than on the master.",
                                    " Jokin on kaukana: se kannattaa korjata miksissä eikä masterissa.");
    }

    // ================= limiter
    const float ceiling = juce::jlimit (-3.0f, 0.0f, p.truePeakMax);
    {
        auto& c = out.ceiling;
        c.shown = true;
        c.value = ceiling;
        c.close = std::abs (cur.ceiling - ceiling) <= 0.15f;
        c.text = num (lang, ceiling, 1) + " dBTP";
        c.why = tr (lang, "The ceiling for this style and target. Below 0 dBTP leaves room for MP3, AAC and Bluetooth encoding, which add overshoots.",
                          "Katto tälle tyylille ja kohteelle. Alle 0 dBTP jättää tilaa MP3-, AAC- ja Bluetooth-pakkaukselle, jotka lisäävät ylityksiä.");
    }
    {
        auto& r = out.release;
        const bool tempo = input.tempoConfidence >= 0.5f && input.tempoBpm > 50.0f;
        const float ms = tempo ? juce::jlimit (30.0f, 300.0f, roundTo (15000.0f / input.tempoBpm, 5.0f)) : 100.0f;
        r.shown = true;
        r.value = ms;
        r.close = std::abs (std::log2 (cur.releaseMs / ms)) <= 0.35f;
        r.text = juce::String (juce::roundToInt (ms)) + " ms";
        r.why = tempo ? tr (lang, "About a 16th note at ", "Noin 16-osanuotti tempossa ") + juce::String (juce::roundToInt (input.tempoBpm))
                            + tr (lang, " BPM: the limiter lets go between hits, in time with the music, so it doesn't pump against the beat.",
                                    " BPM: limitteri päästää irti iskujen välissä musiikin tahdissa, jottei se pumppaa biittiä vastaan.")
                      : tr (lang, "A safe middle value. Shorter is louder but can distort the bass; longer is cleaner but can pump.",
                                  "Turvallinen keskiarvo. Lyhyempi on kovempi mutta voi säröttää bassoa; pidempi on puhtaampi mutta voi pumpata.");
    }

    auto& gs = out.limiterGain;
    gs.shown = true;
    if (stage == Stage::mixing)
    {
        gs.value = 0.0f;
        gs.close = ! cur.limiterOn || std::abs (cur.limiterGain) <= 0.5f;
        gs.text = dbText (lang, 0.0f);
        gs.why = tr (lang, "While mixing, leave the limiter off: it hides balance problems. Switch to MASTER when the mix is ready.",
                           "Miksatessa pidä limitteri pois päältä: se piilottaa tasapaino-ongelmia. Vaihda MASTER-tilaan, kun miksi on valmis.");
        out.limiterNote = gs.why;
        return out;
    }

    // loudness going into the limiter: the input plus what the EQ adds
    const float eqDelta = eqLoudnessChange (input, w, cur);
    const float pre = m.loudness + eqDelta;
    const float peak = m.truePeak + eqDelta;
    const float target = 0.5f * (p.lufsMin + p.lufsMax);
    // Limiting the peaks also takes some loudness away, more the harder it works (calibrated on
    // the test songs: about a tenth of the peak reduction at 4 dB, about 40 % at 11 dB).
    auto lossAt = [] (float reduction) { return reduction * juce::jlimit (0.05f, 0.45f, 0.047f * reduction - 0.12f); };
    auto outFor = [&] (float gain) { return pre + gain - lossAt (juce::jmax (0.0f, peak + gain - ceiling)); };
    float lo = -6.0f, hi = 24.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (outFor (mid) < target ? lo : hi) = mid;
    }
    const float g = juce::jlimit (-6.0f, 24.0f, roundTo (0.5f * (lo + hi), 0.5f));
    out.expectedReduction = juce::jmax (0.0f, peak + g - ceiling);

    gs.value = g;
    gs.close = std::abs (cur.limiterGain - g) <= 0.5f;
    gs.text = dbText (lang, g);
    const auto targetText = num (lang, p.lufsMin) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) + num (lang, p.lufsMax) + " LUFS";
    gs.why = tr (lang, "Going in: ", "Sisään: ") + num (lang, pre) + " LUFS. "
             + tr (lang, "Target: ", "Tavoite: ") + targetText + ". "
             + tr (lang, "This gain should land near ", "Tällä vahvistuksella pitäisi päätyä noin ") + num (lang, target)
             + tr (lang, " LUFS with about ", " LUFS:iin ja noin ") + num (lang, out.expectedReduction)
             + tr (lang, " dB of peak limiting. Check the output meter and fine-tune from there.",
                         " dB:n huippujen limitointiin. Katso lähtömittaria ja hienosäädä siitä.");

    if (g < 0.0f)
        out.limiterNote = tr (lang, "The mix is already louder than the target: turn the mix (or this gain) down rather than limiting harder.",
                                    "Miksi on jo tavoitetta kovempi: laske miksiä (tai tätä vahvistusta) mieluummin kuin limitoi kovempaa.");
    else if (out.expectedReduction > 8.0f)
        out.limiterNote = tr (lang, "That needs a lot of limiting (", "Tämä vaatii paljon limitointia (") + juce::String (juce::roundToInt (out.expectedReduction))
                          + tr (lang, " dB). Let the limiter do 3-6 dB and get the rest earlier: a bus compressor or a clipper on the drums, or a louder mix.",
                                      " dB). Anna limitterin tehdä 3–6 dB ja hae loput aiemmin: väyläkompressori tai klipperi rumpuihin, tai kovempi miksi.");
    else
        out.limiterNote = tr (lang, "Aim for ", "Tähtää ") + targetText + tr (lang, ", ceiling ", ", katto ") + out.ceiling.text
                          + tr (lang, ". Compare with the level-matched bypass: louder always sounds better for a moment.",
                                      ". Vertaa tasoitetulla ohituksella: kovempi kuulostaa aina hetken paremmalta.");
    return out;
}
} // namespace wwma
