#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A small UDP layer for network play, on Winsock and BSD sockets alike. IPv4 only: LAN and
// Tailscale's 100.64.0.0/10 addresses are all the game needs.
namespace Netplay {

// The well-known port rooms are discovered and played on.
inline constexpr uint16_t kDefaultPort = 52130;

struct Address {
    uint32_t ip = 0;    // host byte order
    uint16_t port = 0;  // host byte order

    bool Valid() const { return ip != 0 && port != 0; }
    bool operator==(const Address& other) const { return ip == other.ip && port == other.port; }
    bool operator!=(const Address& other) const { return !(*this == other); }
};

std::string ToString(const Address& address);
std::string IpToString(uint32_t ip);
// "a.b.c.d" or "a.b.c.d:port" (numeric only); `defaultPort` when no port is given.
bool ParseAddress(std::string_view text, uint16_t defaultPort, Address& out);
// Numeric address, or a host name through the system resolver (Tailscale MagicDNS names work while
// Tailscale is up). Blocks; call from the network thread.
bool ResolveAddress(const std::string& text, uint16_t defaultPort, Address& out);
// 100.64.0.0/10, where Tailscale numbers its devices.
bool IsTailscaleIp(uint32_t ip);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds 0.0.0.0:port (0 = any free port), broadcast allowed. False if the port is taken.
    bool Open(uint16_t port);
    void Close();
    bool IsOpen() const;
    uint16_t LocalPort() const { return m_port; }

    bool SendTo(const Address& to, const void* data, size_t size);
    // One datagram, waiting up to timeoutMs (0 = just poll). Returns its size, 0 when nothing came,
    // -1 on a socket error.
    int ReceiveFrom(Address& from, void* buffer, size_t capacity, int timeoutMs);

private:
    bool SendRaw(const Address& to, const void* data, size_t size);
    void RelayTick();
    int OnlinePacket(Address& from, uint8_t* bytes, int received);

    intptr_t m_socket = -1;
    uint16_t m_port = 0;
    bool m_bound = false;
    int64_t m_lastRelayRegister = 0;  // steady-clock milliseconds
};

// Online play over the internet. Every game has its own id; other games are reached at relay addresses, 240.x.y.z,
// which UdpSocket maps to them. The server ([online] server in Config.toml, or BRAWL_ONLINE_SERVER) is either the
// free matchmaking Worker (an https:// address, server/worker), which introduces games so they connect directly, or
// the UDP relay (host:port, server/brawl_online_server.py), which carries their packets. Either keeps the public
// room list.
namespace Relay {

bool Enabled();    // a server is set
bool Connected();  // it answered in the last few seconds
bool IsRelayAddress(const Address& address);
Address OwnAddress();  // this game, as other games reach it
// This game's room code: its relay address as 5 letters and digits. CodeToAddress also takes "240.x.y.z".
std::string Code();
bool CodeToAddress(std::string_view code, Address& out);

// List this game's room publicly while hosting (maxPlayers 0: not listed).
void SetListing(uint8_t maxPlayers, uint8_t players, const std::string& name, const std::string& build);

struct ListedRoom {
    Address host;
    uint8_t maxPlayers = 0;
    uint8_t players = 0;
    std::string name;
    std::string build;
};
// The server's public rooms as last fetched; RequestList fetches them again with the next socket activity.
std::vector<ListedRoom> Listed();
void RequestList();

} // namespace Relay

struct LocalInterface {
    uint32_t ip = 0;         // host byte order
    uint32_t broadcast = 0;  // subnet-directed broadcast, 0 when the interface has none
};
// The active IPv4 interfaces (loopback excluded).
std::vector<LocalInterface> LocalInterfaces();

// This device's name for room lists (computer name, or the phone's model on Android when the app
// passed it in).
std::string DeviceName();
void SetDeviceName(std::string name);

} // namespace Netplay
