// beseda: peer-to-peer UDP console chat.
//
// Usage: beseda [-p LISTEN_PORT] [-u NAME] [HOST[:PORT]]

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "Chat.h"

using namespace beseda;

namespace {

void Usage(const char* prog) {
    std::cerr << "usage: " << prog << " [-p LISTEN_PORT] [-u NAME] [HOST[:PORT]]\n"
              << "\n"
              << "  -p LISTEN_PORT  local UDP port to listen on (default " << kDefaultPort << ")\n"
              << "  -u NAME         username shown to your peer (default $USER, max " << kMaxNameSize << " bytes)\n"
              << "  HOST[:PORT]     peer to chat with (port defaults to " << kDefaultPort << ").\n"
              << "                  Without it, you chat with whoever messages you first.\n"
              << "\n"
              << "Type a line and press Enter to send it. Ctrl-D quits.\n"
              << "Messages are acknowledged by the peer and resent if lost; \"* no delivery\n"
              << "confirmation\" means a message may not have arrived (both sides need this version).\n"
              << "You are told when your partner comes online, leaves, or goes silent for 10 s.\n"
              << "Type /history [N] to show the last N messages (default: all) with timestamps.\n"
              << "History is saved per partner in $XDG_DATA_HOME/beseda (default ~/.local/share/beseda)\n"
              << "and restored once your partner's name is known (their first message).\n";
}

std::uint16_t ParsePort(const std::string& s) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || v < 1 || v > 65535) {
        throw std::invalid_argument("invalid port '" + s + "'");
    }
    return static_cast<std::uint16_t>(v);
}

std::string ParseName(const std::string& s) {
    if (s.empty() || s.size() > kMaxNameSize) {
        throw std::invalid_argument("name must be 1-" + std::to_string(kMaxNameSize) + " bytes");
    }
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) throw std::invalid_argument("name must not contain control characters");
    }
    return s;
}

std::string DefaultName() {
    const char* user = std::getenv("USER");
    if (user && *user) {
        std::string name(user);
        try { return ParseName(name); } catch (const std::exception&) {}
    }
    return "anon";
}

Endpoint ParsePeer(const std::string& s) {
    Endpoint ep;
    ep.port = kDefaultPort;
    std::string host = s;
    auto colon = s.rfind(':');
    if (colon != std::string::npos) {
        host = s.substr(0, colon);
        ep.port = ParsePort(s.substr(colon + 1));
    }
    ep.addr = ResolveIpv4(host);
    return ep;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::uint16_t listenPort = kDefaultPort;
    std::optional<Endpoint> peer;
    std::string name = DefaultName();

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                Usage(argv[0]);
                return 0;
            } else if (arg == "-p" || arg == "--port") {
                if (++i >= argc) throw std::invalid_argument(arg + " needs a value");
                listenPort = ParsePort(argv[i]);
            } else if (arg == "-u" || arg == "--user") {
                if (++i >= argc) throw std::invalid_argument(arg + " needs a value");
                name = ParseName(argv[i]);
            } else if (!peer && arg[0] != '-') {
                peer = ParsePeer(arg);
            } else {
                throw std::invalid_argument("unexpected argument '" + arg + "'");
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n\n";
        Usage(argv[0]);
        return 2;
    }

    try {
        ChatPeer chat(listenPort, peer, name);
        std::cerr << "* beseda: " << name << " listening on port " << listenPort;
        if (peer) {
            std::cerr << ", chatting with " << peer->ToString();
        }
        else { 
            std::cerr << ", waiting for someone to message you";
        }
        std::cerr << std::endl;
        chat.Run();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
