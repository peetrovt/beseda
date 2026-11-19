#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "udp_socket.hpp"

namespace beseda {

constexpr std::uint16_t kDefaultPort = 50000;
constexpr std::size_t kMaxNameSize = 32;

// Wire format of a chat message: "NAME\nTEXT". Neither part contains '\n'.
// A datagram without '\n' is shown as anonymous text from its address.

// A chat peer: one UDP socket used both to receive messages (printed to stdout)
// and to send lines typed on stdin.
//
// If no peer is given up front, the peer becomes the sender of the first
// message received, so replies go straight back to them.
// Every message sent carries this peer's username.
class ChatPeer {
public:
    ChatPeer(std::uint16_t listen_port, std::optional<Endpoint> peer, std::string name);

    // Runs until stdin hits EOF.
    void run();

private:
    void receive_loop();
    void send_loop();
    std::optional<Endpoint> peer();

    UdpSocket socket_;
    const std::string name_;
    std::mutex peer_mutex_;
    std::optional<Endpoint> peer_;
    std::atomic<bool> running_{true};
};

}  // namespace beseda
