#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>

namespace wwma::stream
{
/** "Listen anywhere": WWMA's output as a lossless (16-bit PCM) stream that a browser plays,
    e.g. on a phone. Off until the user turns it on; never started by opening a project.

    Kept in WWMA's own folder on this computer, never in the DAW project. */
struct Settings
{
    int port = 8737;
    bool localOnly = false;   // listen on 127.0.0.1 only: for a reverse proxy (Traefik) on this computer
    bool passwordOn = false;
    juce::String salt, hash;  // SHA-256 of salt + password; the password itself is never saved
    juce::String token;       // part of every link; a new one makes the old links stop working
    juce::String publicUrl;   // the user's own address (a reverse proxy), e.g. "https://mydaw.example.com/"; WWMA adds "?k=..."

    bool hasPassword() const { return hash.isNotEmpty(); }
};

juce::File settingsFile();
/** The self test points this to a temporary folder. */
void setSettingsFileForTests (const juce::File& f);
/** Gives a new token (and saves it) when the file has none. */
Settings loadSettings();
void saveSettings (const Settings& s);
juce::String hashPassword (const juce::String& salt, const juce::String& password);
juce::String newToken();
juce::String newSalt();
/** "mydaw.example.com" -> "https://mydaw.example.com/": a scheme (https when none), no query or #, ends in "/".
    Empty when it is not an http(s) address. */
juce::String normalisePublicUrl (const juce::String& text);

/** One link to the player, and where it works. */
struct Link
{
    enum class Kind { ownAddress, home, vpn, thisComputer };
    juce::String url;
    Kind kind = Kind::home;
};

/** A small HTTP server: the player page, and the audio as a chunked stream of 16-bit stereo PCM.
    The audio thread only writes into a lock-free ring (and only while someone listens);
    every listener reads it on its own thread. */
class Server
{
public:
    Server();
    ~Server();

    // ---- audio thread (prepare: before playback starts)
    void prepare (double sampleRate, int maxBlockSize);
    bool wantsAudio() const noexcept { return listeners.load (std::memory_order_relaxed) > 0; }
    /** r may be the same as l (mono). */
    void push (const float* l, const float* r, int n) noexcept;

    // ---- message thread
    enum class State { off, running, portInUse, blocked };
    /** Starts listening with these settings (stops first if running). False: see getState(). */
    bool start (const Settings& s);
    void stop();
    /** New settings (password, token) for a running server, without dropping the listeners. */
    void updateSecurity (const Settings& s);
    State getState() const noexcept { return state.load(); }
    bool isRunning() const noexcept { return getState() == State::running; }
    int getListeners() const noexcept { return listeners.load(); }
    int getPort() const noexcept { return port; }
    int getStreamRate() const noexcept { return outRate.load(); }
    std::vector<Link> getLinks() const;

    /** The ring holds this many frames (power of two): about 2.7 s at 48 kHz. */
    static constexpr int ringBits = 17, ringSize = 1 << ringBits;

private:
    class Listener;
    class Client;
    friend class Client;

    // ring of packed frames: left in the low 16 bits, right in the high 16 bits
    std::unique_ptr<std::atomic<juce::uint32>[]> ring;
    std::atomic<juce::uint64> written { 0 };
    std::atomic<int> outRate { 48000 };
    std::atomic<int> rateGeneration { 0 };
    int factor = 1, decimate = 0;
    std::array<juce::IIRFilter, 4> lowpass; // two stages per side, before going down to 48 / 44.1 kHz
    juce::HeapBlock<float> scratch;
    int scratchSize = 0;

    std::atomic<int> listeners { 0 };
    std::atomic<State> state { State::off };
    int port = 8737;
    bool localOnly = false;

    // security, read by the client threads
    mutable juce::CriticalSection securityLock;
    Settings security;
    std::atomic<int> failures { 0 };
    std::atomic<juce::uint32> failureWindowStart { 0 }, lockedUntil { 0 };

    std::unique_ptr<Listener> listener;
    juce::CriticalSection clientLock;
    juce::OwnedArray<Client> clients;
    void reapClients (bool all);

    enum class Access { ok, badToken, needPassword, locked };
    Access checkAccess (const juce::String& token, const juce::String& authorization);

    JUCE_DECLARE_NON_COPYABLE (Server)
};
} // namespace wwma::stream
