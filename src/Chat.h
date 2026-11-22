#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#include "History.h"
#include "Protocol.h"
#include "UdpSocket.h"

namespace beseda {

constexpr std::uint16_t kDefaultPort = 50000;

// A chat peer: one UDP socket used both to receive messages (printed to stdout)
// and to send lines typed on stdin. See Protocol.h for the wire format.
//
// If no peer is given up front, the peer becomes the sender of the first
// message received, so replies go straight back to them.
// Every message sent carries this peer's username.
//
// Delivery: every message gets an id and the receiver ACKs it. Unconfirmed
// messages are resent with exponential backoff; after the last attempt a
// "* no delivery confirmation" notice is printed. Retransmitted duplicates are
// recognised by id and shown only once. On EOF, Run() waits for outstanding
// messages to be confirmed or given up on before returning.
//
// While a peer is known, a PING is sent every few seconds and a BYE on exit. 
class ChatPeer {
public:
    ChatPeer(std::uint16_t listenPort, std::optional<Endpoint> peer, std::string name);

    // Runs until stdin hits EOF and all sent messages are confirmed or given up on.
    void Run();

private:
    // A sent message that has not been acknowledged yet.
    struct PendingMessage {
        Endpoint target;
        std::string packet;  // encoded, ready to resend
        std::string text;    // for the failure notice
        int sends = 0;
        std::chrono::steady_clock::time_point deadline;  // when to resend or give up
    };

    void ReceiveLoop();
    void SendLoop();
    std::optional<Endpoint> Peer();
    // Handles a "/history [N]" line. Returns false if `line` is not that command.
    bool HandleHistoryCommand(const std::string& line);
    // Restores and starts saving the history with `partner`. Call with
    // gOutputMutex held (it prints a notice).
    void RestoreHistory(const std::string& partner);

    void HandleAck(const Endpoint& from, std::uint64_t id);
    // Resends overdue messages and drops those out of attempts (printing a
    // notice). Returns how long Receive() may block, in milliseconds.
    int RetransmitDue();
    // Records an incoming message id. Returns false if it was seen recently.
    bool RememberId(std::uint64_t id);
    // Blocks until no messages are pending.
    void WaitForPending();

    // Sets the peer to `from` if none is known yet. Returns true if it did.
    bool LearnPeer(const Endpoint& from);
    // Updates the partner's online state from a packet received from `from`.
    void UpdatePresence(const Endpoint& from, const Packet& packet);
    // Sends a PING when due and notices a silent partner. Returns how long
    // Receive() may block, in milliseconds.
    int HeartbeatDue();

    UdpSocket socket_;
    const std::string name_;
    std::mutex peerMutex_;
    std::optional<Endpoint> peer_;
    std::atomic<bool> running_{true};
    MessageHistory history_;
    bool historyFailed_ = false;  // only touched by the receive thread

    std::uint64_t nextId_;  // only touched by the send thread
    std::mutex pendingMutex_;
    std::condition_variable pendingCv_;  // notified when pending_ becomes empty
    std::map<std::uint64_t, PendingMessage> pending_;

    // Recently received message ids, for dropping duplicates (receive thread only).
    std::deque<std::uint64_t> seenOrder_;
    std::unordered_set<std::uint64_t> seenIds_;

    // Presence (receive thread only).
    bool heartbeatSeen_ = false;  // the partner sends PINGs, so silence means something
    bool online_ = false;
    std::string partnerName_;
    std::chrono::steady_clock::time_point lastHeard_;
    std::chrono::steady_clock::time_point nextPing_;  // epoch: ping as soon as a peer is known
};

}  // namespace beseda
