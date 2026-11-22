#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace beseda {

// One chat message as seen by this peer. The timestamp is local: taken when
// the message was sent or received.
struct HistoryEntry {
    std::chrono::system_clock::time_point time;
    std::string sender;
    std::string text;
    bool outgoing = false;  // true if this peer sent it
    std::string ToString() const;
};

class MessageHistory {
public:
    explicit MessageHistory(std::size_t capacity = 1000);

    void Add(std::string sender, std::string text, bool outgoing);

    std::size_t AttachFile(const std::string& path);
    bool Attached() const;

    std::vector<HistoryEntry> Last(std::size_t count = 0) const;

private:
    void WriteEntry(const HistoryEntry& entry);  // requires mutex_
    void Trim();                                  // requires mutex_

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<HistoryEntry> entries_;
    std::ofstream file_;
};

std::string HistoryPath(const std::string& self, const std::string& partner);

}  // namespace beseda
