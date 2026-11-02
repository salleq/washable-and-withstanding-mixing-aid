#include "AiClient.h"

namespace wwma::ai
{
namespace
{
    constexpr int maxOutputTokens = 16000;

    const char* systemText =
        "You help a music producer improve their own mix. You can't hear audio: you read measurements from WWMA, "
        "an analyser plugin, and spectrogram pictures. Be concrete and practical, explain in plain words, "
        "say when you are unsure, and remember the producer's ears decide.";

    juce::var object() { return juce::var (new juce::DynamicObject()); }

    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto o = object();
        for (const auto& [k, v] : props)
            o.getDynamicObject()->setProperty (k, v);
        return o;
    }

    juce::String base64 (const juce::MemoryBlock& m) { return m.toBase64Encoding(); }

    /** Claude models that take adaptive thinking and the effort setting. */
    bool modernClaude (const juce::String& m)
    {
        for (const auto* prefix : { "claude-opus-5", "claude-sonnet-5", "claude-haiku-5", "claude-fable-5", "claude-mythos-5",
                                    "claude-opus-4-6", "claude-opus-4-7", "claude-opus-4-8", "claude-sonnet-4-6" })
            if (m.startsWith (prefix))
                return true;
        return false;
    }

    /** Claude models with the server-side fallback on a refusal. */
    bool claudeFallbacks (const juce::String& m)
    {
        return m == "claude-opus-5-5" || m == "claude-opus-5" || m == "claude-fable-5-1" || m == "claude-sonnet-5-5";
    }

    juce::String errorMessage (const juce::String& body)
    {
        const auto json = juce::JSON::parse (body);
        auto e = json.getProperty ("error", {});
        if (e.isString())
            return e.toString();
        auto m = e.getProperty ("message", {}).toString();
        if (m.isEmpty() && json.isArray() && json.size() > 0) // Gemini sometimes wraps it in a list
            m = json[0].getProperty ("error", {}).getProperty ("message", {}).toString();
        return m.substring (0, 400);
    }

    juce::String model (const ApiRequest& r)
    {
        return r.model.isNotEmpty() ? r.model : defaultModel (r.provider);
    }

    /** The user turn that carries the report: the pictures, the data, then the question. */
    juce::Array<juce::var> firstContent (const ApiRequest& r, const juce::String& question)
    {
        juce::Array<juce::var> parts;
        for (const auto& img : r.images)
        {
            if (r.provider == Provider::claude)
                parts.add (obj ({ { "type", "image" },
                                  { "source", obj ({ { "type", "base64" }, { "media_type", "image/png" }, { "data", base64 (img) } }) } }));
            else if (r.provider == Provider::chatgpt)
                parts.add (obj ({ { "type", "input_image" }, { "image_url", "data:image/png;base64," + base64 (img) } }));
            else
                parts.add (obj ({ { "inline_data", obj ({ { "mime_type", "image/png" }, { "data", base64 (img) } }) } }));
        }
        for (const auto& t : { r.context, question })
        {
            if (t.isEmpty())
                continue;
            if (r.provider == Provider::claude)
                parts.add (obj ({ { "type", "text" }, { "text", t } }));
            else if (r.provider == Provider::chatgpt)
                parts.add (obj ({ { "type", "input_text" }, { "text", t } }));
            else
                parts.add (obj ({ { "text", t } }));
        }
        return parts;
    }
}

juce::String endpoint (const ApiRequest& r)
{
    switch (r.provider)
    {
        case Provider::chatgpt: return "https://api.openai.com/v1/responses";
        case Provider::gemini:  return "https://generativelanguage.googleapis.com/v1beta/models/" + juce::URL::addEscapeChars (model (r), true) + ":generateContent";
        case Provider::claude:  break;
    }
    return "https://api.anthropic.com/v1/messages";
}

juce::String headers (Provider p, const juce::String& apiKey, const juce::String& m)
{
    juce::String h;
    switch (p)
    {
        case Provider::chatgpt:
            h << "Authorization: Bearer " << apiKey << "\r\n";
            break;
        case Provider::gemini:
            h << "x-goog-api-key: " << apiKey << "\r\n";
            break;
        case Provider::claude:
            h << "x-api-key: " << apiKey << "\r\n" << "anthropic-version: 2023-06-01\r\n";
            if (claudeFallbacks (m))
                h << "anthropic-beta: server-side-fallback-2026-07-01\r\n";
            break;
    }
    h << "Content-Type: application/json\r\n";
    return h;
}

juce::var requestBody (const ApiRequest& r)
{
    const auto m = model (r);
    juce::Array<juce::var> messages;
    bool first = true;
    for (const auto& t : r.turns)
    {
        if (r.provider == Provider::gemini)
        {
            juce::Array<juce::var> parts;
            if (t.user && first)
                parts = firstContent (r, t.text);
            else
                parts.add (obj ({ { "text", t.text } }));
            messages.add (obj ({ { "role", t.user ? "user" : "model" }, { "parts", parts } }));
        }
        else if (t.user && first)
            messages.add (obj ({ { "role", "user" }, { "content", firstContent (r, t.text) } }));
        else
            messages.add (obj ({ { "role", t.user ? "user" : "assistant" }, { "content", t.text } }));
        if (t.user)
            first = false;
    }

    auto body = object();
    auto* o = body.getDynamicObject();
    switch (r.provider)
    {
        case Provider::claude:
            o->setProperty ("model", m);
            o->setProperty ("max_tokens", maxOutputTokens);
            o->setProperty ("system", systemText);
            o->setProperty ("messages", messages);
            if (modernClaude (m))
            {
                o->setProperty ("thinking", obj ({ { "type", "adaptive" } }));
                o->setProperty ("output_config", obj ({ { "effort", "medium" } }));
            }
            if (claudeFallbacks (m))
                o->setProperty ("fallbacks", "default");
            break;
        case Provider::chatgpt:
            o->setProperty ("model", m);
            o->setProperty ("instructions", systemText);
            o->setProperty ("input", messages);
            o->setProperty ("max_output_tokens", maxOutputTokens);
            break;
        case Provider::gemini:
            o->setProperty ("system_instruction", obj ({ { "parts", juce::Array<juce::var> { obj ({ { "text", systemText } }) } } }));
            o->setProperty ("contents", messages);
            break;
    }
    return body;
}

juce::String httpError (Provider p, int status, const juce::String& body)
{
    const auto name = providerName (p);
    juce::String what;
    if (status == 0)
        what = "No connection to " + name + ". Check the internet connection (and that the DAW may use the network).";
    else if (status == 401 || status == 403)
        what = name + " did not accept the API key (" + juce::String (status) + "). Check the key in the AI settings.";
    else if (status == 404)
        what = name + " does not know that model (404). Pick another one in the AI settings.";
    else if (status == 429)
        what = name + " says: too many requests, or no credit left on the account (429). Try again in a moment, or check the account.";
    else if (status >= 500)
        what = name + " is busy or down right now (" + juce::String (status) + "). Try again in a moment.";
    else
        what = name + " refused the request (" + juce::String (status) + ").";
    const auto msg = errorMessage (body);
    return msg.isNotEmpty() ? what + "\n" + msg : what;
}

Reply parseReply (Provider p, int status, const juce::String& body)
{
    Reply r;
    if (status != 200)
    {
        r.error = httpError (p, status, body);
        return r;
    }
    const auto json = juce::JSON::parse (body);
    juce::String cutShort;
    switch (p)
    {
        case Provider::claude:
        {
            r.model = json.getProperty ("model", {}).toString();
            const auto stop = json.getProperty ("stop_reason", {}).toString();
            if (stop == "refusal")
            {
                r.error = "Claude declined to answer this one. Try again, or ask in the browser.";
                return r;
            }
            if (const auto* content = json.getProperty ("content", {}).getArray())
                for (const auto& block : *content)
                    if (block.getProperty ("type", {}).toString() == "text")
                        r.text << block.getProperty ("text", {}).toString();
            if (stop == "max_tokens")
                cutShort = "max";
            break;
        }
        case Provider::chatgpt:
        {
            r.model = json.getProperty ("model", {}).toString();
            if (const auto* output = json.getProperty ("output", {}).getArray())
                for (const auto& item : *output)
                    if (item.getProperty ("type", {}).toString() == "message")
                        if (const auto* content = item.getProperty ("content", {}).getArray())
                            for (const auto& c : *content)
                            {
                                const auto type = c.getProperty ("type", {}).toString();
                                if (type == "output_text")
                                    r.text << c.getProperty ("text", {}).toString();
                                else if (type == "refusal")
                                    r.error = "ChatGPT declined to answer: " + c.getProperty ("refusal", {}).toString();
                            }
            if (json.getProperty ("status", {}).toString() == "incomplete")
                cutShort = "max";
            break;
        }
        case Provider::gemini:
        {
            r.model = json.getProperty ("modelVersion", {}).toString();
            const auto blocked = json.getProperty ("promptFeedback", {}).getProperty ("blockReason", {}).toString();
            if (blocked.isNotEmpty())
            {
                r.error = "Gemini declined to answer (" + blocked + ").";
                return r;
            }
            const auto cand = json.getProperty ("candidates", {})[0];
            if (const auto* parts = cand.getProperty ("content", {}).getProperty ("parts", {}).getArray())
                for (const auto& part : *parts)
                    if (! (bool) part.getProperty ("thought", false))
                        r.text << part.getProperty ("text", {}).toString();
            const auto finish = cand.getProperty ("finishReason", {}).toString();
            if (finish == "MAX_TOKENS")
                cutShort = "max";
            else if (r.text.isEmpty() && finish.isNotEmpty() && finish != "STOP")
                r.error = "Gemini stopped without an answer (" + finish + ").";
            break;
        }
    }
    r.text = r.text.trim();
    if (r.text.isEmpty())
    {
        if (r.error.isEmpty())
            r.error = providerName (p) + " sent an empty answer. Try again.";
        return r;
    }
    if (cutShort.isNotEmpty())
        r.text << "\n\n(The answer was cut short.)";
    r.ok = r.error.isEmpty();
    return r;
}

juce::String modelsEndpoint (Provider p)
{
    switch (p)
    {
        case Provider::chatgpt: return "https://api.openai.com/v1/models";
        case Provider::gemini:  return "https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000";
        case Provider::claude:  break;
    }
    return "https://api.anthropic.com/v1/models?limit=100";
}

Reply parseModels (Provider p, int status, const juce::String& body)
{
    Reply r;
    if (status != 200)
    {
        r.error = httpError (p, status, body);
        return r;
    }
    const auto json = juce::JSON::parse (body);
    if (p == Provider::gemini)
    {
        if (const auto* list = json.getProperty ("models", {}).getArray())
            for (const auto& m : *list)
            {
                bool generates = false;
                if (const auto* methods = m.getProperty ("supportedGenerationMethods", {}).getArray())
                    for (const auto& x : *methods)
                        generates = generates || x.toString() == "generateContent";
                const auto name = m.getProperty ("name", {}).toString().fromFirstOccurrenceOf ("models/", false, false);
                if (generates && name.startsWith ("gemini"))
                    r.models.add (name);
            }
    }
    else if (const auto* list = json.getProperty ("data", {}).getArray())
        for (const auto& m : *list)
        {
            const auto id = m.getProperty ("id", {}).toString();
            if (id.isNotEmpty() && (p == Provider::claude ? id.startsWith ("claude") : id.startsWith ("gpt-")))
                r.models.add (id);
        }
    r.models.sortNatural();
    r.ok = ! r.models.isEmpty();
    if (! r.ok)
        r.error = "The key works, but " + providerName (p) + " listed no models WWMA can use.";
    return r;
}

juce::String pickModel (Provider p, const juce::StringArray& models)
{
    if (models.isEmpty())
        return defaultModel (p);
    if (p == Provider::claude)
    {
        if (models.contains (defaultModel (p)))
            return defaultModel (p);
        for (int i = models.size(); --i >= 0;)
            if (models[i].startsWith ("claude-opus"))
                return models[i];
        return models[models.size() - 1];
    }
    const juce::StringArray skip = p == Provider::chatgpt
        ? juce::StringArray { "mini", "nano", "audio", "realtime", "image", "tts", "transcribe", "search", "instruct", "codex", "oss", "chat", "pro", "deep-research" }
        : juce::StringArray { "tts", "image", "embedding", "vision", "exp", "live", "audio", "computer", "lite", "flash", "robotics" };
    auto usable = [&] (const juce::String& m, bool allowPreview)
    {
        for (const auto& s : skip)
            if (m.containsIgnoreCase (s))
                return false;
        if (! allowPreview && m.containsIgnoreCase ("preview"))
            return false;
        // dated snapshots ("gpt-5-2025-08-07"): the plain name follows the newest
        const auto tail = m.getLastCharacters (11);
        const bool dated = tail.length() == 11 && tail[0] == '-' && tail[5] == '-' && tail[8] == '-'
                           && tail.removeCharacters ("-").containsOnly ("0123456789");
        return ! dated;
    };
    for (bool allowPreview : { false, true })
        for (int i = models.size(); --i >= 0;) // sorted naturally, so the newest version is last
            if (usable (models[i], allowPreview))
                return models[i];
    return models[models.size() - 1];
}

juce::StringArray cliArguments (const CliRequest& r)
{
    juce::StringArray args;
    const auto exe = r.exe.getFullPathName();
    const auto folder = r.folder.getFullPathName();
    switch (r.provider)
    {
        case Provider::claude:
            args.addArray (juce::StringArray (exe, "-p", r.prompt, "--add-dir", folder, "--allowedTools", "Read", "--output-format", "text"));
            break;
        case Provider::gemini:
            args.addArray (juce::StringArray (exe, "-p", r.prompt, "--include-directories", folder));
            break;
        case Provider::chatgpt:
            args.addArray (juce::StringArray (exe, "exec", "--skip-git-repo-check", "--sandbox", "read-only", "-C", folder));
            if (r.image.existsAsFile())
                args.addArray (juce::StringArray ("--image", r.image.getFullPathName()));
            if (r.lastMessage != juce::File())
                args.addArray (juce::StringArray ("--output-last-message", r.lastMessage.getFullPathName()));
            args.add (r.prompt);
            break;
    }

   #if JUCE_WINDOWS
    if (r.exe.hasFileExtension ("cmd;bat"))
    {
        juce::StringArray shell ("cmd.exe", "/c");
        shell.addArray (args);
        args = shell;
    }
   #else
    // A DAW started from the dock or a menu often has a short PATH without node, which the
    // npm installed apps need. Start the app through sh with the usual places added to the PATH.
    juce::StringArray dirs { r.exe.getParentDirectory().getFullPathName(), "/opt/homebrew/bin", "/usr/local/bin" };
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    for (const auto* d : { ".local/bin", ".volta/bin", ".bun/bin", ".npm-global/bin" })
        dirs.add (home.getChildFile (d).getFullPathName());
    auto nvm = home.getChildFile (".nvm/versions/node").findChildFiles (juce::File::findDirectories, false);
    std::sort (nvm.begin(), nvm.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) > 0; });
    if (! nvm.isEmpty())
        dirs.add (nvm.getFirst().getChildFile ("bin").getFullPathName());
    juce::String path;
    for (const auto& d : dirs)
        path << "'" << d.replace ("'", "'\\''") << "':";
    juce::String script = "PATH=" + path + "\"$PATH\"; export PATH; exec \"$0\" \"$@\"";
    if (r.output != juce::File())
        script << " > '" << r.output.getFullPathName().replace ("'", "'\\''") << "' 2>&1";
    juce::StringArray shell ("/bin/sh", "-c", script);
    shell.addArray (args);
    args = shell;
   #endif
    return args;
}

// =====================================================================================
Worker::Worker() : juce::Thread ("WWMA AI") {}

Worker::~Worker()
{
    cancel();
    stopThread (5000);
}

bool Worker::start (std::function<Reply()> w, Done d)
{
    if (isThreadRunning())
        return false;
    work = std::move (w);
    done = std::move (d);
    cancelled = false;
    startedMs = juce::Time::getMillisecondCounter();
    return startThread();
}

void Worker::run()
{
    auto reply = work();
    if (cancelled.load() || threadShouldExit())
        return;
    if (direct)
    {
        if (done) done (reply);
        return;
    }
    juce::MessageManager::callAsync ([d = done, reply] { if (d) d (reply); });
}

void Worker::cancel()
{
    cancelled = true;
    const juce::ScopedLock sl (lock);
    if (stream != nullptr)
        stream->cancel();
    if (process != nullptr)
        process->kill();
}

void Worker::tick()
{
    if (! isThreadRunning())
        return;
    const juce::ScopedLock sl (lock);
    if (process != nullptr && elapsedSeconds() > cliTimeoutSeconds)
    {
        process->kill();
    }
}

int Worker::elapsedSeconds() const
{
    return isThreadRunning() ? (int) ((juce::Time::getMillisecondCounter() - startedMs.load()) / 1000) : 0;
}

std::pair<int, juce::String> Worker::http (const juce::String& url, const juce::String& hdrs, const juce::String* body, int timeoutMs)
{
    juce::URL u (url);
    if (body != nullptr)
        u = u.withPOSTData (*body);
    juce::WebInputStream s (u, body != nullptr);
    s.withExtraHeaders (hdrs).withConnectionTimeout (timeoutMs).withNumRedirectsToFollow (3);
    {
        const juce::ScopedLock sl (lock);
        stream = &s;
    }
    const bool connected = ! cancelled.load() && s.connect (nullptr);
    const int status = s.getStatusCode();
    const auto text = connected ? s.readEntireStreamAsString() : juce::String();
    {
        const juce::ScopedLock sl (lock);
        stream = nullptr;
    }
    return { status, text };
}

bool Worker::ask (ApiRequest request, Done d)
{
    return start ([this, request]
    {
        const auto body = juce::JSON::toString (requestBody (request), true);
        const auto res = http (endpoint (request), headers (request.provider, request.apiKey, request.model.isNotEmpty() ? request.model : defaultModel (request.provider)),
                               &body, 600000);
        return parseReply (request.provider, res.first, res.second);
    }, std::move (d));
}

bool Worker::listModels (Provider p, const juce::String& apiKey, Done d)
{
    return start ([this, p, apiKey]
    {
        auto h = headers (p, apiKey, {}).replace ("Content-Type: application/json\r\n", "");
        const auto res = http (modelsEndpoint (p), h, nullptr, 20000);
        return parseModels (p, res.first, res.second);
    }, std::move (d));
}

bool Worker::runCli (CliRequest request, Done d)
{
    return start ([this, request] { return runProcess (request); }, std::move (d));
}

Reply Worker::runProcess (const CliRequest& r)
{
    Reply reply;
    if (! r.exe.existsAsFile())
    {
        reply.error = cliAppName (r.provider) + " was not found on this computer. Install it and sign in once in a terminal, or set its path in the AI settings.";
        return reply;
    }
    r.lastMessage.deleteFile();
    juce::ChildProcess p;
   #if JUCE_WINDOWS
    // Windows: the pipe is read without blocking, and ends when the app does
    const auto args = cliArguments (r);
    const int flags = juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr;
   #else
    // macOS / Linux: the app writes to a file, so a cancelled app never leaves a read hanging
    // (apps start helpers of their own, which would keep a pipe open after the app is gone)
    auto withOutput = r;
    withOutput.output = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wwma-ai-out-" + juce::Uuid().toDashedString() + ".txt");
    const auto args = cliArguments (withOutput);
    const int flags = 0;
   #endif
    if (! p.start (args, flags))
    {
        reply.error = cliAppName (r.provider) + " could not be started. Some DAWs don't let plugins start other apps: then use the browser or an API key.";
        return reply;
    }
    {
        const juce::ScopedLock sl (lock);
        process = &p;
    }
    juce::MemoryOutputStream out;
   #if JUCE_WINDOWS
    char buffer[4096];
    for (;;)
    {
        const int n = p.readProcessOutput (buffer, (int) sizeof (buffer));
        if (n <= 0)
            break;
        out.write (buffer, (size_t) n);
        if (out.getDataSize() > 4 * 1024 * 1024)
        {
            p.kill();
            break;
        }
    }
    p.waitForProcessToFinish (3000);
    const auto exitCode = p.getExitCode();
   #else
    while (p.isRunning() && ! cancelled.load())
        juce::Thread::sleep (50);
    // read at once: asking JUCE again after the app has ended would lose the code
    const auto exitCode = p.getExitCode();
    if (cancelled.load())
        p.kill();
   #endif
    const bool timedOut = elapsedSeconds() > cliTimeoutSeconds;
    {
        const juce::ScopedLock sl (lock);
        process = nullptr;
    }
   #if ! JUCE_WINDOWS
    out << withOutput.output.loadFileAsString();
    withOutput.output.deleteFile();
   #endif
    const auto output = out.toString().trim();
    if (timedOut)
    {
        reply.error = cliAppName (r.provider) + " took longer than " + juce::String (cliTimeoutSeconds / 60) + " minutes and was stopped.";
        return reply;
    }
    auto answer = r.lastMessage.existsAsFile() ? r.lastMessage.loadFileAsString().trim() : output;
    if (exitCode != 0 || answer.isEmpty())
    {
        reply.error = cliAppName (r.provider) + " stopped without an answer (exit code " + juce::String (exitCode) + ")."
                      " If it asks you to sign in, run \"" + juce::String (cliCommand (r.provider)) + "\" once in a terminal."
                      + (output.isNotEmpty() ? "\n" + output.getLastCharacters (600) : juce::String());
        return reply;
    }
    reply.ok = true;
    reply.text = answer;
    reply.model = cliAppName (r.provider);
    return reply;
}
} // namespace wwma::ai
