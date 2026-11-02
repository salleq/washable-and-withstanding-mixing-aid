#include "AiSettings.h"

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <sys/stat.h>
#endif

namespace wwma::ai
{
namespace
{
    juce::File& testFile()
    {
        static juce::File f;
        return f;
    }

    const char* modeId (Mode m)
    {
        switch (m)
        {
            case Mode::cli: return "cli";
            case Mode::api: return "api";
            case Mode::browser: break;
        }
        return "browser";
    }

    Mode modeFromId (const juce::String& s)
    {
        if (s == "cli") return Mode::cli;
        if (s == "api") return Mode::api;
        return Mode::browser;
    }
}

juce::String providerName (Provider p)
{
    switch (p)
    {
        case Provider::gemini:  return "Gemini";
        case Provider::chatgpt: return "ChatGPT";
        case Provider::claude:  break;
    }
    return "Claude";
}

const char* providerId (Provider p)
{
    switch (p)
    {
        case Provider::gemini:  return "gemini";
        case Provider::chatgpt: return "chatgpt";
        case Provider::claude:  break;
    }
    return "claude";
}

const char* cliCommand (Provider p)
{
    switch (p)
    {
        case Provider::gemini:  return "gemini";
        case Provider::chatgpt: return "codex";
        case Provider::claude:  break;
    }
    return "claude";
}

juce::String cliAppName (Provider p)
{
    switch (p)
    {
        case Provider::gemini:  return "Gemini CLI";
        case Provider::chatgpt: return "Codex CLI";
        case Provider::claude:  break;
    }
    return "Claude Code";
}

juce::String chatUrl (Provider p)
{
    switch (p)
    {
        case Provider::gemini:  return "https://gemini.google.com/app";
        case Provider::chatgpt: return "https://chatgpt.com/";
        case Provider::claude:  break;
    }
    return "https://claude.ai/new";
}

juce::String defaultModel (Provider p)
{
    return p == Provider::claude ? juce::String ("claude-opus-5-5") : juce::String();
}

juce::File settingsFile()
{
    if (testFile() != juce::File())
        return testFile();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("WWMA").getChildFile ("ai-settings.xml");
}

void setSettingsFileForTests (const juce::File& f) { testFile() = f; }

Settings loadSettings()
{
    Settings s;
    const auto xml = juce::XmlDocument::parse (settingsFile());
    if (xml == nullptr || ! xml->hasTagName ("WWMA_AI"))
        return s;
    const auto provider = xml->getStringAttribute ("provider");
    for (int i = 0; i < numProviders; ++i)
        if (provider == providerId ((Provider) i))
            s.provider = (Provider) i;
    s.consent = xml->getBoolAttribute ("consent", false);
    for (auto* e : xml->getChildWithTagNameIterator ("Provider"))
        for (int i = 0; i < numProviders; ++i)
            if (e->getStringAttribute ("id") == providerId ((Provider) i))
            {
                auto& p = s.providers[(size_t) i];
                p.mode = modeFromId (e->getStringAttribute ("mode"));
                p.apiKey = e->getStringAttribute ("key").trim();
                p.model = e->getStringAttribute ("model").trim();
                p.cliPath = e->getStringAttribute ("cli").trim();
            }
    return s;
}

void saveSettings (const Settings& s)
{
    juce::XmlElement xml ("WWMA_AI");
    xml.setAttribute ("provider", providerId (s.provider));
    xml.setAttribute ("consent", s.consent);
    for (int i = 0; i < numProviders; ++i)
    {
        const auto& p = s.providers[(size_t) i];
        auto* e = xml.createNewChildElement ("Provider");
        e->setAttribute ("id", providerId ((Provider) i));
        e->setAttribute ("mode", modeId (p.mode));
        e->setAttribute ("key", p.apiKey);
        e->setAttribute ("model", p.model);
        e->setAttribute ("cli", p.cliPath);
    }
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    // the file holds API keys: make it readable by this user only, before anything is written to it
    if (! file.existsAsFile())
        file.create();
   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
   #endif
    // written in place (not through a temporary file), so the file keeps its permissions
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        out.setPosition (0);
        out.truncate();
        out << xml.toString();
    }
}

juce::File findCli (Provider p, const juce::String& configured)
{
    if (configured.isNotEmpty())
    {
        const juce::File f (configured);
        return f.existsAsFile() ? f : juce::File();
    }
    const juce::String name (cliCommand (p));
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    juce::Array<juce::File> places;
   #if JUCE_WINDOWS
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    for (const auto& dir : { home.getChildFile (".local/bin"), home.getChildFile (".claude/local"), appData.getChildFile ("npm") })
        for (const auto& ext : { ".exe", ".cmd" })
            places.add (dir.getChildFile (name + ext));
   #else
    for (const auto& dir : { home.getChildFile (".local/bin"), home.getChildFile (".claude/local"), home.getChildFile (".npm-global/bin"),
                             home.getChildFile (".bun/bin"), home.getChildFile (".volta/bin"),
                             juce::File ("/opt/homebrew/bin"), juce::File ("/usr/local/bin"), juce::File ("/usr/bin") })
        places.add (dir.getChildFile (name));
    // nvm keeps each node version in its own folder
    for (const auto& v : home.getChildFile (".nvm/versions/node").findChildFiles (juce::File::findDirectories, false))
        places.add (v.getChildFile ("bin").getChildFile (name));
   #endif
    for (const auto& f : places)
        if (f.existsAsFile())
            return f;

    // the PATH, as this process sees it
    const auto path = juce::SystemStats::getEnvironmentVariable ("PATH", {});
   #if JUCE_WINDOWS
    const auto dirs = juce::StringArray::fromTokens (path, ";", "\"");
    for (const auto& d : dirs)
        for (const auto& ext : { ".exe", ".cmd" })
            if (const auto f = juce::File (d.unquoted()).getChildFile (name + ext); d.isNotEmpty() && f.existsAsFile())
                return f;
   #else
    const auto dirs = juce::StringArray::fromTokens (path, ":", "");
    for (const auto& d : dirs)
        if (const auto f = juce::File (d).getChildFile (name); d.startsWithChar ('/') && f.existsAsFile())
            return f;
   #endif
    return {};
}
} // namespace wwma::ai
