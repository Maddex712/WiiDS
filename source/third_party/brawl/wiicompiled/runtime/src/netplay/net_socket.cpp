#include "netplay/net_socket.h"

#include "runtime_config.h"
#include "runtime_log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Netplay {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalid = INVALID_SOCKET;
void CloseNative(NativeSocket s) { closesocket(s); }

bool EnsureWinsock() {
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalid = -1;
void CloseNative(NativeSocket s) { ::close(s); }
bool EnsureWinsock() { return true; }
#endif

NativeSocket Native(intptr_t value) { return static_cast<NativeSocket>(value); }

sockaddr_in ToSockaddr(const Address& address) {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_addr.s_addr = htonl(address.ip);
    out.sin_port = htons(address.port);
    return out;
}

std::mutex g_nameMutex;
std::string g_deviceName;

} // namespace

std::string IpToString(uint32_t ip) {
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return text;
}

std::string ToString(const Address& address) {
    return IpToString(address.ip) + ":" + std::to_string(address.port);
}

bool ParseAddress(std::string_view text, uint16_t defaultPort, Address& out) {
    uint32_t parts[4] = {};
    size_t index = 0;
    size_t pos = 0;
    for (; index < 4; ++index) {
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
            return false;
        }
        uint32_t value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            value = value * 10 + static_cast<uint32_t>(text[pos++] - '0');
            if (value > 255) {
                return false;
            }
        }
        parts[index] = value;
        if (index < 3) {
            if (pos >= text.size() || text[pos] != '.') {
                return false;
            }
            ++pos;
        }
    }
    uint32_t port = defaultPort;
    if (pos < text.size()) {
        if (text[pos] != ':') {
            return false;
        }
        ++pos;
        port = 0;
        if (pos >= text.size()) {
            return false;
        }
        for (; pos < text.size(); ++pos) {
            if (text[pos] < '0' || text[pos] > '9') {
                return false;
            }
            port = port * 10 + static_cast<uint32_t>(text[pos] - '0');
            if (port > 65535) {
                return false;
            }
        }
    }
    out.ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    out.port = static_cast<uint16_t>(port);
    return out.ip != 0 && out.port != 0;
}

bool ResolveAddress(const std::string& text, uint16_t defaultPort, Address& out) {
    if (ParseAddress(text, defaultPort, out)) {
        return true;
    }
    if (!EnsureWinsock()) {
        return false;
    }
    std::string host = text;
    uint16_t port = defaultPort;
    if (const size_t colon = text.rfind(':'); colon != std::string::npos) {
        host = text.substr(0, colon);
        port = static_cast<uint16_t>(std::strtoul(text.c_str() + colon + 1, nullptr, 10));
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return false;
    }
    const auto* in = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    out.ip = ntohl(in->sin_addr.s_addr);
    out.port = port;
    freeaddrinfo(result);
    return out.Valid();
}

bool IsTailscaleIp(uint32_t ip) {
    return (ip & 0xFFC00000u) == 0x64400000u;  // 100.64.0.0/10
}

UdpSocket::~UdpSocket() {
    Close();
}

bool UdpSocket::Open(uint16_t port) {
    Close();
    if (!EnsureWinsock()) {
        return false;
    }
    const NativeSocket s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) {
        return false;
    }
    const int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef _WIN32
    // A datagram to a port nobody listens on would otherwise fail the next recvfrom with
    // WSAECONNRESET; a peer that quit is not an error for us.
    BOOL reportReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &reportReset, sizeof(reportReset), nullptr, 0, &bytes, nullptr, nullptr);
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
#ifdef _WIN32
    // Any port: leave the socket unbound and let the first send pick one. An explicit bind makes
    // Windows Firewall ask about letting the game accept connections, which only a host needs.
    if (port == 0) {
        m_socket = static_cast<intptr_t>(s);
        m_port = 0;
        m_bound = false;
        return true;
    }
#endif
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        CloseNative(s);
        return false;
    }
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &length);
    m_socket = static_cast<intptr_t>(s);
    m_port = ntohs(bound.sin_port);
    m_bound = true;
    return true;
}

void UdpSocket::Close() {
    if (m_socket != -1) {
        CloseNative(Native(m_socket));
        m_socket = -1;
        m_port = 0;
        m_bound = false;
    }
}

bool UdpSocket::IsOpen() const {
    return m_socket != -1;
}

// ---- the online server's relay (Relay in net_socket.h) ----
// ---- online play beyond the local network (Relay in net_socket.h) ----
//
// Two kinds of server, picked by the [online] server setting:
//  * "https://..." (or "http://..."): the matchmaking Worker (server/worker). Games check in with it over HTTP every
//    couple of seconds while a socket is busy, and it introduces games to each other; each game learns its own
//    public address from a STUN server through the game's own socket, then the two games send "punch" packets to
//    each other's public and local addresses until one gets through, and talk directly from then on.
//  * "host[:port]": the UDP relay (server/brawl_online_server.py), which carries every packet itself.
// Either way other games are relay addresses, 240.x.y.z (x.y.z = their id), so the lobby and session never know.
namespace {

constexpr uint8_t kRelayRegister = 0xB1, kRelayRegistered = 0xB2, kRelayForward = 0xB3, kRelayList = 0xB5,
                  kRelayListReply = 0xB6;
constexpr uint16_t kRelayVersion = 1;
constexpr uint16_t kRelayDefaultPort = 52200;
// The public matchmaking Worker, once deployed; Config.toml [online] server or BRAWL_ONLINE_SERVER overrides it.
constexpr const char* kDefaultOnlineServer = "https://brawl-online.josiahg2502.workers.dev";
constexpr const char* kStunServer = "stun.l.google.com:19302";
constexpr char kCodeLetters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // no 0/O or 1/I to mix up
constexpr uint8_t kPunchMagic[4] = {'B', 'R', 'P', 'N'};
constexpr uint32_t kStunCookie = 0x2112A442u;

struct Peer {
    Address pub, lan;          // as the matchmaker reported them
    Address direct;            // where its packets come from once a punch got through
    int64_t lastPunch = 0;
    int64_t known = 0;         // when the matchmaker first introduced it
    int64_t introduced = 0;    // when it last did (it keeps doing so while either game wants the other)
    bool warned = false;
};

struct RelayState {
    std::mutex mutex;
    bool loaded = false;
    std::string serverText;
    bool matchmaker = false;   // an http(s) server: introductions and direct connections
    Address server;            // the UDP relay
    int64_t lastResolve = -100000;
    uint32_t id = 0;
    int64_t lastRegistered = -100000;  // the server answered
    uint8_t listMax = 0, listPlayers = 0;
    std::string listName, listBuild;
    bool listRequested = false;
    std::vector<Relay::ListedRoom> listed;
    // Matchmaking.
    Address stun;
    int64_t lastStun = -100000;
    Address pub;               // this game's public address, from STUN
    Address lan;               // and on its own network (the busy socket's port)
    std::map<uint32_t, Peer> peers;
    std::map<uint32_t, int64_t> wants;  // games this one is trying to reach, by when it last tried
    int64_t lastActive = -100000;       // a socket used the network
    bool threadStarted = false;
};

RelayState& R() {
    static RelayState state;
    return state;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// This game's id: made once and kept with its settings, so it is the same game after the restart into a match.
uint32_t LoadRelayId() {
    const auto path = RuntimeConfigFile::ApplicationDataDirectory() / "Netplay" / "online_id";
    std::error_code ec;
    if (std::ifstream in(path); in) {
        uint32_t id = 0;
        if (in >> std::hex >> id && (id & 0xFFFFFFu) != 0) return id & 0xFFFFFFu;
    }
    std::random_device random;
    uint32_t id = 0;
    while ((id & 0xFFFFFFu) == 0) id = random();
    id &= 0xFFFFFFu;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream(path) << std::hex << id << "\n";
    return id;
}

void LoadRelayLocked(RelayState& r) {
    if (r.loaded) return;
    r.loaded = true;
    const char* env = std::getenv("BRAWL_ONLINE_SERVER");
    r.serverText = env != nullptr ? env : RuntimeConfigFile::OnlineServer(kDefaultOnlineServer);
    r.matchmaker = r.serverText.rfind("http://", 0) == 0 || r.serverText.rfind("https://", 0) == 0;
    r.id = LoadRelayId();
}

// The UDP relay's address, resolved now and then (a name may move); invalid without one.
Address RelayServerLocked(RelayState& r) {
    LoadRelayLocked(r);
    if (r.serverText.empty() || r.matchmaker) return {};
    const int64_t now = NowMs();
    if (!r.server.Valid() ? now - r.lastResolve > 10000 : now - r.lastResolve > 300000) {
        r.lastResolve = now;
        Address resolved;
        if (ResolveAddress(r.serverText, kRelayDefaultPort, resolved)) {
            r.server = resolved;
        } else {
            RT_LOGF(RT_TAG_RUNTIME, "online: can't find the server %s\n", r.serverText.c_str());
        }
    }
    return r.server;
}

void PutU24(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
}

uint32_t GetU24(const uint8_t* p) {
    return p[0] | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
}

void PutStr8(std::vector<uint8_t>& out, const std::string& text) {
    const size_t n = std::min<size_t>(text.size(), 63);
    out.push_back(static_cast<uint8_t>(n));
    out.insert(out.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(n));
}

bool GetStr8(const uint8_t* data, size_t size, size_t& pos, std::string& out) {
    if (pos >= size || pos + 1 + data[pos] > size) return false;
    out.assign(reinterpret_cast<const char*>(data + pos + 1), data[pos]);
    pos += 1 + data[pos];
    return true;
}

Address RelayAddressFor(uint32_t id) {
    return Address{(240u << 24) | (id & 0xFFFFFFu), 1};
}

std::string CodeFor(uint32_t id) {
    std::string code(5, 'A');
    for (int i = 4; i >= 0; --i, id >>= 5) code[static_cast<size_t>(i)] = kCodeLetters[id & 31];
    return code;
}

// One HTTP(S) POST with a text body; false on any failure. Windows: WinHTTP. Elsewhere: the system's curl, which
// SteamOS and every desktop Linux has.
bool HttpPost(const std::string& url, const std::string& body, std::string& response) {
#ifdef _WIN32
    std::wstring wide(url.begin(), url.end());
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {}, path[1024] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 1023;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return false;
    std::wstring target = std::wstring(path) + L"hello";
    if (target.size() > 5 && target[target.size() - 6] != L'/') target.insert(target.size() - 5, L"/");
    HINTERNET session = WinHttpOpen(L"BrawlRecompiled/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) return false;
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 6000);
    bool ok = false;
    if (HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0)) {
        const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        if (HINTERNET request = WinHttpOpenRequest(connect, L"POST", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags)) {
            if (WinHttpSendRequest(request, L"Content-Type: text/plain\r\n", static_cast<DWORD>(-1L),
                                   const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                                   static_cast<DWORD>(body.size()), 0) &&
                WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0, size = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                                    &size, nullptr);
                response.clear();
                char buffer[4096];
                DWORD got = 0;
                while (WinHttpReadData(request, buffer, sizeof(buffer), &got) && got > 0 && response.size() < 65536) {
                    response.append(buffer, got);
                }
                ok = status == 200;
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return ok;
#else
    for (char c : url) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && std::strchr(":/._-", c) == nullptr) return false;
    }
    char path[] = "/tmp/brawl-online-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) return false;
    const bool wrote = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
    ::close(fd);
    std::string base = url;
    if (!base.empty() && base.back() != '/') base += '/';
    const std::string command = "curl -s -m 6 -X POST --data-binary @" + std::string(path) + " '" + base + "hello'";
    bool ok = false;
    if (wrote) {
        if (FILE* pipe = popen(command.c_str(), "r")) {
            response.clear();
            char buffer[4096];
            size_t got = 0;
            while ((got = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0 && response.size() < 65536) response.append(buffer, got);
            ok = pclose(pipe) == 0 && response.rfind("ok", 0) == 0;
        }
    }
    ::unlink(path);
    return ok;
#endif
}

// The matchmaking check-in, on its own thread so a slow request never holds up the game: while a socket is busy,
// every 2 s (every 10 s once every wanted game is connected and nothing else is needed).
void MatchmakerThread() {
    int64_t lastPost = -100000;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        RelayState& r = R();
        std::string body, url;
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            const int64_t now = NowMs();
            if (!r.stun.Valid() && now - r.lastResolve > 10000) {
                r.lastResolve = now;
                r.mutex.unlock();
                Address stun;
                const bool found = ResolveAddress(kStunServer, 19302, stun);
                r.mutex.lock();
                if (found) r.stun = stun;
            }
            if (now - r.lastActive > 3000) continue;  // no room, search or match in progress
            for (auto w = r.wants.begin(); w != r.wants.end();) w = now - w->second > 30000 ? r.wants.erase(w) : std::next(w);
            bool waiting = r.listMax != 0 || r.listRequested;
            for (const auto& [id, when] : r.wants) {
                const auto peer = r.peers.find(id);
                if (peer == r.peers.end() || !peer->second.direct.Valid()) waiting = true;
            }
            if (now - lastPost < (waiting ? 2000 : 10000)) continue;
            lastPost = now;
            char line[96];
            body = "v 1\n";
            std::snprintf(line, sizeof(line), "id %06x\n", r.id);
            body += line;
            if (r.pub.Valid()) body += "pub " + IpToString(r.pub.ip) + " " + std::to_string(r.pub.port) + "\n";
            if (r.lan.Valid()) body += "lan " + IpToString(r.lan.ip) + " " + std::to_string(r.lan.port) + "\n";
            if (r.listMax != 0) {
                std::string name = r.listName;
                std::replace_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\t'; }, ' ');
                std::string build = r.listBuild;
                std::replace(build.begin(), build.end(), ' ', '_');
                body += "room " + std::to_string(r.listMax) + " " + std::to_string(r.listPlayers) + " " + build + "\t" + name + "\n";
            }
            for (const auto& [id, when] : r.wants) {
                std::snprintf(line, sizeof(line), "want %06x\n", id);
                body += line;
            }
            if (r.listRequested) {
                r.listRequested = false;
                body += "list 1\n";
            }
            url = r.serverText;
        }
        std::string response;
        if (!HttpPost(url, body, response)) {
            static int64_t lastWarning = -100000;
            if (NowMs() - lastWarning > 30000) {
                lastWarning = NowMs();
                RT_LOGF(RT_TAG_RUNTIME, "online: can't reach the matchmaking server %s\n", url.c_str());
            }
            continue;
        }
        std::lock_guard<std::mutex> lock(r.mutex);
        const int64_t now = NowMs();
        if (now - r.lastRegistered > 15000) RT_LOGF(RT_TAG_RUNTIME, "online: connected to %s\n", url.c_str());
        r.lastRegistered = now;
        std::vector<Relay::ListedRoom> rooms;
        bool listReply = body.find("list 1") != std::string::npos;
        std::istringstream lines(response);
        for (std::string text; std::getline(lines, text);) {
            const size_t tab = text.find('\t');
            std::istringstream fields(text.substr(0, tab));
            std::string kind, idText;
            fields >> kind >> idText;
            const uint32_t id = static_cast<uint32_t>(std::strtoul(idText.c_str(), nullptr, 16)) & 0xFFFFFFu;
            if (kind == "peer" && id != 0 && id != r.id) {
                std::string pubIp, lanIp;
                uint32_t pubPort = 0, lanPort = 0;
                fields >> pubIp >> pubPort >> lanIp >> lanPort;
                Peer& peer = r.peers[id];
                ParseAddress(pubIp, static_cast<uint16_t>(pubPort), peer.pub);
                ParseAddress(lanIp, static_cast<uint16_t>(lanPort), peer.lan);
                if (peer.known == 0) peer.known = now;
                peer.introduced = now;
            } else if (kind == "room" && id != 0 && id != r.id && tab != std::string::npos) {
                Relay::ListedRoom room;
                int max = 0, players = 0;
                fields >> max >> players >> room.build;
                std::replace(room.build.begin(), room.build.end(), '_', ' ');
                room.host = RelayAddressFor(id);
                room.maxPlayers = static_cast<uint8_t>(max);
                room.players = static_cast<uint8_t>(players);
                room.name = text.substr(tab + 1);
                rooms.push_back(std::move(room));
            }
        }
        if (listReply) r.listed = std::move(rooms);
    }
}

} // namespace

namespace Relay {

bool Enabled() {
    RelayState& r = R();
    std::lock_guard<std::mutex> lock(r.mutex);
    LoadRelayLocked(r);
    return !r.serverText.empty();
}

bool Connected() {
    RelayState& r = R();
    std::lock_guard<std::mutex> lock(r.mutex);
    return NowMs() - r.lastRegistered < (r.matchmaker ? 15000 : 6000);
}

bool IsRelayAddress(const Address& address) {
    return (address.ip >> 24) == 240;
}

Address OwnAddress() {
    RelayState& r = R();
    std::lock_guard<std::mutex> lock(r.mutex);
    LoadRelayLocked(r);
    return RelayAddressFor(r.id);
}

std::string Code() {
    return CodeFor(OwnAddress().ip & 0xFFFFFFu);
}

bool CodeToAddress(std::string_view code, Address& out) {
    if (ParseAddress(code, 1, out) && IsRelayAddress(out)) return true;
    uint32_t id = 0;
    int letters = 0;
    for (char c : code) {
        if (c == ' ' || c == '-') continue;
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const char* at = c != '\0' ? std::strchr(kCodeLetters, c) : nullptr;
        if (at == nullptr) return false;
        id = (id << 5) | static_cast<uint32_t>(at - kCodeLetters);
        ++letters;
    }
    if (letters != 5 || id == 0 || id > 0xFFFFFFu) return false;
    out = RelayAddressFor(id);
    return true;
}

void SetListing(uint8_t maxPlayers, uint8_t players, const std::string& name, const std::string& build) {
    RelayState& r = R();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.listMax = maxPlayers;
    r.listPlayers = players;
    r.listName = name;
    r.listBuild = build;
}

std::vector<ListedRoom> Listed() {
    RelayState& r = R();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.listed;
}

void RequestList() {
    RelayState& r = R();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.listRequested = true;
}

} // namespace Relay

// Per call on a busy socket: keep the UDP relay registration, or (matchmaking) this socket's public address fresh
// from STUN and punches going to every introduced game not reached yet.
void UdpSocket::RelayTick() {
    RelayState& r = R();
    std::vector<std::pair<Address, std::vector<uint8_t>>> out;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        LoadRelayLocked(r);
        if (r.serverText.empty()) return;
        const int64_t now = NowMs();
        r.lastActive = now;
        if (r.matchmaker) {
            if (!r.threadStarted) {
                r.threadStarted = true;
                std::thread(MatchmakerThread).detach();
            }
            if (m_bound && r.lan.port != m_port) {
                for (const auto& itf : LocalInterfaces()) {
                    if (!IsTailscaleIp(itf.ip)) {
                        r.lan = Address{itf.ip, m_port};
                        break;
                    }
                }
            }
            if (r.stun.Valid() && now - m_lastRelayRegister >= (r.pub.Valid() ? 20000 : 1000)) {
                m_lastRelayRegister = now;
                std::vector<uint8_t> request(20, 0);
                request[1] = 0x01;  // Binding Request, no attributes
                const uint32_t cookie = htonl(kStunCookie);
                std::memcpy(request.data() + 4, &cookie, 4);
                for (int i = 8; i < 20; ++i) request[static_cast<size_t>(i)] = static_cast<uint8_t>(std::rand());
                out.emplace_back(r.stun, std::move(request));
            }
            for (auto& [id, peer] : r.peers) {
                if (peer.direct.Valid() || now - peer.introduced > 10000 || now - peer.lastPunch < 250) continue;
                peer.lastPunch = now;
                std::vector<uint8_t> punch(kPunchMagic, kPunchMagic + 4);
                PutU24(punch, r.id);
                punch.push_back(0);
                for (const Address& to : {peer.pub, peer.lan}) {
                    if (to.Valid()) out.emplace_back(to, punch);
                }
                if (!peer.warned && now - peer.known > 15000) {
                    peer.warned = true;
                    RT_LOGF(RT_TAG_RUNTIME, "online: can't connect directly to %s (%s): a router between you blocks it\n",
                            CodeFor(id).c_str(), ToString(peer.pub).c_str());
                }
            }
        } else {
            const Address server = RelayServerLocked(r);
            if (server.Valid() && now - m_lastRelayRegister >= 2000) {
                m_lastRelayRegister = now;
                std::vector<uint8_t> reg{kRelayRegister, static_cast<uint8_t>(kRelayVersion), static_cast<uint8_t>(kRelayVersion >> 8)};
                PutU24(reg, r.id);
                reg.push_back(r.listMax != 0 ? 1 : 0);
                if (r.listMax != 0) {
                    reg.push_back(r.listMax);
                    reg.push_back(r.listPlayers);
                    PutStr8(reg, r.listName);
                    PutStr8(reg, r.listBuild);
                }
                out.emplace_back(server, std::move(reg));
            }
            if (server.Valid() && r.listRequested) {
                r.listRequested = false;
                out.emplace_back(server, std::vector<uint8_t>{kRelayList});
            }
        }
    }
    for (const auto& [to, packet] : out) SendRaw(to, packet.data(), packet.size());
}

bool UdpSocket::SendTo(const Address& to, const void* data, size_t size) {
    if (!IsOpen() || !to.Valid()) {
        return false;
    }
    if (Relay::IsRelayAddress(to)) {
        RelayTick();
        RelayState& r = R();
        Address direct, server;
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            const uint32_t id = to.ip & 0xFFFFFFu;
            if (r.matchmaker) {
                r.wants[id] = NowMs();  // ask the matchmaker to introduce us
                const auto peer = r.peers.find(id);
                if (peer != r.peers.end()) direct = peer->second.direct;
            } else {
                server = RelayServerLocked(r);
            }
        }
        if (direct.Valid()) return SendRaw(direct, data, size);
        if (!server.Valid() || size > 1400) return false;  // not connected yet: the lobby and session send again
        std::vector<uint8_t> packet{kRelayForward};
        PutU24(packet, to.ip & 0xFFFFFFu);
        packet.insert(packet.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        return SendRaw(server, packet.data(), packet.size());
    }
    return SendRaw(to, data, size);
}

bool UdpSocket::SendRaw(const Address& to, const void* data, size_t size) {
    const sockaddr_in target = ToSockaddr(to);
    const auto sent = ::sendto(Native(m_socket), static_cast<const char*>(data), static_cast<int>(size), 0,
                               reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    if (!m_bound && sent >= 0) {
        // The first send bound the socket to a port of the system's choosing.
        sockaddr_in bound{};
        socklen_t length = sizeof(bound);
        getsockname(Native(m_socket), reinterpret_cast<sockaddr*>(&bound), &length);
        m_port = ntohs(bound.sin_port);
        m_bound = true;
    }
    return sent == static_cast<decltype(sent)>(size);
}

// A datagram from the network: online-play traffic is handled here (STUN answers, punches, the relay's packets) and
// a packet from another game is handed on as from its relay address. Returns its size, or 0 when it was ours.
int UdpSocket::OnlinePacket(Address& from, uint8_t* bytes, int received) {
    RelayState& r = R();
    std::vector<uint8_t> ack;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.serverText.empty()) return received;
        // STUN: this socket's public address.
        if (r.stun.Valid() && from == r.stun) {
            uint32_t cookie = 0;
            if (received >= 20) std::memcpy(&cookie, bytes + 4, 4);
            if (received < 20 || bytes[0] != 0x01 || bytes[1] != 0x01 || ntohl(cookie) != kStunCookie) return 0;
            for (int pos = 20; pos + 4 <= received;) {
                const int type = (bytes[pos] << 8) | bytes[pos + 1], length = (bytes[pos + 2] << 8) | bytes[pos + 3];
                if (pos + 4 + length > received) break;
                if ((type == 0x0020 || type == 0x0001) && length >= 8 && bytes[pos + 5] == 0x01) {
                    uint16_t port = static_cast<uint16_t>((bytes[pos + 6] << 8) | bytes[pos + 7]);
                    uint32_t ip = (static_cast<uint32_t>(bytes[pos + 8]) << 24) | (bytes[pos + 9] << 16) | (bytes[pos + 10] << 8) | bytes[pos + 11];
                    if (type == 0x0020) {
                        port ^= static_cast<uint16_t>(kStunCookie >> 16);
                        ip ^= kStunCookie;
                    }
                    if (r.pub != Address{ip, port}) {
                        RT_LOGF(RT_TAG_RUNTIME, "online: this game is %s on the internet\n", ToString(Address{ip, port}).c_str());
                    }
                    r.pub = Address{ip, port};
                    break;
                }
                pos += 4 + ((length + 3) & ~3);
            }
            return 0;
        }
        // A punch from another game: it can reach us, and we now know where it is.
        if (received == 8 && std::memcmp(bytes, kPunchMagic, 4) == 0) {
            const uint32_t id = GetU24(bytes + 4);
            Peer& peer = r.peers[id];
            // The first path that gets through is kept: a later punch can come round another way (through the
            // router's public address) while packets keep arriving on the first.
            if (!peer.direct.Valid()) {
                RT_LOGF(RT_TAG_RUNTIME, "online: connected directly to %s at %s\n", CodeFor(id).c_str(),
                        ToString(from).c_str());
                peer.direct = from;
            }
            if (bytes[7] == 0) {
                ack.assign(kPunchMagic, kPunchMagic + 4);
                PutU24(ack, r.id);
                ack.push_back(1);
            }
        } else {
            for (const auto& [id, peer] : r.peers) {
                if (peer.direct.Valid() && peer.direct == from) {
                    from = RelayAddressFor(id);
                    return received;
                }
            }
            if (!r.matchmaker && r.server.Valid() && from == r.server && received >= 1) {
                if (bytes[0] == kRelayForward && received >= 4) {
                    from = RelayAddressFor(GetU24(bytes + 1));
                    std::memmove(bytes, bytes + 4, static_cast<size_t>(received - 4));
                    return received - 4;
                }
                if (bytes[0] == kRelayRegistered) {
                    if (NowMs() - r.lastRegistered > 6000) RT_LOGF(RT_TAG_RUNTIME, "online: connected to %s\n", r.serverText.c_str());
                    r.lastRegistered = NowMs();
                } else if (bytes[0] == kRelayListReply && received >= 2) {
                    const size_t size = static_cast<size_t>(received);
                    std::vector<Relay::ListedRoom> rooms;
                    size_t pos = 2;
                    for (uint8_t i = 0; i < bytes[1]; ++i) {
                        Relay::ListedRoom room;
                        if (pos + 5 > size) break;
                        room.host = RelayAddressFor(GetU24(bytes + pos));
                        room.maxPlayers = bytes[pos + 3];
                        room.players = bytes[pos + 4];
                        pos += 5;
                        if (!GetStr8(bytes, size, pos, room.name) || !GetStr8(bytes, size, pos, room.build)) break;
                        if ((room.host.ip & 0xFFFFFFu) != r.id) rooms.push_back(std::move(room));
                    }
                    r.listed = std::move(rooms);
                }
                return 0;
            }
            return received;
        }
    }
    if (!ack.empty()) SendRaw(from, ack.data(), ack.size());
    return 0;
}

int UdpSocket::ReceiveFrom(Address& from, void* buffer, size_t capacity, int timeoutMs) {
    if (!IsOpen()) {
        return -1;
    }
    RelayTick();
    if (!m_bound) {
        // Nothing can arrive before the first send gives the socket a port.
        if (timeoutMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        }
        return 0;
    }
    if (timeoutMs > 0) {
#ifdef _WIN32
        WSAPOLLFD pfd{};
        pfd.fd = Native(m_socket);
        pfd.events = POLLRDNORM;
        if (WSAPoll(&pfd, 1, timeoutMs) <= 0) {
            return 0;
        }
#else
        pollfd pfd{};
        pfd.fd = Native(m_socket);
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, timeoutMs) <= 0) {
            return 0;
        }
#endif
    }
    sockaddr_in source{};
    socklen_t length = sizeof(source);
    const auto received = ::recvfrom(Native(m_socket), static_cast<char*>(buffer), static_cast<int>(capacity), 0,
                                     reinterpret_cast<sockaddr*>(&source), &length);
    if (received < 0) {
#ifdef _WIN32
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAECONNRESET || error == WSAEMSGSIZE ? 0 : -1;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
#endif
    }
    from.ip = ntohl(source.sin_addr.s_addr);
    from.port = ntohs(source.sin_port);
    return OnlinePacket(from, static_cast<uint8_t*>(buffer), static_cast<int>(received));
}

std::vector<LocalInterface> LocalInterfaces() {
    std::vector<LocalInterface> result;
#ifdef _WIN32
    if (!EnsureWinsock()) {
        return result;
    }
    ULONG size = 16 * 1024;
    std::vector<unsigned char> storage;
    for (int attempt = 0; attempt < 3; ++attempt) {
        storage.resize(size);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        const ULONG status = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters,
            &size);
        if (status == ERROR_BUFFER_OVERFLOW) {
            continue;
        }
        if (status != NO_ERROR) {
            return result;
        }
        for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
                const auto* in = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
                if (in == nullptr || in->sin_family != AF_INET) {
                    continue;
                }
                LocalInterface entry;
                entry.ip = ntohl(in->sin_addr.s_addr);
                const uint8_t prefix = unicast->OnLinkPrefixLength;
                if (prefix > 0 && prefix < 31 && !IsTailscaleIp(entry.ip)) {
                    const uint32_t mask = 0xFFFFFFFFu << (32 - prefix);
                    entry.broadcast = (entry.ip & mask) | ~mask;
                }
                result.push_back(entry);
            }
        }
        break;
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return result;
    }
    for (ifaddrs* item = list; item != nullptr; item = item->ifa_next) {
        if (item->ifa_addr == nullptr || item->ifa_addr->sa_family != AF_INET || (item->ifa_flags & IFF_UP) == 0 ||
            (item->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        LocalInterface entry;
        entry.ip = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_addr)->sin_addr.s_addr);
        if ((item->ifa_flags & IFF_BROADCAST) != 0 && item->ifa_broadaddr != nullptr && !IsTailscaleIp(entry.ip)) {
            entry.broadcast = ntohl(reinterpret_cast<const sockaddr_in*>(item->ifa_broadaddr)->sin_addr.s_addr);
        }
        result.push_back(entry);
    }
    freeifaddrs(list);
#endif
    return result;
}

std::string DeviceName() {
    {
        std::lock_guard<std::mutex> lock(g_nameMutex);
        if (!g_deviceName.empty()) {
            return g_deviceName;
        }
    }
#ifdef _WIN32
    char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD length = sizeof(name);
    if (GetComputerNameA(name, &length) && length > 0) {
        return std::string(name, length);
    }
#else
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) == 0 && name[0] != '\0' && std::strcmp(name, "localhost") != 0) {
        return name;
    }
#endif
    return "Player";
}

void SetDeviceName(std::string name) {
    std::lock_guard<std::mutex> lock(g_nameMutex);
    g_deviceName = std::move(name);
}

} // namespace Netplay
