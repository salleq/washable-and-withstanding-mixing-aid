#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace wwma::ai
{
/** The AI assistants WWMA can hand a report to. */
enum class Provider { claude, gemini, chatgpt };
constexpr int numProviders = 3;

/** How WWMA reaches the assistant.
    browser: opens the chat site, the user attaches the files (no key, nothing sent by WWMA).
    cli:     runs the assistant's own app installed on this computer (Claude Code, Gemini CLI,
             Codex CLI), signed in with the user's own account (no key).
    api:     calls the assistant's API with the user's own API key. */
enum class Mode { browser, cli, api };

juce::String providerName (Provider p);      // "Claude", "Gemini", "ChatGPT"
const char* providerId (Provider p);         // "claude", "gemini", "chatgpt"
const char* cliCommand (Provider p);         // "claude", "gemini", "codex"
juce::String cliAppName (Provider p);        // "Claude Code", "Gemini CLI", "Codex CLI"
juce::String chatUrl (Provider p);           // where the browser mode opens

struct ProviderSettings
{
    Mode mode = Mode::browser;
    juce::String apiKey;
    juce::String model;      // empty: WWMA picks one (Claude: the default below; the others: from the API's model list)
    juce::String cliPath;    // empty: found automatically
};

/** Kept in WWMA's own folder on this computer, never in the DAW project (sharing a project
    must not share an API key), never in a report. */
struct Settings
{
    Provider provider = Provider::claude;
    /** The user allowed WWMA to send the report's numbers and pictures to the chosen service
        (API and app modes). Never audio. The browser mode needs no consent: the user sends it. */
    bool consent = false;
    std::array<ProviderSettings, numProviders> providers;

    ProviderSettings& of (Provider p) { return providers[(size_t) p]; }
    const ProviderSettings& of (Provider p) const { return providers[(size_t) p]; }
    const ProviderSettings& current() const { return of (provider); }
};

/** The model used when the user has not chosen one. Empty for Gemini and ChatGPT: their
    names change often, so WWMA reads the list from the API and picks from it. */
juce::String defaultModel (Provider p);

juce::File settingsFile();
/** The self test points this to a temporary folder. */
void setSettingsFileForTests (const juce::File& f);
Settings loadSettings();
void saveSettings (const Settings& s);

/** Finds the assistant's command line app: the path the user gave, else the usual install
    places (a DAW's PATH is often not the shell's), else the PATH. */
juce::File findCli (Provider p, const juce::String& configured);
} // namespace wwma::ai
