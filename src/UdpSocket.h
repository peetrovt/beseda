#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <optional>
#include <string>

namespace beseda {

// Maximum message payload in bytes; longer lines are truncated before sending.
constexpr std::size_t kMaxMessageSize = 1024;

struct Endpoint {
    in_addr addr{};
    std::uint16_t port = 0;  // host byte order

    std::string ToString() const;
};

inline bool operator==(const Endpoint& a, const Endpoint& b) {
    return a.addr.s_addr == b.addr.s_addr && a.port == b.port;
}

struct Datagram {
    Endpoint from;
    std::string payload;
};

// RAII wrapper around a POSIX UDP socket bound to a local port.
// SendTo() and Receive() may be called concurrently from different threads.
class UdpSocket {
public:
    explicit UdpSocket(std::uint16_t localPort);
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    void SendTo(const Endpoint& to, const std::string& data);

    // Waits up to timeoutMs for a datagram. Returns nullopt on timeout.
    std::optional<Datagram> Receive(int timeoutMs);

private:
    int fd_ = -1;
};

// Resolves a hostname or dotted IPv4 address. Throws on failure.
in_addr ResolveIpv4(const std::string& host);

}  // namespace beseda
