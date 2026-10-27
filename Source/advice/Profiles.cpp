#include "Profiles.h"
#include <algorithm>
#include <cmath>

namespace wwma
{
std::array<float, Bands::count> relativeToTotal (const std::array<float, Bands::count>& db)
{
    const float total = sumBands (db, 0, Bands::count - 1);
    std::array<float, Bands::count> out {};
    for (int b = 0; b < Bands::count; ++b)
        out[(size_t) b] = db[(size_t) b] - total;
    return out;
}

float sumBands (const std::array<float, Bands::count>& db, int first, int last)
{
    double p = 0.0;
    for (int b = first; b <= last; ++b)
        p += std::pow (10.0, db[(size_t) b] / 10.0);
    return p > 1.0e-30 ? (float) (10.0 * std::log10 (p)) : -300.0f;
}

void Profile::computeTarget()
{
    std::array<float, Bands::count> db {};
    for (int b = 0; b < Bands::count; ++b)
    {
        const double f = Bands::centre (b);
        double l = tilt * std::log2 (f / 1000.0);
        l += lowShelf / (1.0 + std::pow (f / 120.0, 2.0));                    // gentle low end lift
        l -= 10.0 * std::log10 (1.0 + std::pow (subCorner / f, 4.0));        // 12 dB/oct sub roll-off
        l -= 10.0 * std::log10 (1.0 + std::pow (f / highCorner, 2.0));       // 6 dB/oct top roll-off
        db[(size_t) b] = (float) l;
    }
    target = relativeToTotal (db);
}

namespace
{
    struct Spec
    {
        const char* name; bool electronic;
        float lufsMin, lufsMax, tp, plrMin, plrMax, lraMax;
        float tilt, lowShelf, subCorner, highCorner;
        float lowWidthMax, widthMax, lowContrastMin;
    };

    // name                               elec  LUFS min/max   TP    PLR min/max  LRA   tilt  shelf sub  top     lowW   width  kick
    const Spec specs[] = {
        { "House",                        true,  -9.0f, -7.0f, -0.5f, 7.0f, 10.0f,  7.0f, -3.0f, 1.0f, 32, 12000, -20.0f, -6.0f, 5.0f },
        { "Deep House",                   true, -10.0f, -8.0f, -0.5f, 8.0f, 11.0f,  7.0f, -3.1f, 1.5f, 32, 10000, -20.0f, -6.0f, 5.0f },
        { "Tech House",                   true,  -8.5f, -6.5f, -0.5f, 6.5f,  9.5f,  6.0f, -3.0f, 1.5f, 30, 12000, -20.0f, -6.0f, 5.0f },
        { "Techno",                       true,  -8.5f, -6.0f, -0.5f, 6.0f,  9.0f,  6.0f, -2.8f, 1.5f, 30, 11000, -20.0f, -6.0f, 5.0f },
        { "Melodic Techno / Progressive", true,  -9.5f, -7.5f, -0.5f, 7.0f, 10.0f,  7.0f, -3.0f, 1.0f, 32, 11000, -20.0f, -5.0f, 4.5f },
        { "Trance",                       true,  -8.5f, -6.5f, -0.5f, 6.5f,  9.0f,  7.0f, -2.6f, 0.5f, 32, 14000, -20.0f, -5.0f, 4.5f },
        { "EDM / Big Room",               true,  -7.0f, -5.0f, -0.3f, 5.5f,  8.0f,  7.0f, -2.6f, 1.0f, 32, 14000, -20.0f, -5.0f, 4.5f },
        { "Drum & Bass",                  true,  -8.0f, -6.0f, -0.3f, 6.0f,  9.0f,  6.0f, -2.8f, 2.0f, 28, 13000, -20.0f, -5.0f, 4.0f },
        { "Dubstep / Bass Music",         true,  -7.0f, -5.0f, -0.3f, 5.0f,  8.0f,  7.0f, -2.7f, 2.5f, 28, 13000, -20.0f, -5.0f, 3.0f },
        { "Hardstyle / Hard Dance",       true,  -6.5f, -4.5f, -0.3f, 5.0f,  7.5f,  6.0f, -2.6f, 2.0f, 32, 13000, -20.0f, -5.0f, 4.0f },
        { "Synthwave / Retrowave",        true, -11.0f, -8.0f, -1.0f, 8.0f, 12.0f,  8.0f, -3.2f, 0.0f, 35, 10000, -18.0f, -4.0f, 4.0f },
        { "Ambient / Downtempo",          true, -16.0f,-11.0f, -1.0f,10.0f, 16.0f, 12.0f, -3.3f, 0.0f, 30, 10000, -16.0f, -3.0f, 0.0f },
        { "Lo-fi / Chillhop",             true, -14.0f,-10.0f, -1.0f, 9.0f, 13.0f,  8.0f, -3.6f, 0.5f, 40,  8000, -18.0f, -5.0f, 3.0f },
        { "Electronic (streaming)",       true, -14.0f,-10.0f, -1.0f, 8.0f, 12.0f,  9.0f, -3.0f, 1.0f, 32, 12000, -20.0f, -5.0f, 4.5f },
        { "Hip Hop / Trap",               false, -9.0f, -7.0f, -0.5f, 6.0f, 10.0f,  7.0f, -3.1f, 3.0f, 28, 12000, -20.0f, -6.0f, 3.0f },
        { "Pop",                          false,-10.0f, -7.5f, -1.0f, 7.0f, 10.0f,  7.0f, -2.6f, 0.5f, 35, 14000, -18.0f, -6.0f, 3.0f },
        { "Rock / Band",                  false,-10.0f, -8.0f, -1.0f, 7.0f, 10.0f,  8.0f, -2.4f,-1.0f, 40, 13000, -16.0f, -6.0f, 3.0f },
        { "Acoustic / Singer-Songwriter", false,-16.0f,-12.0f, -1.0f,10.0f, 15.0f, 10.0f, -2.4f,-2.0f, 50, 12000, -16.0f, -6.0f, 0.0f },
        { "Streaming safe (any style)",   false,-15.0f,-13.0f, -1.0f, 9.0f, 14.0f, 10.0f, -2.8f, 0.5f, 35, 12000, -18.0f, -5.0f, 3.0f },
    };
}

const std::vector<Profile>& builtInProfiles()
{
    static const std::vector<Profile> profiles = []
    {
        std::vector<Profile> v;
        for (const auto& s : specs)
        {
            Profile p;
            p.name = s.name;
            p.electronic = s.electronic;
            p.lufsMin = s.lufsMin; p.lufsMax = s.lufsMax;
            p.truePeakMax = s.tp;
            p.plrMin = s.plrMin; p.plrMax = s.plrMax;
            p.lraMax = s.lraMax;
            p.tilt = s.tilt; p.lowShelf = s.lowShelf;
            p.subCorner = s.subCorner; p.highCorner = s.highCorner;
            p.lowWidthMax = s.lowWidthMax; p.widthMax = s.widthMax;
            p.lowContrastMin = s.lowContrastMin;
            if (s.lraMax >= 10.0f)  // dynamic styles: softer drum expectations
                p.highContrastMin = 5.0f;
            p.computeTarget();
            v.push_back (p);
        }
        return v;
    }();
    return profiles;
}

int defaultProfileIndex() { return 0; }

Profile profileFromSnapshot (const Snapshot& s, const std::string& name)
{
    Profile p;
    p.name = name;

    std::array<float, Bands::count> total {};
    for (int b = 0; b < Bands::count; ++b)
    {
        const double m = std::pow (10.0, s.midSong[(size_t) b] / 10.0), sd = std::pow (10.0, s.sideSong[(size_t) b] / 10.0);
        total[(size_t) b] = (float) (10.0 * std::log10 (m + sd + 1.0e-30));
    }
    p.target = relativeToTotal (total);

    p.lufsMin = s.integrated - 1.5f;
    p.lufsMax = s.integrated + 1.5f;
    p.truePeakMax = std::max (-1.0f, std::min (0.0f, s.truePeakMax + 0.3f));
    const float plr = s.truePeakMax - s.integrated;
    p.plrMin = plr - 2.0f;
    p.plrMax = plr + 2.0f;
    p.lraMax = s.loudnessRange + 3.0f;

    const float lowSide = sumBands (s.sideSong, 0, 8), lowMid = sumBands (s.midSong, 0, 8);
    const float side = sumBands (s.sideSong, 0, Bands::count - 1), mid = sumBands (s.midSong, 0, Bands::count - 1);
    p.lowWidthMax = std::max (-24.0f, lowSide - lowMid + 4.0f);
    p.widthMax = side - mid + 4.0f;
    p.widthMin = side - mid - 8.0f;

    if (s.kickSongValid)
        p.lowContrastMin = s.lowContrastSong - 3.0f;
    if (s.contrastSongValid)
    {
        p.highContrastMin = s.highContrastSong - 4.0f;
        p.highContrastMax = s.highContrastSong + 6.0f;
    }
    return p;
}
} // namespace wwma
