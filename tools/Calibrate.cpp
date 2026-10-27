// Calibration helper: renders synthetic songs with typical mixing mistakes,
// analyses them and prints what WWMA says. Used to tune the advice thresholds.
//   wwma_calibrate [preset name]

#include "analysis/Analyzer.h"
#include "advice/Advisor.h"
#include "advice/Profiles.h"
#include "advice/StyleGuess.h"
#include "SongSynth.h"
#include <cstdio>
#include <functional>
#include <iostream>

using namespace wwma;

namespace
{
Snapshot analyseSong (const synth::Stereo& s, double sr)
{
    Analyzer a;
    a.prepare (sr);
    const int n = (int) s.l.size(), block = 512;
    for (int i = 0; i < n; i += block)
        a.process (s.l.data() + i, s.r.data() + i, std::min (block, n - i));
    Snapshot snap;
    a.readSnapshot (snap);
    return snap;
}

struct Case
{
    const char* name;
    std::function<void (synth::SongParams&)> setup;
};
} // namespace

static int styles()
{
    struct Style { const char* name; std::function<void (synth::SongParams&)> setup; };
    const std::vector<Style> list {
        { "house 124", [] (synth::SongParams&) {} },
        { "deep 120 soft", [] (synth::SongParams& p) { p.bpm = 120; p.driveDb = 4; p.airDb = -4; } },
        { "techno 132", [] (synth::SongParams& p) { p.bpm = 132; p.driveDb = 9; p.padDb = -4; } },
        { "trance 138", [] (synth::SongParams& p) { p.bpm = 138; p.padDb = 3; p.airDb = 2; } },
        { "hardstyle 150", [] (synth::SongParams& p) { p.bpm = 150; p.driveDb = 11; p.kickDb = 3; } },
        { "dnb 174", [] (synth::SongParams& p) { p.bpm = 174; p.breakbeat = true; p.subDb = -6; } },
        { "dubstep 140", [] (synth::SongParams& p) { p.bpm = 140; p.halfTimeSnare = true; p.subDb = -3; p.driveDb = 10; } },
        { "hiphop 90", [] (synth::SongParams& p) { p.bpm = 90; p.breakbeat = true; p.subDb = -3; p.hatDb = -4; } },
        { "lofi 82", [] (synth::SongParams& p) { p.bpm = 82; p.breakbeat = true; p.airDb = -9; p.driveDb = 2; } },
        { "ambient", [] (synth::SongParams& p) { p.bpm = 90; p.drums = false; p.driveDb = -2; p.padDb = 4; } },
    };
    const auto& profiles = builtInProfiles();
    for (auto& st : list)
    {
        synth::SongParams p;
        st.setup (p);
        const auto snap = analyseSong (synth::renderSong (p), p.sr);
        const auto g = guessStyle (snap);
        std::printf ("%-15s bpm %5.1f (%.2f) LRA %4.1f -> [%s %.0f%%] %s %.0f%%, %s %.0f%%\n", st.name, snap.tempoBpm, snap.tempoConfidence,
                     snap.loudnessRange, g.best >= 0 ? profiles[(size_t) g.best].family.c_str() : "-", 100.0f * g.familyConfidence,
                     g.best >= 0 ? profiles[(size_t) g.best].name.c_str() : "-", 100.0f * g.confidence,
                     g.runnerUp >= 0 ? profiles[(size_t) g.runnerUp].name.c_str() : "-", 100.0f * g.runnerUpConfidence);
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "styles")
        return styles();
    if (argc > 2 && juce::String (argv[1]) == "acf")
    {
        synth::SongParams p;
        p.bpm = juce::String (argv[2]).getDoubleValue();
        if (argc > 3) p.driveDb = juce::String (argv[3]).getDoubleValue();
        if (argc > 4) p.kickDb = juce::String (argv[4]).getDoubleValue();
        const auto song = synth::renderSong (p);
        Analyzer a;
        a.prepare (p.sr);
        for (int i = 0; i < (int) song.l.size(); i += 512)
            a.process (song.l.data() + i, song.r.data() + i, std::min (512, (int) song.l.size() - i));
        for (int lag = 20; lag <= 420; ++lag)
            std::printf ("%d %.3f\n", lag, a.tempoAcfAt (lag));
        return 0;
    }

    juce::String wanted = argc > 1 ? juce::String (argv[1]) : juce::String ("House");
    const Profile* profile = &builtInProfiles().front();
    for (auto& p : builtInProfiles())
        if (juce::String (p.name).equalsIgnoreCase (wanted))
            profile = &p;

    std::cout << "Preset: " << profile->name << std::endl;

    const std::vector<Case> cases {
        { "good", [] (synth::SongParams&) {} },
        { "sustain duck0", [] (synth::SongParams& p) { p.bassSustain = true; p.duckDb = 0; } },
        { "sustain duck3", [] (synth::SongParams& p) { p.bassSustain = true; p.duckDb = 3; } },
        { "sustain duck6", [] (synth::SongParams& p) { p.bassSustain = true; p.duckDb = 6; } },
        { "sustain duck10", [] (synth::SongParams& p) { p.bassSustain = true; p.duckDb = 10; } },
        { "noduck+bass", [] (synth::SongParams& p) { p.duckDb = 0; p.bassDb = 6; } },
        { "squashed", [] (synth::SongParams& p) { p.driveDb = 17; } },
        { "quiet", [] (synth::SongParams& p) { p.driveDb = -1; } },
        { "sinebass+sub", [] (synth::SongParams& p) { p.bassHarmonics = 0; p.subDb = 0; } },
        { "stereobass", [] (synth::SongParams& p) { p.bassWidth = 1; } },
        { "mud+8", [] (synth::SongParams& p) { p.mudDb = 8; } },
        { "harsh+8", [] (synth::SongParams& p) { p.harshDb = 8; } },
        { "sibilance+12", [] (synth::SongParams& p) { p.sibilanceDb = 12; } },
        { "dull-10", [] (synth::SongParams& p) { p.airDb = -10; } },
        { "rumble", [] (synth::SongParams& p) { p.rumbleDb = 0; } },
        { "balance3", [] (synth::SongParams& p) { p.balanceDb = 3; } },
        { "hardclip", [] (synth::SongParams& p) { p.limiter = false; p.driveDb = 12; } },
        { "lead anti-phase", [] (synth::SongParams& p) { p.leadAntiPhase = 1; p.leadDb = 4; } },
        { "nodrums", [] (synth::SongParams& p) { p.drums = false; } },
        { "snare-10", [] (synth::SongParams& p) { p.snareDb = -10; } },
        { "hats+10", [] (synth::SongParams& p) { p.hatDb = 10; } },
        { "kick-8", [] (synth::SongParams& p) { p.kickDb = -8; } },
        { "halftime140", [] (synth::SongParams& p) { p.bpm = 140; p.halfTimeSnare = true; } },
        { "breaks174", [] (synth::SongParams& p) { p.bpm = 174; p.breakbeat = true; } },
    };

    for (auto& c : cases)
    {
        synth::SongParams p;
        c.setup (p);
        const auto song = synth::renderSong (p);
        const auto snap = analyseSong (song, p.sr);
        const auto as = assess (snap, *profile, Window::song, Lang::english);
        const auto& m = as.measures;

        std::printf ("\n%-14s score %5.1f  I %6.1f TP %5.1f PLR %4.1f kick %c lowC %5.1f snap %5.1f lowW %6.1f W %6.1f ph %5.2f pb %5.1f bpm %5.1f (%.2f) hits %.1f/s duck %.1f\n",
                     c.name, as.score, m.loudness, m.truePeak, m.plr, m.kickValid ? 'y' : 'n',
                     m.lowContrast, m.highContrast, m.lowWidth, m.width, m.correlation, m.phoneBass, snap.tempoBpm, snap.tempoConfidence, snap.hitsPerSecond, snap.duckSong);
        std::printf ("               ranges");
        for (int r = 0; r < numRanges; ++r)
            std::printf (" %+5.1f", m.rangeDeviation[(size_t) r]);
        std::printf ("\n               ");
        for (auto& f : as.findings)
            if (f.id != "tip")
                std::printf ("%s%s(%.2f) ", f.severity == Severity::fix ? "!" : "", f.id.c_str(), f.excess);
        std::printf ("\n");
    }
    return 0;
}
