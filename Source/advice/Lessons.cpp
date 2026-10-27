#include "Lessons.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
namespace
{
    const char* ids[] = { "duck", "monobass", "phonebass", "mud", "snap", "monosafe", "headroom", "loudness" };
    constexpr int count = (int) (sizeof (ids) / sizeof (ids[0]));

    juce::String num (Lang lang, float v, int decimals = 1)
    {
        auto s = juce::String (std::abs (v), decimals);
        if (lang == Lang::finnish)
            s = s.replaceCharacter ('.', ',');
        return (v < 0.0f && s.containsAnyOf ("123456789") ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + s;
    }

    float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

    juce::String atLeast (Lang lang) { return tr (lang, "goal: ", "tavoite: ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x89\xa5 ")); }
    juce::String atMost (Lang lang) { return tr (lang, "goal: ", "tavoite: ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x89\xa4 ")); }
} // namespace

int lessonCount() { return count; }
std::string lessonId (int index) { return ids[std::clamp (index, 0, count - 1)]; }

LessonText lessonText (int index, Lang lang)
{
    switch (std::clamp (index, 0, count - 1))
    {
        case 0: return {
            tr (lang, "Duck the bass under the kick", "Duckaa basso kickin alle"),
            tr (lang, "When the bass plays through every kick, the low end is a wall and the groove loses its punch. Ducking makes room for each kick.",
                      "Kun basso soi jokaisen kickin läpi, bassopää on seinä ja groove menettää iskunsa. Duckaus tekee tilaa jokaiselle kickille."),
            tr (lang, "1. Loop a part with kick and bass.\n2. Put a compressor (or a volume shaper) on the bass and feed the kick to its sidechain.\n3. Start with ratio 4:1, attack 0-5 ms, release 100-200 ms and 3-6 dB of gain reduction.\n4. Watch the bar fill up, then listen: does the kick hit harder?",
                      "1. Looppaa kohta, jossa soi kick ja basso.\n2. Laita bassoon kompressori (tai volume shaper) ja syötä kick sen sivuketjuun.\n3. Aloita: suhde 4:1, attack 0–5 ms, release 100–200 ms ja 3–6 dB vaimennus.\n4. Katso palkin täyttyvän ja kuuntele: iskeekö kick kovemmin?"),
            tr (lang, "The kick has its own space now. Listen in the car if you can: the groove should feel tighter.",
                      "Kickillä on nyt oma tilansa. Kuuntele autossa, jos voit: grooven pitäisi tuntua tiiviimmältä.") };
        case 1: return {
            tr (lang, "Make the low end mono", "Tee bassopäästä mono"),
            tr (lang, "Wide bass cancels on phones and mono club systems, and makes vinyl cutting and limiting harder.",
                      "Leveä basso kumoutuu puhelimissa ja monoklubeilla, ja vaikeuttaa limitointia."),
            tr (lang, "1. Find what puts bass on the sides: stereo synths, wideners, chorus, stereo reverb.\n2. Use a bass-mono / mono-maker tool below about 120 Hz on the bass bus or the master.\n3. Keep the width above that, where it sounds big and doesn't cancel.",
                      "1. Etsi, mikä vie bassoa sivuille: stereosynat, leventäjät, chorus, stereokaiku.\n2. Käytä bass mono- / mono-maker-työkalua alle noin 120 Hz bassoväylässä tai masterissa.\n3. Pidä leveys sen yläpuolella, missä se kuulostaa isolta eikä kumoudu."),
            tr (lang, "Mono below 120 Hz: the bass will hit the same everywhere.", "Mono alle 120 Hz: basso iskee samalla tavalla kaikkialla.") };
        case 2: return {
            tr (lang, "Make the bass heard on a phone", "Saa basso kuulumaan puhelimessa"),
            tr (lang, "Phone speakers play almost nothing below 100 Hz. A bass that is only sub disappears; its harmonics are what you hear.",
                      "Puhelimen kaiutin ei toista juuri mitään alle 100 Hz:n. Pelkkä subbasso katoaa; sen yläsävelet ovat se, mitä kuulet."),
            tr (lang, "1. Add gentle saturation or distortion to the bass (keep the sub clean with a parallel or multiband setup).\n2. Or layer the bass with a sound an octave up.\n3. Check by turning your monitors down very quiet: can you still follow the bassline?",
                      "1. Lisää bassoon kevyt saturaatio tai särö (pidä sub puhtaana rinnakkais- tai monikaistakytkennällä).\n2. Tai kerrosta bassoon oktaavia ylempi ääni.\n3. Tarkista laskemalla kaiuttimet hyvin hiljaiselle: pystytkö yhä seuraamaan bassolinjaa?"),
            tr (lang, "The bassline now carries on small speakers.", "Bassolinja kantaa nyt pienistäkin kaiuttimista.") };
        case 3: return {
            tr (lang, "Clear the mud", "Siivoa muta"),
            tr (lang, "200-400 Hz builds up when many full sounds play together. Too much makes the mix boomy and boxy, and hides the kick and the vocal.",
                      "200–400 Hz kertyy, kun monta täyteläistä ääntä soi yhtä aikaa. Liika tekee miksauksesta tunkkaisen ja peittää kickin ja laulun."),
            tr (lang, "1. Solo nothing: listen to the full mix.\n2. On pads, guitars and synths, cut 2-4 dB somewhere in 200-400 Hz (sweep to find the boxiest spot).\n3. High-pass the sounds that don't need low end.",
                      "1. Älä soolaa: kuuntele koko miksiä.\n2. Leikkaa padeista, kitaroista ja synista 2–4 dB jostain kohtaa 200–400 Hz:stä (pyyhkäise löytääksesi tunkkaisimman kohdan).\n3. Ylipäästösuodata äänet, jotka eivät tarvitse bassoa."),
            tr (lang, "Clearer already. Check that the bass and the kick didn't get thin.", "Jo selkeämpi. Tarkista, ettei basso ja kick ohentuneet.") };
        case 4: return {
            tr (lang, "Let the drums through", "Päästä rummut läpi"),
            tr (lang, "Snare and hat hits that barely stand out sound flat on small speakers. Usually a compressor or the limiter is eating the transients.",
                      "Virveli- ja hihat-iskut, jotka tuskin erottuvat, kuulostavat latteilta pienistä kaiuttimista. Yleensä kompressori tai limitteri syö transientit."),
            tr (lang, "1. Slow the attack of your drum bus and mix bus compressors to 10-30 ms.\n2. Push the limiter 1-2 dB less.\n3. Or add attack with a transient shaper on the snare.",
                      "1. Hidasta rumpu- ja masterväylän kompressorien attack 10–30 ms:iin.\n2. Paina limitteriä 1–2 dB vähemmän.\n3. Tai lisää attackia transienttimuokkaimella virveliin."),
            tr (lang, "The hits pop out again. Does it groove more?", "Iskut erottuvat taas. Grooveeko se enemmän?") };
        case 5: return {
            tr (lang, "Make it mono-safe", "Tee miksistä monokelpoinen"),
            tr (lang, "Phones, Bluetooth speakers and club systems often play mono. Parts that live only on the sides get quieter or vanish.",
                      "Puhelimet, Bluetooth-kaiuttimet ja klubit soittavat usein monona. Pelkästään sivuilla elävät osat hiljenevät tai katoavat."),
            tr (lang, "1. Press mono and listen for what drops.\n2. Narrow that sound, or keep a mono copy in the centre and the wide one on top.\n3. Avoid Haas delays and phase tricks on important parts.",
                      "1. Kytke mono ja kuuntele, mikä hiljenee.\n2. Kavenna sitä ääntä, tai pidä monokopio keskellä ja leveä päällä.\n3. Vältä Haas-viiveitä ja vaihetemppuja tärkeissä osissa."),
            tr (lang, "Nothing important drops in mono now.", "Mikään tärkeä ei enää hiljene monona.") };
        case 6: return {
            tr (lang, "Leave headroom for mastering", "Jätä tilaa masteroinnille"),
            tr (lang, "A mix that already hits 0 dB leaves the mastering nothing to work with, and hides balance problems under the limiter.",
                      "Jo nollaan osuva miksaus ei jätä masteroinnille mitään työstettävää ja piilottaa tasapaino-ongelmat limitterin alle."),
            tr (lang, "1. Switch WWMA to MIX.\n2. Bypass the limiter on the master.\n3. Pull the master fader (or all faders) down until the peaks sit between -6 and -3 dBFS.",
                      "1. Vaihda WWMA MIX-tilaan.\n2. Ohita masterin limitteri.\n3. Laske masterfaderia (tai kaikkia fadereita), kunnes huiput ovat −6 ja −3 dBFS:n välillä."),
            tr (lang, "Room to master. Your balance decisions are honest now.", "Tilaa masteroinnille. Tasapainopäätöksesi ovat nyt rehellisiä.") };
        default: return {
            tr (lang, "Reach the style's loudness without squashing", "Saavuta tyylin äänekkyys litistämättä"),
            tr (lang, "Loud is easy; loud and still punchy takes care. Over-limiting kills the kick and the snare.",
                      "Kovaa on helppo tehdä; kovaa ja silti iskevää vaatii huolellisuutta. Ylilimitointi tappaa kickin ja virvelin."),
            tr (lang, "1. Switch WWMA to MASTER.\n2. Raise the limiter input slowly, 1 dB at a time, ceiling at -1 dBTP.\n3. Stop when you hit the style's range, or earlier if the punch (PLR) drops below its range.",
                      "1. Vaihda WWMA MASTER-tilaan.\n2. Nosta limitterin sisääntuloa hitaasti, 1 dB kerrallaan, katto −1 dBTP.\n3. Lopeta, kun osut tyylin alueelle, tai aiemmin, jos iskevyys (PLR) putoaa alueensa alle."),
            tr (lang, "Loud enough, still punchy. Compare against a reference at the same level.", "Tarpeeksi kova ja silti iskevä. Vertaa referenssiin samalla tasolla.") };
    }
}

LessonCheck checkLesson (int index, const Assessment& a, const Profile& p, Stage stage, Lang lang)
{
    LessonCheck c;
    const auto& m = a.measures;
    if (! m.valid)
    {
        c.waitingFor = tr (lang, "Play your track (loop the part you work on).", "Soita kappaletta (looppaa kohtaa, jota työstät).");
        return c;
    }
    c.ready = true;

    switch (std::clamp (index, 0, count - 1))
    {
        case 0:
        {
            // a kick buried under a sustained bass is not always found one by one, but the beat still is
            if (! m.kickValid && ! m.beatSteady)
            {
                c.ready = false;
                c.waitingFor = tr (lang, "Play a part with a steady kick and bass.", "Soita kohtaa, jossa on tasainen kick ja basso.");
                return c;
            }
            const float goal = std::max (p.lowContrastMin + 1.0f, 6.0f);
            // either the kick clearly rises above the bass, or the bass clearly dips after each kick
            // (a bass that dips 6 dB after the kick is clearly ducked; 4 dB is enough when the kick also stands out)
            const float duckGoal = 6.0f;
            const bool kickOk = m.kickValid && m.lowContrast >= goal;
            const bool duckOk = m.kickValid && (m.duck >= duckGoal || (m.duck >= 4.0f && m.lowContrast >= p.lowContrastMin));
            c.progress = kickOk || duckOk ? 1.0f : clamp01 (std::max (1.0f - (goal - m.lowContrast) / 6.0f, m.duck / duckGoal) * 0.95f);
            c.passed = kickOk || duckOk;
            c.now = tr (lang, "Kick over bass ", "Kick yli basson ") + num (lang, m.lowContrast) + " dB, "
                  + tr (lang, "dip ", "notkahdus ") + num (lang, m.duck) + " dB";
            c.goal = atLeast (lang) + num (lang, goal) + tr (lang, " dB or dip ", " dB tai notk. ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x89\xa5 ")) + num (lang, duckGoal) + " dB";
            break;
        }
        case 1:
        {
            const float goal = std::min (p.lowWidthMax - 2.0f, -22.0f);
            c.progress = clamp01 (1.0f - (m.lowWidth - goal) / 14.0f);
            c.passed = m.lowWidth <= goal;
            c.now = tr (lang, "Side below 140 Hz: ", "Sivusignaali alle 140 Hz: ") + num (lang, m.lowWidth) + " dB";
            c.goal = atMost (lang) + num (lang, goal) + " dB";
            break;
        }
        case 2:
        {
            const float goal = -1.0f;
            c.progress = clamp01 (1.0f - (goal - m.phoneBass) / 6.0f);
            c.passed = m.phoneBass >= goal;
            c.now = tr (lang, "Bass harmonics vs sub: ", "Basson yläsävelet suhteessa subiin: ") + num (lang, m.phoneBass) + tr (lang, " dB vs target", " dB tavoitteesta");
            c.goal = atLeast (lang) + num (lang, goal) + " dB";
            break;
        }
        case 3:
        {
            const float d = m.rangeDeviation[(size_t) Range::lowMid], goal = 1.0f;
            c.progress = clamp01 (1.0f - (d - goal) / 5.0f);
            c.passed = d <= goal;
            c.now = tr (lang, "200-450 Hz vs target: ", "200–450 Hz tavoitteesta: ") + (d > 0.0f ? "+" : "") + num (lang, d) + " dB";
            c.goal = atMost (lang) + "+" + num (lang, goal) + " dB";
            break;
        }
        case 4:
        {
            if (! m.contrastValid)
            {
                c.ready = false;
                c.waitingFor = tr (lang, "Play a part with snare and hats.", "Soita kohtaa, jossa on virveli ja hihatit.");
                return c;
            }
            const float goal = p.highContrastMin + 1.0f;
            c.progress = clamp01 (1.0f - (goal - m.highContrast) / 5.0f);
            c.passed = m.highContrast >= goal;
            c.now = tr (lang, "Snare/hat contrast: ", "Virveli-/hihat-kontrasti: ") + num (lang, m.highContrast) + " dB";
            c.goal = atLeast (lang) + num (lang, goal) + " dB";
            break;
        }
        case 5:
        {
            const float goal = 2.5f;
            c.progress = clamp01 (1.0f - (m.monoLoss - goal) / 5.0f) * (m.correlation >= 0.5f ? 1.0f : 0.5f);
            c.passed = m.monoLoss <= goal && m.correlation >= 0.5f;
            c.now = tr (lang, "Loss in mono: ", "Häviö monona: ") + num (lang, m.monoLoss) + tr (lang, " dB, correlation ", " dB, korrelaatio ") + num (lang, m.correlation, 2);
            c.goal = atMost (lang) + num (lang, goal) + tr (lang, " dB, correlation ", " dB, korrelaatio ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x89\xa5 ")) + num (lang, 0.5f, 2);
            break;
        }
        case 6:
        {
            const float tp = m.truePeak;
            const float off = std::max ({ 0.0f, -6.0f - tp, tp + 3.0f });
            c.progress = clamp01 (1.0f - off / 6.0f);
            c.passed = off <= 0.0f;
            c.now = tr (lang, "Peak: ", "Huippu: ") + num (lang, tp) + " dBTP";
            c.goal = tr (lang, "goal: ", "tavoite: ") + num (lang, -6.0f, 0) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) + num (lang, -3.0f, 0) + " dBTP";
            if (stage != Stage::mixing)
                c.goal << tr (lang, " (switch to MIX)", " (vaihda MIX-tilaan)");
            break;
        }
        default:
        {
            const float off = std::max ({ 0.0f, p.lufsMin - m.loudness, m.loudness - p.lufsMax }) + std::max (0.0f, p.plrMin - m.plr);
            c.progress = clamp01 (1.0f - off / 6.0f);
            c.passed = off <= 0.0f;
            c.now = num (lang, m.loudness) + " LUFS, PLR " + num (lang, m.plr) + " LU";
            c.goal = tr (lang, "goal: ", "tavoite: ") + num (lang, p.lufsMin) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) + num (lang, p.lufsMax)
                     + " LUFS, PLR " + juce::String (juce::CharPointer_UTF8 ("\xe2\x89\xa5 ")) + num (lang, p.plrMin) + " LU";
            if (stage != Stage::mastering)
                c.goal << tr (lang, " (switch to MASTER)", " (vaihda MASTER-tilaan)");
            break;
        }
    }
    return c;
}
} // namespace wwma
