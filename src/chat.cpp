#include "chat.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace beseda {

namespace {
std::mutex g_output_mutex;  // keeps lines from the two threads from interleaving
}  // namespace

ChatPeer::ChatPeer(std::uint16_t listen_port, std::optional<Endpoint> peer, std::string name)
    : socket_(listen_port), name_(std::move(name)), peer_(peer) {}

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
        std::string sender = msg->from.to_string();
        std::string text = msg->payload;
        auto nl = text.find('\n');
        if (nl != std::string::npos && nl > 0 && nl <= kMaxNameSize) {
            sender = text.substr(0, nl);
            text.erase(0, nl + 1);
        }
        std::cout << sender << ": " << text << std::endl;
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
        const std::size_t max_text = kMaxMessageSize - name_.size() - 1;
        if (line.size() > max_text) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* message truncated to " << max_text << " bytes" << std::endl;
            line.resize(max_text);
        }
        try {
            socket_.send_to(*target, name_ + '\n' + line);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* send failed: " << e.what() << std::endl;
        }
    }
}

}  // namespace beseda
