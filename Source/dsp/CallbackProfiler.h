#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace wwma
{
/** Measures how long processBlock() takes compared with the time the audio device gives it.
    Off unless the environment variable WWMA_PROFILE is set (to anything but "0").

    The audio thread only writes two numbers into a preallocated ring buffer (no allocation, no
    locks, no logging). A separate low-priority thread reads them every two seconds and appends a
    summary line to ~/wwma_callback_profile.log (or the file named by WWMA_PROFILE_FILE). */
class CallbackProfiler : private juce::Thread
{
public:
    CallbackProfiler() : juce::Thread ("WWMA callback profiler")
    {
        const auto v = juce::SystemStats::getEnvironmentVariable ("WWMA_PROFILE", {});
        enabled = v.isNotEmpty() && v != "0";
        if (! enabled)
            return;
        ring.resize (ringSize);
        auto path = juce::SystemStats::getEnvironmentVariable ("WWMA_PROFILE_FILE", {});
        logFile = path.isNotEmpty() ? juce::File (path)
                                    : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("wwma_callback_profile.log");
        startThread (juce::Thread::Priority::low);
    }

    ~CallbackProfiler() override { stopThread (2000); }

    bool isEnabled() const noexcept { return enabled; }

    void prepare (double sampleRate, int blockSize) noexcept
    {
        rate.store (sampleRate);
        block.store (blockSize);
    }

    /** Audio thread. Put one on the stack at the top of processBlock(). */
    struct Scope
    {
        Scope (CallbackProfiler& p, int numSamples) noexcept
            : owner (p), n (numSamples), start (p.enabled ? juce::Time::getHighResolutionTicks() : 0) {}
        ~Scope()
        {
            if (owner.enabled)
                owner.record (start, n);
        }
        CallbackProfiler& owner;
        int n;
        juce::int64 start;
    };

private:
    struct Entry { float ms; float gapMs; int samples; };

    void record (juce::int64 startTicks, int n) noexcept
    {
        const auto now = juce::Time::getHighResolutionTicks();
        const float ms = (float) (juce::Time::highResolutionTicksToSeconds (now - startTicks) * 1000.0);
        const float gap = lastStart != 0 ? (float) (juce::Time::highResolutionTicksToSeconds (startTicks - lastStart) * 1000.0) : 0.0f;
        lastStart = startTicks;
        const auto i = head.load (std::memory_order_relaxed);
        ring[i & (ringSize - 1)] = { ms, gap, n };
        head.store (i + 1, std::memory_order_release);
    }

    void run() override
    {
        size_t tail = 0;
        logFile.appendText ("--- WWMA callback profile started " + juce::Time::getCurrentTime().toString (true, true) + "\n");
        while (! threadShouldExit())
        {
            wait (2000);
            const auto h = head.load (std::memory_order_acquire);
            if (h == tail)
                continue;
            if (h - tail > ringSize)
                tail = h - ringSize; // the reader fell behind: skip the overwritten part
            std::vector<float> times, gaps;
            const double budget = block.load() > 0 && rate.load() > 0.0 ? 1000.0 * block.load() / rate.load() : 0.0;
            double sum = 0.0;
            int over = 0, late = 0;
            for (auto i = tail; i < h; ++i)
            {
                const auto e = ring[i & (ringSize - 1)];
                const double b = e.samples > 0 && rate.load() > 0.0 ? 1000.0 * e.samples / rate.load() : budget;
                times.push_back (e.ms);
                sum += e.ms;
                if (b > 0.0 && e.ms > b) ++over;
                // the callback started much later than one block after the previous one
                if (e.gapMs > 0.0f && b > 0.0 && e.gapMs > 1.5 * b) { ++late; gaps.push_back (e.gapMs); }
            }
            tail = h;
            std::sort (times.begin(), times.end());
            const auto pct = [&] (double p) { return times[(size_t) juce::jmin ((double) times.size() - 1, p * (double) times.size())]; };
            const float maxGap = gaps.empty() ? 0.0f : *std::max_element (gaps.begin(), gaps.end());
            logFile.appendText (juce::String::formatted (
                "%s  blocks=%zu budget=%.2fms  dsp avg=%.3f p99=%.3f max=%.3f ms  over_budget=%d  late_callbacks=%d (max gap %.1f ms)\n",
                juce::Time::getCurrentTime().toString (false, true).toRawUTF8(), times.size(), budget,
                sum / (double) times.size(), pct (0.99), (double) times.back(), over, late, (double) maxGap));
        }
    }

    static constexpr size_t ringSize = 1 << 16;
    bool enabled = false;
    juce::File logFile;
    std::vector<Entry> ring;
    std::atomic<size_t> head { 0 };
    std::atomic<double> rate { 0.0 };
    std::atomic<int> block { 0 };
    juce::int64 lastStart = 0; // audio thread only
};
} // namespace wwma
