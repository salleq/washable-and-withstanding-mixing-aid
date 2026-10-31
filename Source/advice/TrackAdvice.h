#pragma once

#include "Advisor.h"
#include <map>
#include <mutex>
#include <vector>

namespace wwma
{
/** Where WWMA sits. Normally last on the master (mix), but it can also go on a single
    track: then it measures what matters for that sound and teaches how to fit it in. */
enum class TrackRole { mix, kick, bass, kickBass };
constexpr int numTrackRoles = 4;

juce::String trackRoleName (TrackRole r, Lang lang);

/** The low end of one track, as other WWMA instances see it. */
struct TrackPrint
{
    TrackRole role = TrackRole::mix;
    bool valid = false;
    std::array<float, LowFine::count> fine {}; // song average, dB
    float peakHz = 0.0f;                       // strongest low peak
    juce::uint32 updatedMs = 0;                // juce::Time::getMillisecondCounter()
};

/** Lets the WWMA on the kick and the WWMA on the bass see each other, so they can tell
    whether the two sounds collide. Shared by every instance loaded in the same process;
    hosts that run each plugin in its own process simply show no other tracks. */
class TrackBus
{
public:
    static TrackBus& shared();
    int join();
    void leave (int id);
    void publish (int id, const TrackPrint& p);
    /** Valid prints of the other instances, newest first. */
    std::vector<TrackPrint> others (int id) const;

private:
    mutable std::mutex lock;
    std::map<int, TrackPrint> prints;
    int nextId = 1;
};

/** The strongest peak of the fine low end grid between two frequencies. */
struct LowPeak
{
    bool found = false;
    float hz = 0.0f, db = -200.0f;
};
LowPeak findLowPeak (const std::array<float, LowFine::count>& fine, double loHz, double hiHz);

/** Where a kick is tuned: the lowest clear peak (within 6 dB of the strongest), because the
    pitch drop at the start can carry as much energy as the tail that sets the note. */
LowPeak findKickNote (const std::array<float, LowFine::count>& fine);

/** "A1", "F#2"... (equal temperament, A4 = 440 Hz). */
juce::String noteName (double hz);

/** What a typical sound of this kind looks like (for the tonal view): a smooth curve,
    with a wider tolerance than a full mix. For TrackRole::mix the style itself. */
Profile trackProfile (TrackRole role, const Profile& style);

/** The low end print of a measured track, for the other instances. */
TrackPrint printOf (const Snapshot& s, TrackRole role);

/** Advice for one track (kick, bass, or a kick + bass bus). Like assess(), nothing is applied:
    WWMA only measures and suggests. `others` are the other WWMA instances (TrackBus),
    `reference` an analysed reference track (a full mix), or nullptr. */
Assessment assessTrack (const Snapshot& s, const Profile& style, TrackRole role, Window w, Lang lang,
                        const std::vector<TrackPrint>& others, const Snapshot* reference);
} // namespace wwma
