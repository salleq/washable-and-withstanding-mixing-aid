#include "StyleGuess.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace wwma
{
namespace
{
    bool tempoUsable (const Snapshot& s) { return s.tempoBpm > 0.0f && s.tempoConfidence >= 0.5f; }

    float tonalDistance (const Snapshot& s, const Profile& p)
    {
        std::array<float, Bands::count> total {};
        for (int b = 0; b < Bands::count; ++b)
            total[(size_t) b] = (float) (10.0 * std::log10 (std::pow (10.0, s.midSong[(size_t) b] / 10.0)
                                                            + std::pow (10.0, s.sideSong[(size_t) b] / 10.0) + 1.0e-30));
        const auto rel = relativeToTotal (total);

        // band by band against the target, centred on the median so overall level doesn't matter
        constexpr int first = 2, last = 27;
        std::vector<float> d;
        for (int b = first; b <= last; ++b)
            d.push_back (rel[(size_t) b] - p.target[(size_t) b]);
        auto sorted = d;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        const float median = sorted[sorted.size() / 2];
        float sum = 0.0f;
        for (float x : d)
            sum += std::min (6.0f, std::abs (x - median));
        return sum / (float) d.size();
    }
} // namespace

float styleDistance (const Snapshot& s, const Profile& p)
{
    // tonal balance: 1 point per dB of average deviation
    float dist = tonalDistance (s, p);

    // tempo: inside the style's range is free; half or double time (DnB heard at 87) costs a little
    const bool drumless = p.lraMax >= 10.0f;
    if (tempoUsable (s))
    {
        float best = 1.0e9f;
        for (float factor : { 1.0f, 2.0f, 0.5f })
        {
            const float bpm = s.tempoBpm * factor;
            const float outside = std::max ({ 0.0f, p.bpmMin - bpm, bpm - p.bpmMax });
            best = std::min (best, outside / 4.0f + (factor != 1.0f ? 0.3f : 0.0f));
        }
        dist += best + (p.bpmMax - p.bpmMin) / 60.0f; // a wide range fits anything, so it says less
    }
    else
    {
        dist += drumless ? 0.0f : 1.5f;
    }

    // dynamics: a very dynamic song is unlikely to be a club style
    dist += std::max (0.0f, s.loudnessRange - p.lraMax) / 4.0f;
    return dist;
}

StyleGuess guessStyle (const Snapshot& s)
{
    StyleGuess g;
    g.tempoFound = tempoUsable (s);
    g.bpm = g.tempoFound ? s.tempoBpm : 0.0f;
    if (s.secondsAnalysed < 15.0f || ! s.spectrumSongValid)
        return g;

    const auto& profiles = builtInProfiles();
    std::vector<double> weight (profiles.size(), 0.0);
    double total = 0.0;
    for (size_t i = 0; i < profiles.size(); ++i)
    {
        if (profiles[i].bpmMax <= 0.0f)
            continue; // "streaming" presets are targets, not styles
        weight[i] = std::exp (-styleDistance (s, profiles[i]) / 0.45);
        total += weight[i];
    }
    if (total <= 0.0)
        return g;

    for (size_t i = 0; i < profiles.size(); ++i)
    {
        const float c = (float) (weight[i] / total);
        if (c > g.confidence)
        {
            g.runnerUp = g.best;
            g.runnerUpConfidence = g.confidence;
            g.best = (int) i;
            g.confidence = c;
        }
        else if (c > g.runnerUpConfidence)
        {
            g.runnerUp = (int) i;
            g.runnerUpConfidence = c;
        }
    }
    if (g.best < 0)
        return g;
    if (g.tempoFound)
    {
        // say the tempo the way the style counts it: drum & bass is 174, not 87
        const auto& bp = profiles[(size_t) g.best];
        float bestOutside = 1.0e9f;
        for (float factor : { 1.0f, 2.0f, 0.5f })
        {
            const float bpm = s.tempoBpm * factor;
            const float outside = std::max ({ 0.0f, bp.bpmMin - bpm, bpm - bp.bpmMax }) + (factor != 1.0f ? 0.5f : 0.0f);
            if (outside < bestOutside)
            {
                bestOutside = outside;
                g.bpm = bpm;
            }
        }
    }
    for (size_t i = 0; i < profiles.size(); ++i)
        if (profiles[i].family == profiles[(size_t) g.best].family)
            g.familyConfidence += (float) (weight[i] / total);
    g.valid = g.familyConfidence >= 0.3f;
    return g;
}
} // namespace wwma
