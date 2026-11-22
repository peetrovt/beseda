#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace beseda {

constexpr std::size_t kMaxNameSize = 32;

struct Packet {
    enum class Kind { Message, Ack, Ping, Bye, Legacy };

    Kind kind = Kind::Legacy;
    std::uint64_t id = 0;  // Message, Ack
    std::string name;      // Message, Ping, Bye; Legacy if it carried one
    std::string data;      // Message, Legacy
};

std::string EncodeMessage(std::uint64_t id, const std::string& name, const std::string& text);
std::string EncodeAck(std::uint64_t id);
std::string EncodePing(const std::string& name);
std::string EncodeBye(const std::string& name);
std::optional<Packet> Decode(const std::string& payload);

}  // namespace beseda
