#include "VersionStore.h"
#include <algorithm>

namespace wwma::store
{
namespace
{
    juce::File& rootOverride()
    {
        static juce::File f;
        return f;
    }

    juce::var readJson (const juce::File& f)
    {
        return f.existsAsFile() ? juce::JSON::parse (f.loadFileAsString()) : juce::var();
    }

    /** Writes next to the file first, then swaps it in, so a crash never leaves half a file. */
    bool writeAtomically (const juce::File& f, const juce::String& text)
    {
        f.getParentDirectory().createDirectory();
        juce::TemporaryFile tmp (f);
        if (! tmp.getFile().replaceWithText (text, false, false, "\n"))
            return false;
        return tmp.overwriteTargetFileWithTemporary();
    }

    /** To the trash when the system has one, otherwise deleted. */
    bool trash (const juce::File& f)
    {
        if (! f.exists())
            return true;
        if (f.moveToTrash())
            return true;
        return f.isDirectory() ? f.deleteRecursively() : f.deleteFile();
    }

    juce::File versionsFolder (const juce::String& id) { return sessionFolder (id).getChildFile ("Versions"); }

    juce::int64 folderBytes (const juce::File& folder)
    {
        juce::int64 total = 0;
        for (const auto& e : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
            total += e.getFileSize();
        return total;
    }

    VersionInfo readVersion (const juce::File& meta)
    {
        VersionInfo v;
        v.meta = meta;
        v.audio = meta.withFileExtension ("flac");
        const auto j = readJson (meta);
        v.name = j.getProperty ("name", "").toString();
        v.time = juce::Time ((juce::int64) j.getProperty ("time", 0));
        v.sampleRate = (double) j.getProperty ("rate", 0.0);
        v.firstSecond = (int) j.getProperty ("first", 0);
        v.seconds = (double) j.getProperty ("seconds", 0.0);
        v.coveredSeconds = j.getProperty ("covered", "").toString().removeCharacters ("0").length();
        v.named = (bool) j.getProperty ("named", false);
        v.score = (float) (double) j.getProperty ("score", -1.0);
        v.bytes = v.audio.getSize() + meta.getSize();
        return v;
    }

    juce::AudioFormatManager& formats()
    {
        static juce::AudioFormatManager m;
        static bool ready = false;
        if (! ready)
        {
            m.registerBasicFormats();
            ready = true;
        }
        return m;
    }
}

juce::File root()
{
    auto f = rootOverride() != juce::File()
                 ? rootOverride()
                 : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WWMA").getChildFile ("Projects");
    f.createDirectory();
    return f;
}

void setRootForTests (const juce::File& folder) { rootOverride() = folder; }

juce::File sessionFolder (const juce::String& id)
{
    // only letters, digits and dashes: an id never points outside the folder
    return root().getChildFile (id.retainCharacters ("0123456789abcdefABCDEF-"));
}

bool saveSession (const juce::String& id, const juce::String& stateXml, const SessionInfo& info)
{
    if (id.isEmpty())
        return false;
    const auto folder = sessionFolder (id);
    if (! folder.createDirectory())
        return false;

    // keep a name the user gave, unless a new one is passed
    auto* obj = new juce::DynamicObject();
    const auto old = readJson (folder.getChildFile ("info.json"));
    obj->setProperty ("id", id);
    obj->setProperty ("name", info.name.isNotEmpty() ? info.name : old.getProperty ("name", "").toString());
    obj->setProperty ("lastUsed", juce::Time::getCurrentTime().toMilliseconds());
    obj->setProperty ("host", info.host);
    obj->setProperty ("role", info.role);
    juce::Array<juce::var> refs;
    for (const auto& r : info.references)
        refs.add (r);
    obj->setProperty ("references", refs);
    return writeAtomically (folder.getChildFile ("state.xml"), stateXml)
        && writeAtomically (folder.getChildFile ("info.json"), juce::JSON::toString (juce::var (obj)));
}

std::unique_ptr<juce::XmlElement> loadState (const juce::File& folder)
{
    return juce::XmlDocument::parse (folder.getChildFile ("state.xml"));
}

SessionInfo readSession (const juce::File& folder)
{
    SessionInfo s;
    s.folder = folder;
    const auto j = readJson (folder.getChildFile ("info.json"));
    s.id = folder.getFileName();
    s.name = j.getProperty ("name", "").toString();
    s.host = j.getProperty ("host", "").toString();
    s.role = j.getProperty ("role", "").toString();
    s.lastUsed = juce::Time ((juce::int64) j.getProperty ("lastUsed", 0));
    if (s.lastUsed.toMilliseconds() == 0)
        s.lastUsed = folder.getChildFile ("state.xml").getLastModificationTime();
    if (const auto* refs = j.getProperty ("references", {}).getArray())
        for (const auto& r : *refs)
            s.references.add (r.toString());
    s.versions = folder.getChildFile ("Versions").getNumberOfChildFiles (juce::File::findFiles, "*.json");
    s.bytes = folderBytes (folder);
    return s;
}

std::vector<SessionInfo> listSessions()
{
    std::vector<SessionInfo> out;
    for (const auto& e : juce::RangedDirectoryIterator (root(), false, "*", juce::File::findDirectories))
        out.push_back (readSession (e.getFile()));
    std::sort (out.begin(), out.end(), [] (const SessionInfo& a, const SessionInfo& b) { return a.lastUsed > b.lastUsed; });
    return out;
}

bool renameSession (const juce::File& folder, const juce::String& name)
{
    auto j = readJson (folder.getChildFile ("info.json"));
    if (! j.isObject())
        j = juce::var (new juce::DynamicObject());
    j.getDynamicObject()->setProperty ("name", name);
    return writeAtomically (folder.getChildFile ("info.json"), juce::JSON::toString (j));
}

bool removeSession (const juce::File& folder)
{
    // never anything outside WWMA's own folder
    if (! folder.isAChildOf (root()))
        return false;
    return trash (folder);
}

std::vector<VersionInfo> listVersions (const juce::String& id)
{
    std::vector<VersionInfo> out;
    if (id.isEmpty())
        return out;
    const auto folder = versionsFolder (id);
    if (! folder.isDirectory())
        return out;
    for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*.json", juce::File::findFiles))
    {
        auto v = readVersion (e.getFile());
        if (v.audio.existsAsFile())
            out.push_back (std::move (v));
    }
    std::sort (out.begin(), out.end(), [] (const VersionInfo& a, const VersionInfo& b) { return a.time > b.time; });
    return out;
}

VersionInfo saveVersion (const juce::String& id, const BeforeTake& take, const juce::String& name, float score, int keep,
                         const std::atomic<bool>* cancel)
{
    VersionInfo v;
    const auto [first, last] = take.beforeSpan();
    const double rate = take.getSampleRate();
    const int len = take.getChunkLength();
    if (id.isEmpty() || first < 0 || rate <= 0.0)
        return v;

    const auto folder = versionsFolder (id);
    folder.createDirectory();
    const auto now = juce::Time::getCurrentTime();
    const auto base = folder.getNonexistentChildFile (now.formatted ("%Y-%m-%d %H-%M-%S"), ".flac").getFileNameWithoutExtension();
    v.audio = folder.getChildFile (base + ".flac");
    v.meta = folder.getChildFile (base + ".json");

    std::unique_ptr<juce::AudioFormatWriter> writer;
    {
        juce::FlacAudioFormat flac;
        auto stream = v.audio.createOutputStream();
        if (stream == nullptr)
            return {};
        std::unique_ptr<juce::OutputStream> out (stream.release());
        writer.reset (flac.createWriterFor (out.get(), rate, 2, 24, {}, 5));
        if (writer == nullptr)
            return {};
        out.release(); // the writer owns the stream now
    }

    juce::AudioBuffer<float> sec (2, len);
    juce::String covered;
    bool ok = true;
    for (int s = first; s <= last && ok; ++s)
    {
        if (cancel != nullptr && cancel->load())
            ok = false;
        else
        {
            const bool has = take.readBeforeSecond (s, sec.getWritePointer (0), sec.getWritePointer (1));
            if (! has)
                sec.clear();
            covered << (has ? "1" : "0");
            ok = writer->writeFromAudioSampleBuffer (sec, 0, len);
        }
    }
    writer.reset();
    if (! ok)
    {
        v.audio.deleteFile();
        return {};
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", name);
    obj->setProperty ("named", name.isNotEmpty());
    obj->setProperty ("time", now.toMilliseconds());
    obj->setProperty ("rate", rate);
    obj->setProperty ("first", first);
    obj->setProperty ("seconds", (double) (last - first + 1));
    obj->setProperty ("covered", covered);
    obj->setProperty ("score", score);
    if (! writeAtomically (v.meta, juce::JSON::toString (juce::var (obj))))
    {
        v.audio.deleteFile();
        return {};
    }
    if (keep > 0)
        prune (id, keep);
    return readVersion (v.meta);
}

bool loadVersion (const VersionInfo& v, BeforeTake& take, const std::atomic<bool>* cancel)
{
    const double hostRate = take.getSampleRate();
    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (v.audio));
    if (reader == nullptr || hostRate <= 0.0)
        return false;
    const auto covered = readJson (v.meta).getProperty ("covered", "").toString();

    // played at the host rate: resample while reading if the version was saved at another one
    juce::AudioFormatReaderSource source (reader.get(), false);
    juce::ResamplingAudioSource resampler (&source, false, 2);
    resampler.setResamplingRatio (reader->sampleRate / hostRate);
    const int len = take.getChunkLength();
    resampler.prepareToPlay (len, hostRate);

    juce::AudioBuffer<float> sec (2, len);
    const int seconds = (int) std::ceil ((double) reader->lengthInSamples / reader->sampleRate - 1.0e-6);
    take.beginImport();
    bool ok = true;
    for (int s = 0; s < seconds && ok; ++s)
    {
        if (cancel != nullptr && cancel->load())
        {
            ok = false;
            break;
        }
        sec.clear();
        juce::AudioSourceChannelInfo info (&sec, 0, len);
        resampler.getNextAudioBlock (info);
        if (reader->numChannels == 1)
            sec.copyFrom (1, 0, sec, 0, 0, len);
        const bool has = s >= covered.length() || covered[s] != '0';
        if (has)
            take.importSecond (v.firstSecond + s, sec.getReadPointer (0), sec.getReadPointer (1), len);
    }
    take.endImport();
    return ok && take.hasBefore();
}

bool renameVersion (const VersionInfo& v, const juce::String& name)
{
    auto j = readJson (v.meta);
    if (! j.isObject())
        return false;
    j.getDynamicObject()->setProperty ("name", name);
    j.getDynamicObject()->setProperty ("named", name.isNotEmpty());
    return writeAtomically (v.meta, juce::JSON::toString (j));
}

bool removeVersion (const VersionInfo& v)
{
    const auto r = root();
    if (! v.meta.isAChildOf (r) || ! v.audio.isAChildOf (r))
        return false;
    const bool a = trash (v.audio);
    const bool m = trash (v.meta);
    return a && m;
}

int prune (const juce::String& id, int keep)
{
    int removed = 0, kept = 0;
    for (const auto& v : listVersions (id))   // newest first
    {
        if (v.named || kept < keep)
        {
            kept += v.named ? 0 : 1;
            continue;
        }
        if (removeVersion (v))
            ++removed;
    }
    return removed;
}

juce::int64 totalBytes()
{
    return folderBytes (root());
}

juce::String sizeText (juce::int64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return juce::String ((double) bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
    if (bytes >= 1024LL * 1024)
        return juce::String ((double) bytes / (1024.0 * 1024.0), 0) + " MB";
    return juce::String (juce::jmax ((juce::int64) 1, bytes / 1024)) + " kB";
}
} // namespace wwma::store
