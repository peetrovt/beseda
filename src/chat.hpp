#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "history.hpp"
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
//
// Sent and received messages are kept in a history with local timestamps;
// typing "/history [N]" prints the last N (default: all). The chat partner is
// the first named sender heard from: once known, the saved history with them
// is restored and every message is also saved to their history file.
class ChatPeer {
public:
    ChatPeer(std::uint16_t listen_port, std::optional<Endpoint> peer, std::string name);

    // Runs until stdin hits EOF.
    void run();

private:
    void receive_loop();
    void send_loop();
    std::optional<Endpoint> peer();
    // Handles a "/history [N]" line. Returns false if `line` is not that command.
    bool handle_history_command(const std::string& line);
    // Restores and starts saving the history with `partner`. Call with
    // g_output_mutex held (it prints a notice).
    void restore_history(const std::string& partner);

    UdpSocket socket_;
    const std::string name_;
    std::mutex peer_mutex_;
    std::optional<Endpoint> peer_;
    std::atomic<bool> running_{true};
    MessageHistory history_;
    bool history_failed_ = false;  // only touched by the receive thread
};

}  // namespace beseda
