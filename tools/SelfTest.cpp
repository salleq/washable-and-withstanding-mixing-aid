// WWMA self test: measurement accuracy, advice scenarios, stability, and UI screenshots.
// Usage: wwma_selftest [screenshot-folder]
// Exits with 1 if any check fails.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <complex>
#include <functional>
#include <iostream>

using namespace wwma;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what, const juce::String& detail = {})
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << std::endl;
        if (! ok)
            ++failures;
    }

    bool near (double v, double target, double tol) { return std::abs (v - target) <= tol; }

    struct Stereo { std::vector<float> l, r; };

    Snapshot analyse (const Stereo& s, double sr, int block = 512)
    {
        Analyzer a;
        a.prepare (sr);
        const int n = (int) s.l.size();
        for (int i = 0; i < n; i += block)
            a.process (s.l.data() + i, s.r.data() + i, juce::jmin (block, n - i));
        Snapshot snap;
        a.readSnapshot (snap);
        return snap;
    }

    Stereo sine (double sr, double seconds, double hz, double dbfs, double phase = 0.0)
    {
        const int n = (int) (sr * seconds);
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        const double a = std::pow (10.0, dbfs / 20.0);
        for (int i = 0; i < n; ++i)
            s.l[(size_t) i] = s.r[(size_t) i] = (float) (a * std::sin (2.0 * pi * hz * i / sr + phase));
        return s;
    }

    void append (Stereo& a, const Stereo& b)
    {
        a.l.insert (a.l.end(), b.l.begin(), b.l.end());
        a.r.insert (a.r.end(), b.r.begin(), b.r.end());
    }

    /** Noise whose third-octave spectrum follows the given curve (dB per band). */
    std::vector<float> shapedNoise (double sr, int order, const std::function<double (double)>& levelDb, juce::Random& rng)
    {
        const int n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<juce::dsp::Complex<float>> spec ((size_t) n), out ((size_t) n);
        for (int k = 1; k < n / 2; ++k)
        {
            const double f = k * sr / n;
            if (f < 16.0 || f > 19000.0)
                continue;
            // a third-octave band holds more bins as frequency rises: power per bin ~ level / f
            const double amp = std::sqrt (std::pow (10.0, levelDb (f) / 10.0) / f);
            const double ph = rng.nextDouble() * 2.0 * pi;
            spec[(size_t) k] = { (float) (amp * std::cos (ph)), (float) (amp * std::sin (ph)) };
            spec[(size_t) (n - k)] = std::conj (spec[(size_t) k]);
        }
        fft.perform (spec.data(), out.data(), true);
        std::vector<float> x ((size_t) n);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            x[(size_t) i] = out[(size_t) i].real();
            sum += (double) x[(size_t) i] * x[(size_t) i];
        }
        const float norm = (float) (1.0 / std::sqrt (sum / n + 1.0e-30));
        for (auto& v : x)
            v *= norm;
        return x;
    }

    /** Target curve of a profile interpolated at any frequency. */
    double targetAt (const Profile& p, double f)
    {
        const double pos = 3.0 * std::log2 (f / 1000.0) - Bands::firstK;
        const int b = juce::jlimit (0, Bands::count - 2, (int) std::floor (pos));
        const double t = juce::jlimit (0.0, 1.0, pos - b);
        return p.target[(size_t) b] * (1.0 - t) + p.target[(size_t) b + 1] * t;
    }

    /** A "mix" made of noise that follows the profile, with optional changes. */
    Stereo profileMix (const Profile& p, double sr, const std::function<double (double)>& tweakDb,
                       double sideDb, const std::function<double (double)>& sideTweakDb, double rmsDb, int seed)
    {
        juce::Random rng (seed);
        const int order = 19; // ~11 s at 48 kHz
        auto mid = shapedNoise (sr, order, [&] (double f) { return targetAt (p, f) + tweakDb (f); }, rng);
        auto side = shapedNoise (sr, order, [&] (double f) { return targetAt (p, f) + sideTweakDb (f); }, rng);
        const double gm = std::pow (10.0, rmsDb / 20.0), gs = gm * std::pow (10.0, sideDb / 20.0);
        Stereo s { std::vector<float> (mid.size()), std::vector<float> (mid.size()) };
        for (size_t i = 0; i < mid.size(); ++i)
        {
            s.l[i] = (float) (gm * mid[i] + gs * side[i]);
            s.r[i] = (float) (gm * mid[i] - gs * side[i]);
        }
        return s;
    }

    bool has (const Assessment& a, const std::string& id, Severity atLeast = Severity::watch)
    {
        for (const auto& f : a.findings)
            if (f.id == id && (int) f.severity >= (int) atLeast)
                return true;
        return false;
    }

    juce::String ids (const Assessment& a)
    {
        juce::StringArray s;
        for (const auto& f : a.findings)
            s.add (juce::String (f.id) + (f.severity == Severity::fix ? "!" : ""));
        return s.joinIntoString (" ");
    }

    /** Kick every half second plus a sustained bass at the given level (dB). */
    Stereo kickAndBass (double sr, double seconds, double bassDb)
    {
        const int n = (int) (sr * seconds);
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double tb = std::fmod (t, 0.5);
            phase += 2.0 * pi * (48.0 + 110.0 * std::exp (-tb * 35.0)) / sr;
            if (tb < 1.0 / sr) phase = 0.0;
            const double kick = 0.9 * std::exp (-tb * 14.0) * std::sin (phase);
            const double bass = 0.3 * std::pow (10.0, bassDb / 20.0) * (std::sin (2.0 * pi * 65.0 * t) + 0.3 * std::sin (2.0 * pi * 130.0 * t));
            const double hat = 0.02 * std::sin (2.0 * pi * 3000.0 * t) * (std::fmod (t, 0.25) < 0.02 ? 1.0 : 0.3);
            s.l[(size_t) i] = s.r[(size_t) i] = (float) (kick + bass + hat);
        }
        return s;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    // ------------------------------------------------------------------ loudness
    std::cout << "Loudness (BS.1770-4 / EBU R128)" << std::endl;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto s = analyse (sine (sr, 20.0, 997.0, -20.0), sr);
        const auto tag = juce::String ((int) sr) + " Hz";
        check (near (s.integrated, -20.0, 0.1), "997 Hz sine at -20 dBFS reads -20 LUFS integrated @ " + tag, juce::String (s.integrated, 2));
        check (near (s.shortTerm, -20.0, 0.1), "  short-term", juce::String (s.shortTerm, 2));
        check (near (s.momentary, -20.0, 0.1), "  momentary", juce::String (s.momentary, 2));
    }
    {
        auto x = sine (48000.0, 10.0, 997.0, -20.0);
        append (x, sine (48000.0, 10.0, 997.0, -36.0));
        const auto s = analyse (x, 48000.0);
        check (near (s.integrated, -20.0, 0.15), "relative gate drops the quiet half", juce::String (s.integrated, 2));
    }
    {
        auto x = sine (48000.0, 20.0, 997.0, -20.0);
        append (x, sine (48000.0, 20.0, 997.0, -30.0));
        const auto s = analyse (x, 48000.0);
        check (near (s.loudnessRange, 10.0, 1.0), "loudness range of a -20 / -30 dB pair is 10 LU", juce::String (s.loudnessRange, 2));
    }
    {
        // a mono signal on both channels of a stereo pair: +3 dB versus one channel
        auto x = sine (48000.0, 10.0, 997.0, -23.0);
        std::fill (x.r.begin(), x.r.end(), 0.0f);
        const auto s = analyse (x, 48000.0);
        check (near (s.integrated, -26.01, 0.15), "one channel only reads 3 dB lower", juce::String (s.integrated, 2));
    }

    // ------------------------------------------------------------------ true peak
    std::cout << "True peak" << std::endl;
    {
        const auto s = analyse (sine (48000.0, 2.0, 12000.0, 0.0, pi / 4.0), 48000.0);
        check (near (s.truePeakMax, 0.0, 0.3), "fs/4 sine sampled 45 deg off its peaks reads 0 dBTP (samples are at -3 dB)", juce::String (s.truePeakMax, 2));
    }
    {
        const auto s = analyse (sine (48000.0, 2.0, 1000.0, -6.02), 48000.0);
        check (near (s.truePeakMax, -6.02, 0.1), "1 kHz sine at -6 dBFS reads -6 dBTP", juce::String (s.truePeakMax, 2));
    }

    // ------------------------------------------------------------------ never touches the audio
    std::cout << "Pass-through" << std::endl;
    {
        WwmaAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::Random rng (1);
        juce::AudioBuffer<float> buf (2, 512), copy (2, 512);
        juce::MidiBuffer midi;
        bool identical = true;
        for (int k = 0; k < 200; ++k)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (c, i, rng.nextFloat() * 2.0f - 1.0f);
            copy.makeCopyOf (buf);
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                identical = identical && std::memcmp (buf.getReadPointer (c), copy.getReadPointer (c), 512 * sizeof (float)) == 0;
        }
        check (identical, "output is bit-identical to the input");
    }

    // ------------------------------------------------------------------ stability
    std::cout << "Stability" << std::endl;
    {
        bool finite = true;
        juce::Random rng (7);
        for (double sr : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            Analyzer a;
            a.prepare (sr);
            std::vector<float> l (8192), r (8192);
            for (int k = 0; k < 120; ++k)
            {
                const int n = 1 + rng.nextInt (4096);
                const float g = k % 10 == 0 ? 0.0f : (k % 7 == 0 ? 8.0f : 0.5f);
                for (int i = 0; i < n; ++i) { l[(size_t) i] = g * (rng.nextFloat() - 0.5f); r[(size_t) i] = g * (rng.nextFloat() - 0.5f); }
                a.process (l.data(), r.data(), n);
                if (k == 60) a.requestReset();
            }
            Snapshot s;
            a.readSnapshot (s);
            for (auto v : { s.momentary, s.shortTerm, s.integrated, s.truePeakMax, s.correlationNow, s.lowContrastNow })
                finite = finite && std::isfinite (v);
            for (auto v : s.midNow) finite = finite && std::isfinite (v);
            const auto assessment = assess (s, builtInProfiles()[0], Window::now, Lang::finnish);
            finite = finite && std::isfinite (assessment.score);
        }
        check (finite, "random blocks at 22-192 kHz stay finite, with silence, overloads and a reset");
    }

    // ------------------------------------------------------------------ advice
    std::cout << "Advice scenarios (House profile, 48 kHz)" << std::endl;
    const auto& house = builtInProfiles()[0];
    const double sr = 48000.0;
    auto none = [] (double) { return 0.0; };
    auto run = [&] (const Stereo& s) { return assess (analyse (s, sr), house, Window::song, Lang::english); };

    const auto monoBass = [] (double f) { return f < 140.0 ? -20.0 : 0.0; };
    const auto balanced = profileMix (house, sr, none, -14.0, monoBass, -14.0, 1);
    {
        const auto a = run (balanced);
        bool tonal = false;
        for (int r = 0; r < numRanges; ++r)
            tonal = tonal || has (a, "range" + std::to_string (r) + "+") || has (a, "range" + std::to_string (r) + "-");
        juce::String dev;
        for (auto d : a.measures.rangeDeviation) dev << juce::String (d, 1) << " ";
        check (a.enoughData && ! tonal, "noise shaped like the target: no tonal complaints", "deviations " + dev.trim());
        check (! has (a, "phonebass") && ! has (a, "lowwidth") && ! has (a, "phase"), "  no phone-bass, width or phase complaints", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f < 56.0 ? 9.0 : 0.0; }, -14.0, none, -14.0, 2));
        check (has (a, "range0+", Severity::fix), "+9 dB sub: 'too much sub' (clearly)", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f > 445.0 && f < 2245.0 ? -9.0 : 0.0; }, -14.0, none, -14.0, 3));
        check (has (a, "range3-", Severity::fix), "-9 dB mids: 'scooped mids' (clearly)", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f > 2245.0 && f < 5600.0 ? 8.0 : 0.0; }, -14.0, none, -14.0, 4));
        check (has (a, "range4+", Severity::fix), "+8 dB presence: 'harsh upper mids'", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f > 90.0 && f < 300.0 ? -10.0 : 0.0; }, -14.0, none, -14.0, 5));
        check (has (a, "phonebass"), "bass without harmonics: 'bass will disappear on phones'", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, none, 0.0, [] (double f) { return f < 140.0 ? 0.0 : -20.0; }, -14.0, 6));
        check (has (a, "lowwidth", Severity::fix), "stereo bass: 'low end too wide'", ids (a));
    }
    {
        auto x = balanced;
        for (auto& v : x.r) v = -v;
        const auto a = run (x);
        check (has (a, "phase", Severity::fix), "right channel inverted: 'phase problems' (clearly)", ids (a));
    }
    {
        auto x = balanced;
        for (size_t i = 0; i < x.l.size(); ++i) { x.l[i] *= 6.0f; x.r[i] *= 6.0f; }
        const auto a = run (x);
        check (has (a, "loud") && has (a, "peak", Severity::fix), "+15 dB hotter: 'louder than needed' and 'peaks'", ids (a));
    }
    // ---- common pitfalls
    {
        auto x = balanced;
        for (auto& v : x.r) v *= 0.6f;
        const auto a = run (x);
        check (has (a, "balance"), "right channel 4.4 dB down: 'mix leans to the left'", ids (a));
        check (! has (run (balanced), "balance"), "  a centred mix does not lean");
    }
    {
        auto x = balanced;
        for (auto* ch : { &x.l, &x.r }) for (auto& v : *ch) v += 0.01f;
        check (has (run (x), "dcoffset"), "1 % DC: 'DC offset'", ids (run (x)));
    }
    {
        auto x = balanced;
        for (auto* ch : { &x.l, &x.r }) for (auto& v : *ch) v = juce::jlimit (-1.0f, 1.0f, v * 4.0f);
        const auto a = run (x);
        check (has (a, "clipping", Severity::fix), "hard clipped: 'clipped, flat-topped peaks'", ids (a));
        check (! has (run (balanced), "clipping"), "  no clipping on a clean mix");
    }
    {
        auto x = balanced;
        for (auto* ch : { &x.l, &x.r }) for (auto& v : *ch) v *= 2.5f;
        const auto mix = assess (analyse (x, sr), house, Window::song, Lang::english, Stage::mixing);
        check (has (mix, "headroom") && ! has (mix, "peak") && ! has (mix, "loud"), "mixing stage: 'no headroom left', loudness and ceiling not judged", ids (mix));
        const auto clean = assess (analyse (balanced, sr), house, Window::song, Lang::english, Stage::mixing);
        check (! has (clean, "headroom") && ! has (clean, "hotmix"), "  a mix peaking well below 0 dB is fine", ids (clean));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f > 5800.0 && f < 7000.0 ? 9.0 : 0.0; }, -14.0, monoBass, -14.0, 3));
        check (has (a, "sibilance"), "+9 dB at 6.3 kHz: 'sharp s sounds'", ids (a));
        const auto pres = run (profileMix (house, sr, [] (double f) { return f > 2245.0 && f < 5600.0 ? 8.0 : 0.0; }, -14.0, none, -14.0, 7));
        check (! has (pres, "sibilance"), "  a broad presence lift is not called sibilance", ids (pres));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f < 28.0 ? 15.0 : 0.0; }, -14.0, monoBass, -14.0, 4));
        check (has (a, "rumble"), "+15 dB below 28 Hz: 'rumble'", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, none, -14.0, [] (double f) { return (f < 140.0 ? -20.0 : 0.0) + (f > 4500.0 ? 16.0 : 0.0); }, -14.0, 5));
        check (has (a, "sidehigh"), "highs on the sides only: 'top end mostly on the sides'", ids (a));
    }
    {
        const auto a = run (profileMix (house, sr, [] (double f) { return f > 900.0 && f < 1120.0 ? -14.0 : 0.0; }, -14.0, monoBass, -14.0, 6));
        check (has (a, "notch"), "-14 dB at 1 kHz: 'hole in the spectrum'", ids (a));
    }
    {
        // the tip rotates and is never counted against the mix
        const auto a = run (balanced);
        bool tipOk = false;
        for (const auto& f : a.findings)
            tipOk = tipOk || (f.id == "tip" && f.severity == Severity::info && f.penalty == 0.0f && f.why.isNotEmpty());
        check (tipOk, "a listening tip is shown, at no cost to the score");
    }

    {
        // the score must go up when a problem is fixed
        const auto bad = run (profileMix (house, sr, [] (double f) { return f < 56.0 ? 9.0 : 0.0; }, -14.0, none, -14.0, 2));
        const auto better = run (profileMix (house, sr, [] (double f) { return f < 56.0 ? 4.0 : 0.0; }, -14.0, none, -14.0, 2));
        const auto good = run (balanced);
        check (bad.score < better.score && better.score <= good.score + 0.01f, "score rises as the sub gets fixed",
               juce::String (bad.score, 0) + " < " + juce::String (better.score, 0) + " <= " + juce::String (good.score, 0));
        std::cout << "        verdicts: " << verdictFor (bad.score, Lang::english) << " / " << verdictFor (better.score, Lang::english)
                  << " / " << verdictFor (good.score, Lang::english) << std::endl;
    }
    {
        const auto loudBass = analyse (kickAndBass (sr, 12.0, 6.0), sr);
        const auto plain = analyse (kickAndBass (sr, 12.0, 0.0), sr);
        const auto lowered = analyse (kickAndBass (sr, 12.0, -6.0), sr);
        check (lowered.lowContrastSong > plain.lowContrastSong + 2.0f && plain.lowContrastSong > loudBass.lowContrastSong + 2.0f,
               "kick-vs-bass separation follows the bass level",
               juce::String (loudBass.lowContrastSong, 1) + " / " + juce::String (plain.lowContrastSong, 1) + " / " + juce::String (lowered.lowContrastSong, 1) + " dB");
        const auto a = assess (loudBass, house, Window::song, Lang::english);
        const auto b = assess (lowered, house, Window::song, Lang::english);
        check (has (a, "kickbass") && ! has (b, "kickbass"), "'kick drowns in the bass' with a loud bass, not with a quieter one", ids (a) + " | " + ids (b));
    }
    {
        // every built-in profile produces advice in both languages without empty texts
        bool ok = true;
        const auto s = analyse (balanced, sr);
        for (const auto& p : builtInProfiles())
            for (auto lang : { Lang::english, Lang::finnish })
                for (auto w : { Window::now, Window::song })
                    for (const auto& f : assess (s, p, w, lang).findings)
                        ok = ok && f.title.isNotEmpty() && f.why.isNotEmpty();
        check (ok, "all " + juce::String ((int) builtInProfiles().size()) + " profiles give complete advice in English and Finnish");
    }
    {
        // learning a reference from a mix makes that same mix fit the reference
        const auto s = analyse (profileMix (house, sr, [] (double f) { return f > 2245.0 ? 6.0 : 0.0; }, -10.0, none, -20.0, 9), sr);
        const auto ref = profileFromSnapshot (s, "ref");
        const auto a = assess (s, ref, Window::song, Lang::english);
        check (a.score >= 90.0f, "a learned reference matches its own source", juce::String (a.score, 0) + " " + ids (a));
    }

    // ------------------------------------------------------------------ reference tracks
    const auto refDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wwma-selftest");
    refDir.createDirectory();
    auto writeWav = [&] (const Stereo& x, double rate, const juce::String& name)
    {
        auto file = refDir.getChildFile (name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        juce::AudioBuffer<float> b (2, (int) x.l.size());
        b.copyFrom (0, 0, x.l.data(), (int) x.l.size());
        b.copyFrom (1, 0, x.r.data(), (int) x.r.size());
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    };
    // a louder, brighter "commercial" reference at 44.1 kHz, played in a 48 kHz session
    const auto refAudio = profileMix (house, 44100.0, [] (double f) { return f > 5600.0 ? 2.0 : 0.0; }, -14.0, monoBass, -15.0, 21);
    const auto refFile = writeWav (refAudio, 44100.0, "reference.wav");
    {
        ReferenceTracks refs;
        refs.setPlaybackRate (48000.0);
        const bool ok = refs.loadNow (0, refFile);
        check (ok && refs.getStatus (0) == ReferenceTracks::Status::ready, "a WAV reference track loads and is analysed");
        if (auto info = refs.getInfo (0))
        {
            const auto direct = analyse (refAudio, 44100.0);
            check (near (info->snap.integrated, direct.integrated, 0.1), "the reference is measured like a live mix",
                   juce::String (info->snap.integrated, 2) + " vs " + juce::String (direct.integrated, 2));
            check (near (info->seconds, (double) refAudio.l.size() / 44100.0, 0.01) && info->wave.size() == (size_t) ReferenceTracks::waveColumns,
                   "length and waveform overview");
            const auto a = assess (analyse (refAudio, 44100.0), profileFromSnapshot (info->snap, "ref"), Window::song, Lang::english);
            check (a.score >= 90.0f, "the reference profile fits the reference itself", juce::String (a.score, 0) + " " + ids (a));
        }
        const bool bad = refs.loadNow (1, refDir.getChildFile ("missing.wav"));
        check (! bad && refs.getStatus (1) == ReferenceTracks::Status::failed && refs.getActive() == 0, "a missing file fails cleanly and keeps the active slot");
    }
    {
        // A/B: the reference plays loudness-matched, and the mix comes back untouched afterwards
        WwmaAudioProcessor p;
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        p.refs.loadNow (0, refFile);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, 512), copy (2, 512);
        Stereo heard, quietMix = balanced;
        for (auto* ch : { &quietMix.l, &quietMix.r })
            for (auto& v : *ch)
                v *= 0.4f; // the mix is 8 dB quieter than the reference
        const auto mix = analyse (quietMix, sr);
        const auto refSnap = p.refs.getInfo (0)->snap;
        p.referenceGainDb.store (mix.integrated - refSnap.integrated);
        p.listenToReference.store (true);
        size_t i = 0;
        for (; i + 512 <= quietMix.l.size(); i += 512)
        {
            buf.copyFrom (0, 0, quietMix.l.data() + i, 512);
            buf.copyFrom (1, 0, quietMix.r.data() + i, 512);
            p.processBlock (buf, midi);
            heard.l.insert (heard.l.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            heard.r.insert (heard.r.end(), buf.getReadPointer (1), buf.getReadPointer (1) + 512);
        }
        const auto h = analyse (heard, sr);
        check (near (h.integrated, mix.integrated, 0.5), "B plays the reference as loud as the mix",
               "mix " + juce::String (mix.integrated, 1) + ", ref " + juce::String (refSnap.integrated, 1) + ", heard " + juce::String (h.integrated, 1));
        Snapshot measured;
        p.analyzer.readSnapshot (measured);
        check (near (measured.integrated, mix.integrated, 0.05), "the analysis keeps measuring the mix while B plays");

        p.listenToReference.store (false);
        bool identical = true;
        for (int k = 0; k < 40; ++k)
        {
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, (c == 0 ? quietMix.l : quietMix.r).data() + (size_t) k * 512, 512);
            copy.makeCopyOf (buf);
            p.processBlock (buf, midi);
            if (k >= 4) // after the 30 ms fade
                for (int c = 0; c < 2; ++c)
                    identical = identical && std::memcmp (buf.getReadPointer (c), copy.getReadPointer (c), 512 * sizeof (float)) == 0;
        }
        check (identical, "back on A the output is bit-identical again");
    }

    // ------------------------------------------------------------------ screenshots
    if (argc > 1)
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
        dir.createDirectory();
        std::cout << "Screenshots -> " << dir.getFullPathName() << std::endl;

        struct Shot { const char* name; Stereo audio; Lang lang; bool baseline; bool withRef = false; bool listen = false; };
        const auto refA = writeWav (refAudio, 44100.0, "Favourite club track.wav");
        const auto refB = writeWav (profileMix (house, 48000.0, [] (double f) { return f < 90.0 ? 2.0 : 0.0; }, -14.0, monoBass, -10.0, 33),
                                    48000.0, "Night drive loop.wav");
        auto messy = profileMix (house, sr, [] (double f) { return (f < 56.0 ? 7.0 : 0.0) + (f > 445.0 && f < 2245.0 ? -6.0 : 0.0) + (f > 2245.0 && f < 5600.0 ? 3.0 : 0.0); },
                                 -9.0, [] (double f) { return f < 140.0 ? 4.0 : 0.0; }, -11.0, 11);
        append (messy, kickAndBass (sr, 6.0, 4.0));
        const Shot shots[] = {
            { "wwma-en.png", messy, Lang::english, false },
            { "wwma-fi.png", messy, Lang::finnish, false },
            { "wwma-good-en.png", balanced, Lang::english, true },
            { "wwma-reference-en.png", messy, Lang::english, false, true, false },
            { "wwma-reference-ab-en.png", messy, Lang::english, false, true, true },
        };

        for (const auto& shot : shots)
        {
            WwmaAudioProcessor p;
            p.setPlayConfigDetails (2, 2, sr, 512);
            p.prepareToPlay (sr, 512);
            p.setLang (shot.lang);
            p.setWindow (Window::song);
            if (shot.withRef)
            {
                p.refs.loadNow (0, refA);
                p.refs.loadNow (1, refB);
                p.refs.setActive (0);
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            auto* editor = dynamic_cast<WwmaAudioProcessorEditor*> (ed.get());
            if (shot.withRef)
                editor->referencesChanged();
            if (shot.listen)
                editor->setListening (true);
            juce::MidiBuffer midi;
            const size_t halfSecond = (size_t) (sr * 0.5);
            auto feed = [&] (const Stereo& x)
            {
                juce::AudioBuffer<float> buf (2, 512);
                size_t sinceHistory = 0;
                for (size_t i = 0; i + 512 <= x.l.size(); i += 512)
                {
                    buf.copyFrom (0, 0, x.l.data() + i, 512);
                    buf.copyFrom (1, 0, x.r.data() + i, 512);
                    p.processBlock (buf, midi);
                    sinceHistory += 512;
                    if (sinceHistory >= halfSecond)
                    {
                        // what the editor's timer would do every 0.5 s
                        sinceHistory = 0;
                        editor->refresh (true);
                        editor->pushHistory();
                    }
                }
            };

            if (shot.baseline)
            {
                // show the progress features: snapshots of the messy mix, then the fixed one
                feed (messy);
                editor->refresh();
                editor->settleDisplay();
                editor->saveSnapshot();
                p.analyzer.requestReset();
                feed (balanced);
            }
            else
                feed (shot.audio);

            for (int k = 0; k < 60; ++k)
                editor->refresh (true);
            editor->settleDisplay();
            editor->refresh (false);
            const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (dir.getChildFile (shot.name));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (img, out);
            std::cout << "  wrote " << shot.name << std::endl;
        }
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
