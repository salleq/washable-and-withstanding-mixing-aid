#include "StreamServer.h"
#include "BinaryData.h"
#include <juce_cryptography/juce_cryptography.h>

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <sys/socket.h>
 #include <sys/stat.h>
#endif

namespace wwma::stream
{
namespace
{
    juce::File& testFile()
    {
        static juce::File f;
        return f;
    }

    juce::String randomText (int length)
    {
        // no look-alike letters (0/O, 1/l/I): the link may be typed by hand on a phone
        static const char* chars = "abcdefghijkmnpqrstuvwxyz23456789";
        juce::Random rng; // seeded from the system's entropy
        rng.setSeedRandomly();
        juce::String s;
        for (int i = 0; i < length; ++i)
            s << chars[rng.nextInt (32)];
        return s;
    }

    /** Compares without stopping at the first difference (timing tells nothing). */
    bool sameText (const juce::String& a, const juce::String& b)
    {
        const auto x = a.toStdString(), y = b.toStdString();
        unsigned diff = (unsigned) (x.size() ^ y.size());
        for (size_t i = 0; i < x.size(); ++i)
            diff |= (unsigned) (unsigned char) x[i] ^ (unsigned) (unsigned char) (i < y.size() ? y[i] : 0);
        return diff == 0;
    }

    const char* findPlayer (int& size)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const char* name = BinaryData::namedResourceList[i];
            if (juce::String (BinaryData::getNamedResourceOriginalFilename (name)) == "wwma-player.html")
                return BinaryData::getNamedResource (name, size);
        }
        size = 0;
        return nullptr;
    }

    juce::String queryValue (const juce::String& query, const juce::String& key)
    {
        for (const auto& part : juce::StringArray::fromTokens (query, "&", ""))
            if (part.upToFirstOccurrenceOf ("=", false, false) == key)
                return juce::URL::removeEscapeChars (part.fromFirstOccurrenceOf ("=", false, false));
        return {};
    }

    bool isTailscale (const juce::IPAddress& a)
    {
        // CGNAT range 100.64.0.0/10, used by Tailscale (and some other VPNs)
        return a.address[0] == 100 && a.address[1] >= 64 && a.address[1] <= 127;
    }

    /** A socket that never raises SIGPIPE (which would take the whole DAW down) when the
        listener goes away mid-write. */
    void noSigPipe (juce::StreamingSocket& s)
    {
       #if JUCE_MAC || JUCE_IOS
        int on = 1;
        ::setsockopt (s.getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof (on));
       #else
        juce::ignoreUnused (s);
       #endif
    }

    int sendSome (juce::StreamingSocket& s, const char* data, int bytes)
    {
       #if JUCE_LINUX || JUCE_BSD
        return (int) ::send (s.getRawSocketHandle(), data, (size_t) bytes, MSG_NOSIGNAL);
       #else
        return s.write (data, bytes);
       #endif
    }
}

// ---- settings -----------------------------------------------------------------------------
juce::File settingsFile()
{
    if (testFile() != juce::File())
        return testFile();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("WWMA").getChildFile ("stream-settings.xml");
}

void setSettingsFileForTests (const juce::File& f) { testFile() = f; }

juce::String hashPassword (const juce::String& salt, const juce::String& password)
{
    const auto text = salt + ":" + password;
    return juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString();
}

juce::String newToken() { return randomText (10); }
juce::String newSalt() { return randomText (16); }

juce::String normalisePublicUrl (const juce::String& text)
{
    auto u = text.trim().upToFirstOccurrenceOf ("#", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (u.isEmpty() || u.containsAnyOf (" \t\r\n\"<>"))
        return {};
    if (! u.containsIgnoreCase ("://"))
        u = "https://" + u;
    const auto scheme = u.upToFirstOccurrenceOf ("://", false, false).toLowerCase();
    if (scheme != "https" && scheme != "http")
        return {};
    u = scheme + "://" + u.fromFirstOccurrenceOf ("://", false, false);
    if (u.fromFirstOccurrenceOf ("://", false, false).upToFirstOccurrenceOf ("/", false, false).isEmpty())
        return {};  // no host
    if (! u.endsWithChar ('/'))
        u << "/";
    return u;
}

Settings loadSettings()
{
    Settings s;
    if (const auto xml = juce::XmlDocument::parse (settingsFile()); xml != nullptr && xml->hasTagName ("WWMA_STREAM"))
    {
        s.port = juce::jlimit (1024, 65535, xml->getIntAttribute ("port", s.port));
        s.localOnly = xml->getBoolAttribute ("localOnly", false);
        s.passwordOn = xml->getBoolAttribute ("passwordOn", false);
        s.salt = xml->getStringAttribute ("salt");
        s.hash = xml->getStringAttribute ("hash");
        s.token = xml->getStringAttribute ("token").retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
        s.publicUrl = normalisePublicUrl (xml->getStringAttribute ("publicUrl"));
    }
    if (s.token.length() < 6)
    {
        s.token = newToken();
        saveSettings (s);
    }
    if (! s.hasPassword())
        s.passwordOn = false;
    return s;
}

void saveSettings (const Settings& s)
{
    juce::XmlElement xml ("WWMA_STREAM");
    xml.setAttribute ("port", s.port);
    xml.setAttribute ("localOnly", s.localOnly);
    xml.setAttribute ("passwordOn", s.passwordOn);
    xml.setAttribute ("salt", s.salt);
    xml.setAttribute ("hash", s.hash);
    xml.setAttribute ("token", s.token);
    xml.setAttribute ("publicUrl", s.publicUrl);
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    // the token works like a key: readable by this user only
    if (! file.existsAsFile())
        file.create();
   #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
    ::chmod (file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
   #endif
    juce::FileOutputStream out (file);
    if (out.openedOk())
    {
        out.setPosition (0);
        out.truncate();
        out << xml.toString();
    }
}

// ---- one connection --------------------------------------------------------------------------
class Server::Client : public juce::Thread
{
public:
    Client (Server& s, juce::StreamingSocket* sock) : juce::Thread ("WWMA stream client"), server (s), socket (sock)
    {
        noSigPipe (*socket);
    }
    ~Client() override { stopThread (3000); }

    void run() override
    {
        handle();
        socket->close();
        finished = true;
    }

    std::atomic<bool> finished { false };

private:
    Server& server;
    std::unique_ptr<juce::StreamingSocket> socket;

    bool sendAll (const void* data, int bytes)
    {
        auto* p = static_cast<const char*> (data);
        int stalls = 0;
        while (bytes > 0)
        {
            if (threadShouldExit())
                return false;
            const int ready = socket->waitUntilReady (false, 200);
            if (ready < 0)
                return false;
            if (ready == 0)
            {
                if (++stalls > 50) // 10 s without the listener taking anything
                    return false;
                continue;
            }
            const int n = sendSome (*socket, p, bytes);
            if (n <= 0)
                return false;
            p += n;
            bytes -= n;
            stalls = 0;
        }
        return true;
    }

    bool sendText (const juce::String& s) { return sendAll (s.toRawUTF8(), (int) s.getNumBytesAsUTF8()); }

    void respond (const juce::String& status, const juce::String& type, const void* body, int size, const juce::String& extra = {})
    {
        sendText ("HTTP/1.1 " + status + "\r\n"
                  "Content-Type: " + type + "\r\n"
                  "Content-Length: " + juce::String (size) + "\r\n"
                  "Cache-Control: no-store\r\n"
                  "X-Content-Type-Options: nosniff\r\n"
                  "Referrer-Policy: no-referrer\r\n"
                  + extra +
                  "Connection: close\r\n\r\n");
        if (size > 0)
            sendAll (body, size);
    }

    void respondText (const juce::String& status, const juce::String& text, const juce::String& extra = {})
    {
        respond (status, "text/plain; charset=utf-8", text.toRawUTF8(), (int) text.getNumBytesAsUTF8(), extra);
    }

    void handle()
    {
        // the request line and headers (nothing else is read: only GET is served)
        juce::MemoryOutputStream request;
        char buf[2048];
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (request.getDataSize() < 16384)
        {
            if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline)
                return;
            const int ready = socket->waitUntilReady (true, 200);
            if (ready < 0)
                return;
            if (ready == 0)
                continue;
            const int n = socket->read (buf, (int) sizeof (buf), false);
            if (n <= 0)
                return;
            request.write (buf, (size_t) n);
            if (request.toString().contains ("\r\n\r\n"))
                break;
        }
        const auto text = request.toString();
        const auto lines = juce::StringArray::fromLines (text.upToFirstOccurrenceOf ("\r\n\r\n", false, false));
        if (lines.isEmpty())
            return;
        const auto first = juce::StringArray::fromTokens (lines[0], " ", "");
        if (first.size() < 2 || first[0] != "GET")
        {
            respondText ("405 Method Not Allowed", "Only GET.\n", "Allow: GET\r\n");
            return;
        }
        const auto target = first[1];
        const auto path = target.upToFirstOccurrenceOf ("?", false, false);
        const auto query = target.fromFirstOccurrenceOf ("?", false, false);
        juce::String authorization;
        for (int i = 1; i < lines.size(); ++i)
            if (lines[i].upToFirstOccurrenceOf (":", false, false).trim().equalsIgnoreCase ("Authorization"))
                authorization = lines[i].fromFirstOccurrenceOf (":", false, false).trim();

        if (path == "/favicon.ico" || path == "/robots.txt")
        {
            respondText (path == "/robots.txt" ? "200 OK" : "404 Not Found", path == "/robots.txt" ? "User-agent: *\nDisallow: /\n" : "");
            return;
        }

        switch (server.checkAccess (queryValue (query, "k"), authorization))
        {
            case Access::ok: break;
            case Access::locked:
                respondText ("429 Too Many Requests", "Too many wrong tries. Wait a minute. / Liian monta väärää yritystä. Odota minuutti.\n", "Retry-After: 60\r\n");
                return;
            case Access::badToken:
                respondText ("403 Forbidden", "This link is not valid. Copy the link again from WWMA's settings.\n"
                                              "Linkki ei kelpaa. Kopioi linkki uudelleen WWMA:n asetuksista.\n");
                return;
            case Access::needPassword:
                respondText ("401 Unauthorized", "Password needed. / Salasana tarvitaan.\n", "WWW-Authenticate: Basic realm=\"WWMA\", charset=\"UTF-8\"\r\n");
                return;
        }

        if (path == "/" || path == "/index.html")
        {
            int size = 0;
            const char* page = findPlayer (size);
            respond ("200 OK", "text/html; charset=utf-8", page, size,
                     "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline' blob:; style-src 'unsafe-inline'; img-src 'self' data:; media-src 'self' blob:\r\n");
        }
        else if (path == "/info")
        {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("sampleRate", server.outRate.load());
            o->setProperty ("channels", 2);
            o->setProperty ("bits", 16);
            o->setProperty ("name", "WWMA");
            const auto json = juce::JSON::toString (juce::var (o.get()), true);
            respond ("200 OK", "application/json; charset=utf-8", json.toRawUTF8(), (int) json.getNumBytesAsUTF8());
        }
        else if (path == "/stream")
            stream();
        else
            respondText ("404 Not Found", "Not found.\n");
    }

    bool sendChunk (const void* data, int bytes)
    {
        return sendText (juce::String::toHexString (bytes) + "\r\n") && sendAll (data, bytes) && sendText ("\r\n");
    }

    void stream()
    {
        if (! sendText ("HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "Cache-Control: no-store\r\n"
                        "X-Accel-Buffering: no\r\n"   // proxies: pass it on as it comes
                        "X-Content-Type-Options: nosniff\r\n"
                        "Transfer-Encoding: chunked\r\n"
                        "Connection: close\r\n\r\n"))
            return;

        struct Count
        {
            explicit Count (std::atomic<int>& c) : n (c) { ++n; }
            ~Count() { --n; }
            std::atomic<int>& n;
        } count (server.listeners);

        const int generation = server.rateGeneration.load();
        const int rate = server.outRate.load();
        constexpr int maxFrames = 2048;
        std::vector<char> bytes ((size_t) maxFrames * 4);
        // start just behind "now"
        juce::uint64 pos = server.written.load (std::memory_order_acquire);
        auto lastData = juce::Time::getMillisecondCounterHiRes();
        double silenceStart = -1.0;
        juce::int64 silenceSent = 0;

        while (! threadShouldExit() && server.rateGeneration.load() == generation)
        {
            const auto w = server.written.load (std::memory_order_acquire);
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (w - pos > (juce::uint64) (ringSize - 8192))
                pos = w - (juce::uint64) (rate / 10); // fell far behind: skip ahead
            const auto avail = (int) juce::jmin<juce::uint64> (w - pos, (juce::uint64) maxFrames);
            if (avail >= 128)
            {
                for (int i = 0; i < avail; ++i)
                {
                    const auto v = server.ring[(size_t) ((pos + (juce::uint64) i) & (juce::uint64) (ringSize - 1))].load (std::memory_order_relaxed);
                    auto* b = bytes.data() + i * 4;
                    b[0] = (char) (v & 0xff);
                    b[1] = (char) ((v >> 8) & 0xff);
                    b[2] = (char) ((v >> 16) & 0xff);
                    b[3] = (char) ((v >> 24) & 0xff);
                }
                pos += (juce::uint64) avail;
                if (! sendChunk (bytes.data(), avail * 4))
                    return;
                lastData = now;
                silenceStart = -1.0;
            }
            else if (now - lastData > 300.0)
            {
                // the host is not running the plugin (stopped, or exporting): keep the player
                // going with silence at the real pace, so it does not give up or stutter
                if (silenceStart < 0.0)
                {
                    silenceStart = now;
                    silenceSent = 0;
                }
                const auto due = (juce::int64) ((now - silenceStart) * rate / 1000.0) - silenceSent;
                if (due >= 256)
                {
                    const int frames = (int) juce::jmin<juce::int64> (due, maxFrames);
                    std::fill (bytes.begin(), bytes.begin() + frames * 4, (char) 0);
                    if (! sendChunk (bytes.data(), frames * 4))
                        return;
                    silenceSent += frames;
                }
                pos = w;
                wait (10);
            }
            else
                wait (4);
        }
        sendText ("0\r\n\r\n");
    }
};

// ---- the listening socket ------------------------------------------------------------------
class Server::Listener : public juce::Thread
{
public:
    explicit Listener (Server& s) : juce::Thread ("WWMA stream server"), server (s) {}
    ~Listener() override { stopThread (3000); }

    juce::StreamingSocket socket;

    void run() override
    {
        while (! threadShouldExit())
        {
            server.reapClients (false);
            if (socket.waitUntilReady (true, 200) != 1)
                continue;
            std::unique_ptr<juce::StreamingSocket> s (socket.waitForNextConnection());
            if (s == nullptr)
                continue;
            const juce::ScopedLock sl (server.clientLock);
            if (server.clients.size() >= 12)
                continue; // closed when it goes out of scope
            auto* c = server.clients.add (new Client (server, s.release()));
            c->startThread();
        }
    }

private:
    Server& server;
};

// ---- the server -------------------------------------------------------------------------------
Server::Server()
    : ring (new std::atomic<juce::uint32>[(size_t) ringSize])
{
    for (int i = 0; i < ringSize; ++i)
        ring[(size_t) i].store (0, std::memory_order_relaxed);
}

Server::~Server() { stop(); }

void Server::prepare (double sampleRate, int maxBlockSize)
{
    factor = sampleRate > 100000.0 ? 4 : sampleRate > 50000.0 ? 2 : 1;
    const int rate = juce::roundToInt (sampleRate / factor);
    if (rate != outRate.load())
    {
        outRate.store (rate);
        ++rateGeneration; // the players reconnect and read the new rate
    }
    decimate = 0;
    // 4th order Butterworth low pass just under the new Nyquist frequency
    const double cutoff = 0.45 * rate;
    const double qs[] = { 0.5412, 1.3066 };
    for (size_t i = 0; i < lowpass.size(); ++i)
    {
        lowpass[i].setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, cutoff, qs[i % 2]));
        lowpass[i].reset();
    }
    scratchSize = juce::jmax (512, maxBlockSize);
    scratch.allocate ((size_t) scratchSize * 2, true);
}

void Server::push (const float* l, const float* r, int n) noexcept
{
    auto q = [] (float x)
    {
        x = juce::jlimit (-1.0f, 1.0f, x);
        return (juce::uint32) (juce::uint16) (juce::int16) juce::roundToInt (x * 32767.0f);
    };
    auto w = written.load (std::memory_order_relaxed);
    const auto mask = (juce::uint64) (ringSize - 1);
    if (factor == 1)
    {
        for (int i = 0; i < n; ++i)
            ring[(size_t) (w++ & mask)].store (q (l[i]) | (q (r[i]) << 16), std::memory_order_relaxed);
    }
    else if (scratchSize > 0)
    {
        for (int start = 0; start < n; start += scratchSize)
        {
            const int len = juce::jmin (scratchSize, n - start);
            float* sl = scratch.get();
            float* sr = scratch.get() + scratchSize;
            std::copy (l + start, l + start + len, sl);
            std::copy (r + start, r + start + len, sr);
            lowpass[0].processSamples (sl, len);
            lowpass[1].processSamples (sl, len);
            lowpass[2].processSamples (sr, len);
            lowpass[3].processSamples (sr, len);
            for (int i = 0; i < len; ++i)
                if (++decimate >= factor)
                {
                    decimate = 0;
                    ring[(size_t) (w++ & mask)].store (q (sl[i]) | (q (sr[i]) << 16), std::memory_order_relaxed);
                }
        }
    }
    written.store (w, std::memory_order_release);
}

bool Server::start (const Settings& s)
{
    stop();
    port = s.port;
    localOnly = s.localOnly;
    updateSecurity (s);
    failures = 0;
    lockedUntil = 0;

    auto l = std::make_unique<Listener> (*this);
    if (! l->socket.createListener (port, localOnly ? juce::String ("127.0.0.1") : juce::String()))
    {
        // something answers on the port (another WWMA, or another app), or this host / the system
        // does not let a plugin open a port at all
        juce::StreamingSocket probe;
        state = probe.connect ("127.0.0.1", port, 300) ? State::portInUse : State::blocked;
        return false;
    }
    listener = std::move (l);
    listener->startThread();
    state = State::running;
    return true;
}

void Server::stop()
{
    if (listener != nullptr)
    {
        listener->signalThreadShouldExit();
        listener->stopThread (3000);
        listener->socket.close();
        listener.reset();
    }
    reapClients (true);
    state = State::off;
}

void Server::updateSecurity (const Settings& s)
{
    const juce::ScopedLock sl (securityLock);
    security = s;
}

void Server::reapClients (bool all)
{
    juce::OwnedArray<Client> done;
    {
        const juce::ScopedLock sl (clientLock);
        for (int i = clients.size(); --i >= 0;)
            if (all || clients[i]->finished.load())
                done.add (clients.removeAndReturn (i));
    }
    for (auto* c : done)
        c->signalThreadShouldExit();
    done.clear(); // each waits for its thread
}

Server::Access Server::checkAccess (const juce::String& token, const juce::String& authorization)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now < lockedUntil.load())
        return Access::locked;

    Settings sec;
    {
        const juce::ScopedLock sl (securityLock);
        sec = security;
    }
    auto failed = [this, now] (Access a)
    {
        // more than 10 wrong tries in a minute: nobody gets in for a minute
        if (now - failureWindowStart.load() > 60000)
        {
            failureWindowStart = now;
            failures = 0;
        }
        if (++failures > 10)
            lockedUntil = now + 60000;
        juce::Thread::sleep (400);
        return a;
    };

    if (! sameText (token, sec.token))
        return failed (Access::badToken);
    if (sec.passwordOn && sec.hasPassword())
    {
        if (! authorization.startsWithIgnoreCase ("Basic "))
            return Access::needPassword; // the browser's first try never has it: not counted
        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, authorization.substring (6).trim()))
            return failed (Access::needPassword);
        const auto password = decoded.toUTF8().fromFirstOccurrenceOf (":", false, false); // any user name
        if (! sameText (hashPassword (sec.salt, password), sec.hash))
            return failed (Access::needPassword);
    }
    return Access::ok;
}

std::vector<Link> Server::getLinks() const
{
    Settings sec;
    {
        const juce::ScopedLock sl (securityLock);
        sec = security;
    }
    std::vector<Link> links;
    // the user's own address first: that is the one to share
    if (const auto own = normalisePublicUrl (sec.publicUrl); own.isNotEmpty())
        links.push_back ({ own + "?k=" + sec.token, Link::Kind::ownAddress });
    const auto tail = ":" + juce::String (port) + "/?k=" + sec.token;
    if (! localOnly)
        for (const auto& a : juce::IPAddress::getAllAddresses (false))
        {
            // not loopback, link-local (no DHCP), multicast or reserved: nobody can connect there
            if (a.isNull() || a.address[0] == 0 || a.address[0] == 127 || a.address[0] >= 224
                || (a.address[0] == 169 && a.address[1] == 254))
                continue;
            links.push_back ({ "http://" + a.toString() + tail, isTailscale (a) ? Link::Kind::vpn : Link::Kind::home });
        }
    links.push_back ({ "http://127.0.0.1" + tail, Link::Kind::thisComputer });
    return links;
}
} // namespace wwma::stream
