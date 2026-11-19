// beseda: peer-to-peer UDP console chat.
//
// Usage: beseda [-p LISTEN_PORT] [HOST[:PORT]]

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "chat.hpp"

using namespace beseda;

namespace {

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " [-p LISTEN_PORT] [HOST[:PORT]]\n"
              << "\n"
              << "  -p LISTEN_PORT  local UDP port to listen on (default " << kDefaultPort << ")\n"
              << "  HOST[:PORT]     peer to chat with (port defaults to " << kDefaultPort << ").\n"
              << "                  Without it, you chat with whoever messages you first.\n"
              << "\n"
              << "Type a line and press Enter to send it. Ctrl-D quits.\n";
}

std::uint16_t parse_port(const std::string& s) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || v < 1 || v > 65535) {
        throw std::invalid_argument("invalid port '" + s + "'");
    }
    return static_cast<std::uint16_t>(v);
}

Endpoint parse_peer(const std::string& s) {
    Endpoint ep;
    ep.port = kDefaultPort;
    std::string host = s;
    auto colon = s.rfind(':');
    if (colon != std::string::npos) {
        host = s.substr(0, colon);
        ep.port = parse_port(s.substr(colon + 1));
    }
    ep.addr = resolve_ipv4(host);
    return ep;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::uint16_t listen_port = kDefaultPort;
    std::optional<Endpoint> peer;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                usage(argv[0]);
                return 0;
            } else if (arg == "-p" || arg == "--port") {
                if (++i >= argc) throw std::invalid_argument(arg + " needs a value");
                listen_port = parse_port(argv[i]);
            } else if (!peer && arg[0] != '-') {
                peer = parse_peer(arg);
            } else {
                throw std::invalid_argument("unexpected argument '" + arg + "'");
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n\n";
        usage(argv[0]);
        return 2;
    }

    try {
        ChatPeer chat(listen_port, peer);
        std::cerr << "* beseda listening on port " << listen_port;
        if (peer) {
            std::cerr << ", chatting with " << peer->to_string();
        }
        else { 
            std::cerr << ", waiting for someone to message you";
        }
        std::cerr << std::endl;
        chat.run();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
