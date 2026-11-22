#include "Chat.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace beseda {

namespace {
std::mutex gOutputMutex;  // keeps lines from the two threads from interleaving

using Clock = std::chrono::steady_clock;

// Retransmission: the n-th send of a message waits kFirstRetry * 2^(n-1) for an
// ACK; after kMaxSends sends it is given up on (250+500+1000+2000+4000 ms).
constexpr std::chrono::milliseconds kFirstRetry{250};
constexpr int kMaxSends = 5;
constexpr int kPollMs = 200;                // longest wait between checks
constexpr std::size_t kSeenCapacity = 1024;  // incoming ids remembered for dedup

Clock::duration RetryDelay(int sends) {
    return kFirstRetry * (1 << (sends - 1));
}

// Random start, so a restarted peer does not reuse ids the other side has seen.
std::uint64_t RandomId() {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) | rd();
}

}  // namespace

ChatPeer::ChatPeer(std::uint16_t listenPort, std::optional<Endpoint> peer, std::string name)
    : socket_(listenPort), name_(std::move(name)), peer_(peer), nextId_(RandomId()) {}

void ChatPeer::Run() {
    std::thread receiver(&ChatPeer::ReceiveLoop, this);
    SendLoop();
    WaitForPending();
    running_ = false;
    receiver.join();
}

std::optional<Endpoint> ChatPeer::Peer() {
    std::lock_guard<std::mutex> lock(peerMutex_);
    return peer_;
}

void ChatPeer::ReceiveLoop() {
    while (running_) {
        const int timeoutMs = RetransmitDue();
        std::optional<Datagram> msg;
        try {
            msg = socket_.Receive(timeoutMs);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(gOutputMutex);
            std::cerr << "receive failed: " << e.what() << std::endl;
            continue;
        }
        if (!msg) continue;

        auto packet = Decode(msg->payload);
        if (!packet) continue;  // malformed BESEDA1 packet
        if (packet->kind == Packet::Kind::Ack) {
            HandleAck(msg->from, packet->id);
            continue;
        }
        if (packet->kind == Packet::Kind::Message) {
            // ACK every copy: if our earlier ACK was lost, the sender is retrying.
            try {
                socket_.SendTo(msg->from, EncodeAck(packet->id));
            } catch (const std::exception&) {
                // The sender will retry and we will ACK again then.
            }
            if (!RememberId(packet->id)) continue;  // already shown
        }

        bool learned = false;
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            if (!peer_) {
                peer_ = msg->from;
                learned = true;
            }
        }

        const bool named = !packet->name.empty();
        std::string sender = named ? std::move(packet->name) : msg->from.ToString();
        std::string text = std::move(packet->data);

        std::lock_guard<std::mutex> lock(gOutputMutex);
        if (learned) std::cerr << "* chatting with " << msg->from.ToString() << std::endl;
        // The first named sender is the chat partner: restore our saved history
        // with them before recording this message.
        if (named && !historyFailed_ && !history_.Attached()) RestoreHistory(sender);
        std::cout << sender << ": " << text << std::endl;
        history_.Add(std::move(sender), std::move(text), /*outgoing=*/false);
    }
}

void ChatPeer::SendLoop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (HandleHistoryCommand(line)) continue;

        auto target = Peer();
        if (!target) {
            std::lock_guard<std::mutex> lock(gOutputMutex);
            std::cerr << "* no peer yet: wait for someone to message you, "
                         "or restart with a peer address" << std::endl;
            continue;
        }
        const std::uint64_t id = nextId_++;
        const std::size_t maxText = kMaxMessageSize - EncodeMessage(id, name_, "").size();
        if (line.size() > maxText) {
            std::lock_guard<std::mutex> lock(gOutputMutex);
            std::cerr << "* message truncated to " << maxText << " bytes" << std::endl;
            line.resize(maxText);
        }
        const std::string packet = EncodeMessage(id, name_, line);
        {
            // Register before sending, so an ACK cannot arrive before we expect it.
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pending_[id] = PendingMessage{*target, packet, line, 1, Clock::now() + RetryDelay(1)};
        }
        try {
            socket_.SendTo(*target, packet);
            history_.Add(name_, line, /*outgoing=*/true);
        } catch (const std::exception& e) {
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                pending_.erase(id);
                if (pending_.empty()) pendingCv_.notify_all();
            }
            std::lock_guard<std::mutex> lock(gOutputMutex);
            std::cerr << "* send failed: " << e.what() << std::endl;
        }
    }
}

void ChatPeer::HandleAck(const Endpoint& from, std::uint64_t id) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pending_.find(id);
    if (it == pending_.end() || !(it->second.target == from)) return;
    pending_.erase(it);
    if (pending_.empty()) pendingCv_.notify_all();
}

int ChatPeer::RetransmitDue() {
    const auto now = Clock::now();
    auto next = now + std::chrono::milliseconds(kPollMs);
    std::vector<std::string> failed;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            PendingMessage& m = it->second;
            if (m.deadline <= now) {
                if (m.sends >= kMaxSends) {
                    failed.push_back(std::move(m.text));
                    it = pending_.erase(it);
                    continue;
                }
                try {
                    socket_.SendTo(m.target, m.packet);
                } catch (const std::exception&) {
                    // Counts as an attempt; a transient error may clear by the next one.
                }
                ++m.sends;
                m.deadline = now + RetryDelay(m.sends);
            }
            next = std::min(next, m.deadline);
            ++it;
        }
        if (!failed.empty() && pending_.empty()) pendingCv_.notify_all();
    }

    if (!failed.empty()) {
        std::lock_guard<std::mutex> lock(gOutputMutex);
        for (const auto& text : failed) {
            std::cerr << "* no delivery confirmation, message may be lost: " << text << std::endl;
        }
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count() + 1;
    return static_cast<int>(std::clamp<long long>(ms, 1, kPollMs));
}

bool ChatPeer::RememberId(std::uint64_t id) {
    if (!seenIds_.insert(id).second) return false;
    seenOrder_.push_back(id);
    if (seenOrder_.size() > kSeenCapacity) {
        seenIds_.erase(seenOrder_.front());
        seenOrder_.pop_front();
    }
    return true;
}

void ChatPeer::WaitForPending() {
    std::unique_lock<std::mutex> lock(pendingMutex_);
    if (pending_.empty()) return;
    const std::size_t n = pending_.size();
    lock.unlock();
    {
        std::lock_guard<std::mutex> out(gOutputMutex);
        std::cerr << "* waiting for " << n << " unconfirmed message" << (n == 1 ? "" : "s")
                  << " (Ctrl-C to quit now)" << std::endl;
    }
    lock.lock();
    pendingCv_.wait(lock, [this] { return pending_.empty(); });
}

void ChatPeer::RestoreHistory(const std::string& partner) {
    std::string path;
    try {
        path = HistoryPath(name_, partner);
        std::size_t n = history_.AttachFile(path);
        if (n > 0) {
            std::cerr << "* restored " << n << " earlier message" << (n == 1 ? "" : "s")
                      << " with " << partner << " (type /history to see them)" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "* history will not be saved: " << e.what() << std::endl;
        historyFailed_ = true;
    }
}

bool ChatPeer::HandleHistoryCommand(const std::string& line) {
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
            std::lock_guard<std::mutex> lock(gOutputMutex);
            std::cerr << "* usage: /history [N]" << std::endl;
            return true;
        }
        count = static_cast<std::size_t>(v);
    }

    auto entries = history_.Last(count);
    std::lock_guard<std::mutex> lock(gOutputMutex);
    if (entries.empty()) {
        std::cerr << "* history is empty" << std::endl;
        return true;
    }
    std::cerr << "* --- history (" << entries.size() << ") ---" << std::endl;
    for (const auto& e : entries) std::cerr << e.ToString() << '\n';
    std::cerr << "* --- end of history ---" << std::endl;
    return true;
}

}  // namespace beseda
