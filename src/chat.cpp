#include "chat.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <thread>

namespace beseda {

namespace {
std::mutex g_output_mutex;  // keeps lines from the two threads from interleaving
}  // namespace

ChatPeer::ChatPeer(std::uint16_t listen_port, std::optional<Endpoint> peer)
    : socket_(listen_port), peer_(peer) {}

void ChatPeer::run() {
    std::thread receiver(&ChatPeer::receive_loop, this);
    send_loop();
    running_ = false;
    receiver.join();
}

std::optional<Endpoint> ChatPeer::peer() {
    std::lock_guard<std::mutex> lock(peer_mutex_);
    return peer_;
}

void ChatPeer::receive_loop() {
    while (running_) {
        std::optional<Datagram> msg;
        try {
            msg = socket_.receive(200);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "receive failed: " << e.what() << std::endl;
            continue;
        }
        if (!msg) continue;

        bool learned = false;
        {
            std::lock_guard<std::mutex> lock(peer_mutex_);
            if (!peer_) {
                peer_ = msg->from;
                learned = true;
            }
        }

        std::lock_guard<std::mutex> lock(g_output_mutex);
        if (learned) std::cerr << "* chatting with " << msg->from.to_string() << std::endl;
        std::cout << msg->from.to_string() << ": " << msg->payload << std::endl;
    }
}

void ChatPeer::send_loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        auto target = peer();
        if (!target) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* no peer yet: wait for someone to message you, "
                         "or restart with a peer address" << std::endl;
            continue;
        }
        if (line.size() > kMaxMessageSize) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* message truncated to " << kMaxMessageSize << " bytes" << std::endl;
            line.resize(kMaxMessageSize);
        }
        try {
            socket_.send_to(*target, line);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* send failed: " << e.what() << std::endl;
        }
    }
}

}  // namespace beseda
