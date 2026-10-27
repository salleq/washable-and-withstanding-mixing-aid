#pragma once

// A small "song" generator for calibrating the advice: kick, bass, snare/clap,
// hats, pad and a lead, mixed and limited like a real track, with knobs for the
// classic mistakes. Deterministic for a given seed.

#include "analysis/Filters.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace wwma::synth
{
struct Stereo { std::vector<float> l, r; };

struct SongParams
{
    double sr = 48000.0, seconds = 32.0, bpm = 124.0;

    // levels of the parts (dB, relative to a sensible default balance)
    double kickDb = 0.0, bassDb = 0.0, snareDb = 0.0, hatDb = 0.0, padDb = 0.0, leadDb = 0.0;
    bool drums = true;
    bool halfTimeSnare = false;   // snare on beat 3 only (dubstep, trap)
    bool breakbeat = false;       // kick on 1 and the "and" of 2 (DnB, breaks)

    double duckDb = 6.0;          // sidechain: how far the kick ducks the bass and pad
    bool bassSustain = false;     // a long bass note under every kick instead of off-beat notes
    double bassHarmonics = 1.0;   // 0 = pure sine bass (nothing for phones), 1 = normal
    double subDb = -100.0;        // extra sine sub under the bass
    double bassWidth = 0.0;       // 0 = mono bass, 1 = fully different left and right
    double padWidth = 0.6;        // stereo detune of the pad
    double hatPan = 0.25;         // hats to the left (0..1)
    double leadAntiPhase = 0.0;   // 1 = the lead is flipped in polarity on the right (vanishes in mono)

    // tone mistakes (dB)
    double mudDb = 0.0;           // 300 Hz bump on the music
    double harshDb = 0.0;         // 3.5 kHz bump on the mix
    double sibilanceDb = 0.0;     // narrow 7 kHz bump
    double airDb = 0.0;           // shelf above ~8 kHz
    double rumbleDb = -100.0;     // 21 Hz rumble, relative to the kick
    double balanceDb = 0.0;       // right channel turned down by this much
    double dc = 0.0;              // DC offset added to both channels

    // master
    double driveDb = 7.0;         // into the limiter: more = louder and flatter
    double ceilingDb = -1.0;      // limiter output ceiling (sample peak)
    bool limiter = true;          // false: just the gain, hard clipped at 0 dBFS
};

namespace detail
{
    inline Biquad peak (double sr, double f, double q, double gainDb)
    {
        Biquad b;
        const double a = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr, alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        b.setNormalised (1.0 + alpha * a, -2.0 * c, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * c, 1.0 - alpha / a);
        return b;
    }

    inline Biquad highShelf (double sr, double f, double gainDb)
    {
        Biquad b;
        const double a = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr, c = std::cos (w);
        const double alpha = std::sin (w) / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (a) * alpha;
        b.setNormalised (a * ((a + 1.0) + (a - 1.0) * c + sq), -2.0 * a * ((a - 1.0) + (a + 1.0) * c), a * ((a + 1.0) + (a - 1.0) * c - sq),
                         (a + 1.0) - (a - 1.0) * c + sq, 2.0 * ((a - 1.0) - (a + 1.0) * c), (a + 1.0) - (a - 1.0) * c - sq);
        return b;
    }

    /** Band-limited saw from a few harmonics. */
    inline double saw (double phase, int harmonics, double upper = 1.0)
    {
        double v = 0.0;
        for (int n = 1; n <= harmonics; ++n)
            v += (n == 1 ? 1.0 : upper) * std::sin (n * phase) / n;
        return v;
    }

    inline double db (double v) { return std::pow (10.0, v / 20.0); }
}

inline Stereo renderSong (const SongParams& p, int seed = 1)
{
    using namespace detail;
    const double sr = p.sr;
    const int n = (int) (p.seconds * sr);
    Stereo out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    juce::Random rng (seed);

    const double beat = 60.0 / p.bpm, sixteenth = beat / 4.0;
    const double bassNotes[] = { 55.0, 55.0, 43.65, 49.0 };   // A1 A1 F1 G1
    const double chords[4][3] = { { 220.0, 261.63, 329.63 }, { 220.0, 261.63, 329.63 },
                                  { 174.61, 220.0, 261.63 }, { 196.0, 246.94, 293.66 } };
    const double leadNotes[16] = { 440, 0, 523.25, 0, 659.26, 0, 523.25, 440, 0, 392, 0, 440, 523.25, 0, 587.33, 0 };

    Biquad snareHp, snareLp, hatHpL, hatHpR, padLpL, padLpR, leadLp, bassLp;
    snareHp.setHighPass (sr, 900.0, 0.7);
    snareLp.setLowPass (sr, 9000.0, 0.7);
    hatHpL.setHighPass (sr, 7000.0, 0.7);
    hatHpR.setHighPass (sr, 7000.0, 0.7);
    padLpL.setLowPass (sr, 2600.0, 0.7);
    padLpR.setLowPass (sr, 2600.0, 0.7);
    leadLp.setLowPass (sr, 3200.0, 0.9);
    bassLp.setLowPass (sr, 420.0, 0.8);
    Biquad bassLpR = bassLp;
    Biquad mudL = peak (sr, 300.0, 1.0, p.mudDb), mudR = mudL;
    Biquad harshL = peak (sr, 3500.0, 1.2, p.harshDb), harshR = harshL;
    Biquad sibL = peak (sr, 7100.0, 4.0, p.sibilanceDb), sibR = sibL;
    Biquad airL = highShelf (sr, 8000.0, p.airDb), airR = airL;

    double kickPhase = 0.0, bassPhaseL = 0.0, bassPhaseR = 0.0, subPhase = 0.0, leadPhase = 0.0;
    double padPhase[2][3] {};
    const double duck = 1.0 - db (-p.duckDb);
    double lastKick = -10.0;

    std::vector<double> mixL ((size_t) n), mixR ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const int bar = (int) (t / (4.0 * beat)) % 4;
        const double tBar = std::fmod (t, 4.0 * beat);
        const double tb = std::fmod (t, beat);
        const int beatInBar = (int) (tBar / beat);
        const double t16 = std::fmod (t, sixteenth);
        const int step16 = (int) (tBar / sixteenth);

        // kick pattern: four to the floor, or 1 and the "and" of 2 (and 3.5 lightly) for breaks
        bool kickHit = false;
        if (p.drums)
        {
            if (! p.breakbeat)
                kickHit = tb < 1.0 / sr;
            else
                kickHit = (step16 == 0 || step16 == 6 || step16 == 10) && t16 < 1.0 / sr;
        }
        if (kickHit)
        {
            lastKick = t;
            kickPhase = 0.0;
        }
        const double tk = t - lastKick;
        const double sidechain = 1.0 - duck * std::exp (-tk * 9.0);

        double kick = 0.0;
        if (p.drums && tk < 2.0)
        {
            kickPhase += 2.0 * pi * (48.0 + 110.0 * std::exp (-tk * 32.0)) / sr;
            kick = db (p.kickDb) * (0.95 * std::exp (-tk * 7.0) * std::sin (kickPhase)
                                    + 0.25 * (rng.nextDouble() * 2.0 - 1.0) * std::exp (-tk * 400.0));
        }

        // bass: off-beat eighths with harmonics, low-passed
        const double tOff = std::fmod (t + beat * 0.5, beat);
        const double tBass = std::fmod (t, 2.0 * beat);
        const double bassEnv = p.bassSustain ? std::min (1.0, tBass / 0.004) * 0.75
                                             : std::min (1.0, tOff / 0.004) * std::exp (-tOff * 3.0)
                                                   * (tOff < beat * 0.45 ? 1.0 : std::exp (-(tOff - beat * 0.45) * 60.0));
        const double bf = bassNotes[bar];
        bassPhaseL += 2.0 * pi * bf / sr;
        bassPhaseR += 2.0 * pi * bf * (1.0 + 0.012 * p.bassWidth) / sr;
        subPhase += 2.0 * pi * bf / sr;
        const double bassWaveL = bassLp.process (saw (bassPhaseL, 8, p.bassHarmonics));
        const double bassWaveR = bassLpR.process (saw (bassPhaseR + 1.3, 8, p.bassHarmonics));
        const double bassGain = 0.42 * db (p.bassDb) * bassEnv * sidechain;
        const double sub = db (p.subDb) * 0.4 * bassEnv * sidechain * std::sin (subPhase);
        const double bl = bassGain * bassWaveL + sub;
        const double br = bassGain * ((1.0 - p.bassWidth) * bassWaveL + p.bassWidth * bassWaveR) + sub;

        // snare / clap on 2 and 4 (or on 3 for half time)
        double snare = 0.0;
        const double snareNoise = snareLp.process (snareHp.process (rng.nextDouble() * 2.0 - 1.0));
        const bool snareBeat = p.halfTimeSnare ? beatInBar == 2 : (beatInBar == 1 || beatInBar == 3);
        if (p.drums && snareBeat)
        {
            const double body = 0.35 * std::exp (-tb * 30.0) * std::sin (2.0 * pi * 190.0 * tb);
            snare = db (p.snareDb) * (body + 0.55 * snareNoise * std::exp (-tb * 16.0));
        }

        // hats: closed sixteenths, accent on the off-beat
        double hatL = 0.0, hatR = 0.0;
        {
            const double nz = rng.nextDouble() * 2.0 - 1.0;
            const double fl = hatHpL.process (nz), fr = hatHpR.process (nz);
            if (p.drums)
            {
                const double accent = (step16 % 4 == 2) ? 1.0 : 0.45;
                const double env = accent * std::exp (-t16 * 70.0) * db (p.hatDb) * 0.15;
                hatL = fl * env * (1.0 + p.hatPan);
                hatR = fr * env * (1.0 - p.hatPan);
            }
        }

        // pad: sustained chord, detuned left and right
        double padL = 0.0, padR = 0.0;
        for (int v = 0; v < 3; ++v)
        {
            const double f = chords[bar][v];
            padPhase[0][v] += 2.0 * pi * f * (1.0 - 0.004 * p.padWidth) / sr;
            padPhase[1][v] += 2.0 * pi * f * (1.0 + 0.004 * p.padWidth) / sr;
            padL += saw (padPhase[0][v], 12);
            padR += saw (padPhase[1][v], 12);
        }
        const double padGain = 0.07 * db (p.padDb) * (1.0 - 0.5 * duck * std::exp (-tk * 6.0));
        padL = padLpL.process (padL) * padGain;
        padR = padLpR.process (padR) * padGain;

        // lead pluck in the mids
        const double lf = leadNotes[step16 % 16];
        double lead = 0.0;
        if (lf > 0.0)
        {
            leadPhase += 2.0 * pi * lf / sr;
            lead = 0.16 * db (p.leadDb) * std::exp (-t16 * 14.0) * saw (leadPhase, 10);
        }
        lead = leadLp.process (lead);

        // the music (pad, lead, bass) gets the mud bump
        const double musicL = mudL.process (padL + lead + bl);
        const double musicR = mudR.process (padR + lead * (1.0 - 2.0 * p.leadAntiPhase) + br);

        double l = musicL + kick + snare + hatL;
        double r = musicR + kick + snare + hatR;
        if (p.rumbleDb > -90.0)
        {
            const double rum = db (p.rumbleDb) * 0.5 * std::sin (2.0 * pi * 21.0 * t);
            l += rum;
            r += rum;
        }
        l = airL.process (sibL.process (harshL.process (l)));
        r = airR.process (sibR.process (harshR.process (r)));
        mixL[(size_t) i] = l;
        mixR[(size_t) i] = r * db (-p.balanceDb);
    }

    // master: drive into a look-ahead peak limiter (or straight into hard clipping)
    const double drive = db (p.driveDb - 6.0), ceiling = db (p.ceilingDb);
    const int look = (int) (0.0015 * sr);
    const double release = std::exp (-1.0 / (0.08 * sr));
    double gain = 1.0;
    std::deque<std::pair<int, double>> window; // running max of |x| over the look-ahead
    for (int i = 0; i < n + look; ++i)
    {
        if (i < n)
        {
            const double a = std::max (std::abs (mixL[(size_t) i]), std::abs (mixR[(size_t) i])) * drive;
            while (! window.empty() && window.back().second <= a) window.pop_back();
            window.push_back ({ i, a });
        }
        while (! window.empty() && window.front().first < i - look) window.pop_front();
        const int o = i - look;
        if (o < 0)
            continue;
        const double need = window.empty() ? 1.0 : std::min (1.0, ceiling / std::max (1.0e-9, window.front().second));
        gain = need < gain ? need : need + (gain - need) * release;
        double l = mixL[(size_t) o] * drive, r = mixR[(size_t) o] * drive;
        if (p.limiter)
        {
            l *= gain;
            r *= gain;
        }
        else
        {
            l = std::clamp (l, -1.0, 1.0);
            r = std::clamp (r, -1.0, 1.0);
        }
        out.l[(size_t) o] = (float) (l + p.dc);
        out.r[(size_t) o] = (float) (r + p.dc);
    }
    return out;
}
} // namespace wwma::synth
