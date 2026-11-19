#include "udp_socket.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace beseda {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

}  // namespace

std::string Endpoint::to_string() const {
    char buf[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return std::string(buf) + ":" + std::to_string(port);
}

UdpSocket::UdpSocket(std::uint16_t local_port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) throw_errno("socket");

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(local_port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        int err = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::system_error(err, std::generic_category(),
                                "bind to port " + std::to_string(local_port));
    }
}

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) ::close(fd_);
}

void UdpSocket::send_to(const Endpoint& to, const std::string& data) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr = to.addr;
    dest.sin_port = htons(to.port);
    ssize_t n = ::sendto(fd_, data.data(), data.size(), 0,
                         reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    if (n < 0) throw_errno("sendto");
}

std::optional<Datagram> UdpSocket::receive(int timeout_ms) {
    pollfd pfd{fd_, POLLIN, 0};
    int ready = ::poll(&pfd, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR) return std::nullopt;
        throw_errno("poll");
    }
    if (ready == 0) return std::nullopt;

    char buf[kMaxMessageSize];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0,
                           reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < 0) {
        // ECONNREFUSED: an earlier send hit a closed port (ICMP); not fatal for UDP.
        if (errno == EINTR || errno == ECONNREFUSED) return std::nullopt;
        throw_errno("recvfrom");
    }
    return Datagram{Endpoint{from.sin_addr, ntohs(from.sin_port)},
                    std::string(buf, static_cast<std::size_t>(n))};
}

in_addr resolve_ipv4(const std::string& host) {
    in_addr addr{};
    if (::inet_pton(AF_INET, host.c_str(), &addr) == 1) return addr;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr) {
        throw std::runtime_error("cannot resolve '" + host + "': " + ::gai_strerror(rc));
    }
    addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
    ::freeaddrinfo(result);
    return addr;
}

}  // namespace beseda
