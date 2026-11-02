#pragma once

#include "AiSettings.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <utility>
#include <functional>
#include <vector>

namespace wwma::ai
{
/** One turn of the conversation. */
struct Turn
{
    bool user = true;
    juce::String text;
};

/** A question to an assistant's API. The first user turn carries the report and the pictures. */
struct ApiRequest
{
    Provider provider = Provider::claude;
    juce::String apiKey, model;
    juce::String context;                 // the listening file and the report, as text
    std::vector<juce::MemoryBlock> images; // PNG pictures (the spectrograms)
    std::vector<Turn> turns;              // the conversation so far; the last one is the new question
};

struct Reply
{
    bool ok = false;
    juce::String text;                    // the answer
    juce::String error;                   // what went wrong, in plain words
    juce::String model;                   // the model that answered, when known
    juce::StringArray models;             // for a model list request
};

// ---- the pieces of an API call (no network: also used by the self test)
juce::String endpoint (const ApiRequest& r);
juce::String headers (Provider p, const juce::String& apiKey, const juce::String& model);
juce::var requestBody (const ApiRequest& r);
Reply parseReply (Provider p, int status, const juce::String& body);
juce::String modelsEndpoint (Provider p);
Reply parseModels (Provider p, int status, const juce::String& body);
/** A sensible model from a list, when the user has not chosen one. */
juce::String pickModel (Provider p, const juce::StringArray& models);
/** An HTTP error turned into words, with the service's own message when it gave one. */
juce::String httpError (Provider p, int status, const juce::String& body);

// ---- the assistant's own command line app
struct CliRequest
{
    Provider provider = Provider::claude;
    juce::File exe;                // the app (found by findCli)
    juce::String prompt;           // a short instruction: read the prompt file
    juce::File folder;             // the report folder: the app may read it
    juce::File image;              // the spectrogram, for apps that take pictures as arguments
    juce::File lastMessage;        // Codex writes its final answer here
    juce::File output;             // macOS / Linux: the app's output goes to this file (no pipe to block on)
};
/** The command line, including a node runtime when the app is a node script (a DAW's PATH
    often has no node) and cmd.exe for .cmd files on Windows. */
juce::StringArray cliArguments (const CliRequest& r);

/** Runs one request at a time on its own thread; the result comes back on the message thread.
    Cancel stops the network call or the app at once. Never touches the audio thread. */
class Worker : private juce::Thread
{
public:
    Worker();
    ~Worker() override;

    using Done = std::function<void (const Reply&)>;
    bool ask (ApiRequest request, Done done);
    bool listModels (Provider p, const juce::String& apiKey, Done done);
    bool runCli (CliRequest request, Done done);
    void cancel();
    bool busy() const { return isThreadRunning(); }
    /** Called by the editor's timer: gives up on an app that runs too long. */
    void tick();
    /** Seconds the current request has been running. */
    int elapsedSeconds() const;
    /** The self test has no message loop: the result is then handed over on the worker thread. */
    void deliverOnWorkerThreadForTests() { direct = true; }

private:
    void run() override;
    bool start (std::function<Reply()> work, Done done);
    /** Status code (0: no connection) and the body. */
    std::pair<int, juce::String> http (const juce::String& url, const juce::String& hdrs, const juce::String* body, int timeoutMs);
    Reply runProcess (const CliRequest& r);

    std::function<Reply()> work;
    Done done;
    std::atomic<bool> cancelled { false };
    bool direct = false;
    std::atomic<juce::uint32> startedMs { 0 };
    juce::CriticalSection lock;
    juce::WebInputStream* stream = nullptr;
    juce::ChildProcess* process = nullptr;
    static constexpr int cliTimeoutSeconds = 600;
};
} // namespace wwma::ai
