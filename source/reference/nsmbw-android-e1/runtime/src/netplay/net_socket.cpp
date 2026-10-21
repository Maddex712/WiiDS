#include "netplay/net_socket.h"

#include "runtime_log.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
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

bool UdpSocket::SendTo(const Address& to, const void* data, size_t size) {
    if (!IsOpen() || !to.Valid()) {
        return false;
    }
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

int UdpSocket::ReceiveFrom(Address& from, void* buffer, size_t capacity, int timeoutMs) {
    if (!IsOpen()) {
        return -1;
    }
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
    return static_cast<int>(received);
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
