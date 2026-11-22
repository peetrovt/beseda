#include "Protocol.h"

#include <cerrno>
#include <cstdlib>
#include <utility>

namespace beseda {

namespace {

const std::string kPrefix = "BESEDA1 ";

bool ParseId(const std::string& s, std::uint64_t& id) {
    if (s.empty() || s.size() > 20) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    errno = 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0') return false;
    id = static_cast<std::uint64_t>(v);
    return true;
}

bool ValidName(const std::string& name) {
    return !name.empty() && name.size() <= kMaxNameSize && name.find('\n') == std::string::npos;
}

}  // namespace

std::string EncodeMessage(std::uint64_t id, const std::string& name, const std::string& text) {
    return kPrefix + "MSG " + std::to_string(id) + '\n' + name + '\n' + text;
}

std::string EncodeAck(std::uint64_t id) {
    return kPrefix + "ACK " + std::to_string(id);
}

std::string EncodePing(const std::string& name) {
    return kPrefix + "PING\n" + name;
}

std::string EncodeBye(const std::string& name) {
    return kPrefix + "BYE\n" + name;
}

std::optional<Packet> Decode(const std::string& payload) {
    Packet p;

    if (payload.compare(0, kPrefix.size(), kPrefix) != 0) {
        p.kind = Packet::Kind::Legacy;
        p.data = payload;
        auto nl = payload.find('\n');
        if (nl != std::string::npos && nl > 0 && nl <= kMaxNameSize) {
            p.name = payload.substr(0, nl);
            p.data = payload.substr(nl + 1);
        }
        return p;
    }

    const std::string rest = payload.substr(kPrefix.size());
    if (rest.compare(0, 4, "ACK ") == 0) {
        if (!ParseId(rest.substr(4), p.id)) return std::nullopt;
        p.kind = Packet::Kind::Ack;
        return p;
    }
    if (rest.compare(0, 4, "MSG ") == 0) {
        auto nl1 = rest.find('\n', 4);
        if (nl1 == std::string::npos) return std::nullopt;
        if (!ParseId(rest.substr(4, nl1 - 4), p.id)) return std::nullopt;
        auto nl2 = rest.find('\n', nl1 + 1);
        if (nl2 == std::string::npos) return std::nullopt;
        p.name = rest.substr(nl1 + 1, nl2 - nl1 - 1);
        if (!ValidName(p.name)) return std::nullopt;
        p.kind = Packet::Kind::Message;
        p.data = rest.substr(nl2 + 1);
        return p;
    }
    for (auto [word, kind] : {std::pair{"PING\n", Packet::Kind::Ping},
                              std::pair{"BYE\n", Packet::Kind::Bye}}) {
        const std::string head = word;
        if (rest.compare(0, head.size(), head) != 0) continue;
        p.name = rest.substr(head.size());
        if (!ValidName(p.name)) return std::nullopt;
        p.kind = kind;
        return p;
    }
    return std::nullopt;
}

}  // namespace beseda
