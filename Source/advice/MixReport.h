#pragma once

#include "Advisor.h"
#include <map>
#include <string>
#include <vector>

namespace wwma
{
/** One number that describes the mix, with how far it is from the style's target
    (0 = on target). Used for before/after snapshots and for the report. */
struct KeyMetric
{
    std::string id;
    float value = 0.0f;
    float off = 0.0f;    // distance outside the target, in the metric's own unit
    bool valid = true;
};

std::vector<KeyMetric> keyMetrics (const Assessment& a, const Profile& p, Stage stage);
juce::String metricName (const std::string& id, Lang lang);
juce::String metricValueText (const std::string& id, float value, Lang lang);
/** The smallest change in "off" that counts as better or worse. */
float metricStep (const std::string& id);

/** A saved snapshot: the score and the key metrics at that moment, and whether the
    user confirmed saving a new version of the DAW project to go with it. */
struct SavedSnapshot
{
    juce::Time time;
    float score = 0.0f;
    std::map<std::string, std::pair<float, float>> metrics; // id -> (value, off)
    bool confirmed = false;
    juce::Time confirmedAt;
};

struct MetricChange
{
    std::string id;
    float before = 0.0f, now = 0.0f;
    int verdict = 0; // +1 better, -1 worse, 0 about the same
};

/** Before vs now for every metric both sides have; better ones first, then worse, then unchanged. */
std::vector<MetricChange> compareMetrics (const std::map<std::string, std::pair<float, float>>& before,
                                          const std::vector<KeyMetric>& now);

std::map<std::string, std::pair<float, float>> toMap (const std::vector<KeyMetric>& metrics);

/** Everything that goes into a report file. */
struct ReportInput
{
    juce::Time time;
    juce::String presetName;
    Stage stage = Stage::mastering;
    Window window = Window::song;
    float secondsAnalysed = 0.0f;
    float tempoBpm = 0.0f;
    juce::String styleGuess;          // "Sounds like House (72 %)", empty when unknown
    Assessment assessment;
    Profile profile;
    std::vector<SavedSnapshot> snapshots;
    juce::String referenceName;       // empty when no reference is loaded
    Assessment referenceAssessment;
};

/** Machine-readable report (saved next to the Markdown one, read back for comparisons). */
juce::var reportJson (const ReportInput& in);

/** The report a person reads. When a previous report's JSON is given, it ends with
    what changed since then. */
juce::String reportMarkdown (const ReportInput& in, Lang lang, const juce::var& previous = {}, const juce::String& previousName = {});

/** Reads the metrics back from a report's JSON. */
std::map<std::string, std::pair<float, float>> metricsFromJson (const juce::var& json);
} // namespace wwma
