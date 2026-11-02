#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "BeforeTake.h"
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace wwma
{
/** WWMA's own folder on disk, so nothing big goes into the DAW project and nothing is lost
    when WWMA is taken out of a project and put back (e.g. to update it).

    <user app data>/WWMA/Projects/<session id>/
        state.xml          everything the DAW project would keep (references, loops, snapshots...)
        info.json          name, last used, host, role, reference names
        Versions/*.flac    "before" recordings (24-bit FLAC), each with a .json beside it

    Windows: %APPDATA%\WWMA, macOS: ~/Library/Application Support/WWMA, Linux: ~/.config/WWMA.
    Everything that is deleted goes to the system's trash, so it can be brought back. */
namespace store
{
    /** The Projects folder (created on first use). Tests point it somewhere else. */
    juce::File root();
    void setRootForTests (const juce::File& folder);
    juce::File sessionFolder (const juce::String& id);

    struct SessionInfo
    {
        juce::File folder;
        juce::String id, name, host, role;
        juce::Time lastUsed;
        juce::StringArray references;   // file names of the reference tracks
        int versions = 0;
        juce::int64 bytes = 0;
        bool hasState() const { return folder.getChildFile ("state.xml").existsAsFile(); }
    };

    /** Writes state.xml (atomically) and info.json for this session. */
    bool saveSession (const juce::String& id, const juce::String& stateXml, const SessionInfo& info);
    std::unique_ptr<juce::XmlElement> loadState (const juce::File& sessionFolder);
    /** Every session on disk, most recently used first. */
    std::vector<SessionInfo> listSessions();
    SessionInfo readSession (const juce::File& sessionFolder);
    /** Renames a session (info.json only). */
    bool renameSession (const juce::File& sessionFolder, const juce::String& name);
    /** The whole session folder to the trash. */
    bool removeSession (const juce::File& sessionFolder);

    struct VersionInfo
    {
        juce::File audio, meta;
        juce::String name;
        juce::Time time;
        double sampleRate = 0.0, seconds = 0.0;
        int firstSecond = 0, coveredSeconds = 0;
        juce::int64 bytes = 0;
        bool named = false;    // the user gave it a name: never removed automatically
        float score = -1.0f;   // the snapshot's score, -1: none
    };

    /** The session's saved before versions, newest first. */
    std::vector<VersionInfo> listVersions (const juce::String& id);
    /** Writes the before take of `take` as a new version (FLAC + json). Slow: call on a background
        thread. `keep`: afterwards removes the oldest unnamed versions beyond this many (0: keep all). */
    VersionInfo saveVersion (const juce::String& id, const BeforeTake& take, const juce::String& name, float score, int keep,
                             const std::atomic<bool>* cancel = nullptr);
    /** Reads a version back into the before take (resampled to the take's rate if needed). Slow. */
    bool loadVersion (const VersionInfo& v, BeforeTake& take, const std::atomic<bool>* cancel = nullptr);
    bool renameVersion (const VersionInfo& v, const juce::String& name);
    /** The FLAC and its json to the trash. */
    bool removeVersion (const VersionInfo& v);
    /** Removes the oldest unnamed versions beyond `keep`. Returns how many went. */
    int prune (const juce::String& id, int keep);

    /** Total size of all sessions on disk. */
    juce::int64 totalBytes();
    juce::String sizeText (juce::int64 bytes);
} // namespace store
} // namespace wwma
