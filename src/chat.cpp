#include "chat.hpp"

#include <cstdlib>
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

        std::string sender = msg->from.to_string();
        std::string text = msg->payload;
        bool named = false;
        auto nl = text.find('\n');
        if (nl != std::string::npos && nl > 0 && nl <= kMaxNameSize) {
            sender = text.substr(0, nl);
            text.erase(0, nl + 1);
            named = true;
        }

        std::lock_guard<std::mutex> lock(g_output_mutex);
        if (learned) std::cerr << "* chatting with " << msg->from.to_string() << std::endl;
        // The first named sender is the chat partner: restore our saved history
        // with them before recording this message.
        if (named && !history_failed_ && !history_.attached()) restore_history(sender);
        std::cout << sender << ": " << text << std::endl;
        history_.add(std::move(sender), std::move(text), /*outgoing=*/false);
    }
}

void ChatPeer::send_loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (handle_history_command(line)) continue;

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
            history_.add(name_, line, /*outgoing=*/true);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* send failed: " << e.what() << std::endl;
        }
    }
}

void ChatPeer::restore_history(const std::string& partner) {
    std::string path;
    try {
        path = history_path(name_, partner);
        std::size_t n = history_.attach_file(path);
        if (n > 0) {
            std::cerr << "* restored " << n << " earlier message" << (n == 1 ? "" : "s")
                      << " with " << partner << " (type /history to see them)" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "* history will not be saved: " << e.what() << std::endl;
        history_failed_ = true;
    }
}

bool ChatPeer::handle_history_command(const std::string& line) {
    const std::string cmd = "/history";
    if (line.compare(0, cmd.size(), cmd) != 0) return false;
    std::string arg = line.substr(cmd.size());
    if (!arg.empty() && arg[0] != ' ') return false;  // e.g. "/historyX" is a normal message
    arg.erase(0, arg.find_first_not_of(' '));
    arg.erase(arg.find_last_not_of(' ') + 1);

    std::size_t count = 0;
    if (!arg.empty()) {
        char* end = nullptr;
        long v = std::strtol(arg.c_str(), &end, 10);
        if (*end != '\0' || v < 1) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cerr << "* usage: /history [N]" << std::endl;
            return true;
        }
        count = static_cast<std::size_t>(v);
    }

    auto entries = history_.last(count);
    std::lock_guard<std::mutex> lock(g_output_mutex);
    if (entries.empty()) {
        std::cerr << "* history is empty" << std::endl;
        return true;
    }
    std::cerr << "* --- history (" << entries.size() << ") ---" << std::endl;
    for (const auto& e : entries) std::cerr << e.to_string() << '\n';
    std::cerr << "* --- end of history ---" << std::endl;
    return true;
}

}  // namespace beseda
