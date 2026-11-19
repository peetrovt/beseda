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

    std::string to_string() const;
};

struct Datagram {
    Endpoint from;
    std::string payload;
};

// RAII wrapper around a POSIX UDP socket bound to a local port.
// send_to() and receive() may be called concurrently from different threads.
class UdpSocket {
public:
    explicit UdpSocket(std::uint16_t local_port);
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    void send_to(const Endpoint& to, const std::string& data);

    // Waits up to timeout_ms for a datagram. Returns nullopt on timeout.
    std::optional<Datagram> receive(int timeout_ms);

private:
    int fd_ = -1;
};

// Resolves a hostname or dotted IPv4 address. Throws on failure.
in_addr resolve_ipv4(const std::string& host);

}  // namespace beseda
