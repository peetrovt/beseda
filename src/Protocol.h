#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace beseda {

constexpr std::size_t kMaxNameSize = 32;

// Wire format, one packet per datagram:
//
//   "BESEDA1 MSG <id>\n<name>\n<text>"   a chat message; the receiver answers with an ACK
//   "BESEDA1 ACK <id>"                   confirms that message <id> arrived

struct Packet {
    enum class Kind { Message, Ack, Legacy };

    Kind kind = Kind::Legacy;
    std::uint64_t id = 0;  
    std::string name;      
    std::string data;  
};

std::string EncodeMessage(std::uint64_t id, const std::string& name, const std::string& text);
std::string EncodeAck(std::uint64_t id);
std::optional<Packet> Decode(const std::string& payload);

}  // namespace beseda
