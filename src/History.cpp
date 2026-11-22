#include "History.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace beseda {

namespace {

std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

std::string Unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            out += c == 't' ? '\t' : c == 'n' ? '\n' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool ParseLine(const std::string& line, HistoryEntry& entry) {
    auto t1 = line.find('\t');
    if (t1 == std::string::npos) return false;
    auto t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) return false;
    auto t3 = line.find('\t', t2 + 1);
    if (t3 == std::string::npos) return false;

    const std::string secs = line.substr(0, t1);
    const std::string dir = line.substr(t1 + 1, t2 - t1 - 1);
    char* end = nullptr;
    long long v = std::strtoll(secs.c_str(), &end, 10);
    if (secs.empty() || *end != '\0' || (dir != "i" && dir != "o")) return false;

    entry.time = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(v));
    entry.outgoing = dir == "o";
    entry.sender = Unescape(line.substr(t2 + 1, t3 - t2 - 1));
    entry.text = Unescape(line.substr(t3 + 1));
    return true;
}

// Keeps a name usable as a single file name: anything other than
// [A-Za-z0-9._-] is written as %XX, and a leading '.' too.
std::string SafeFileName(const std::string& name) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || (c == '.' && i > 0);
        if (ok) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xf];
        }
    }
    return out;
}

}  // namespace

std::string HistoryEntry::ToString() const {
    std::time_t t = std::chrono::system_clock::to_time_t(time);
    std::tm tm{};
    ::localtime_r(&t, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(stamp) + (outgoing ? " > " : " < ") + sender + ": " + text;
}

MessageHistory::MessageHistory(std::size_t capacity) : capacity_(capacity) {}

void MessageHistory::Add(std::string sender, std::string text, bool outgoing) {
    HistoryEntry entry{std::chrono::system_clock::now(), std::move(sender), std::move(text), outgoing};
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) WriteEntry(entry);
    entries_.push_back(std::move(entry));
    Trim();
}

std::size_t MessageHistory::AttachFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) return 0;

    std::deque<HistoryEntry> restored;
    {
        std::ifstream in(path);
        std::string line;
        HistoryEntry entry;
        while (std::getline(in, line)) {
            if (!ParseLine(line, entry)) continue;  
            restored.push_back(std::move(entry));
            if (restored.size() > capacity_) restored.pop_front();
        }
    }

    file_.open(path, std::ios::app);
    if (!file_) throw std::runtime_error("cannot open history file '" + path + "'");
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);

    for (const auto& e : entries_) WriteEntry(e);
    const std::size_t count = restored.size();
    entries_.insert(entries_.begin(), restored.begin(), restored.end());
    Trim();
    return count;
}

bool MessageHistory::Attached() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_.is_open();
}

std::vector<HistoryEntry> MessageHistory::Last(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = (count == 0 || count > entries_.size()) ? entries_.size() : count;
    return std::vector<HistoryEntry>(entries_.end() - static_cast<std::ptrdiff_t>(n), entries_.end());
}

void MessageHistory::WriteEntry(const HistoryEntry& entry) {
    file_ << static_cast<long long>(std::chrono::system_clock::to_time_t(entry.time)) << '\t'
          << (entry.outgoing ? 'o' : 'i') << '\t' << Escape(entry.sender) << '\t'
          << Escape(entry.text) << '\n';
    file_.flush();  // survive a crash or Ctrl-C
}

void MessageHistory::Trim() {
    while (entries_.size() > capacity_) entries_.pop_front();
}

std::string HistoryPath(const std::string& self, const std::string& partner) {
    namespace fs = std::filesystem;
    fs::path base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = fs::path(home) / ".local" / "share";
    } else {
        throw std::runtime_error("neither XDG_DATA_HOME nor HOME is set");
    }

    fs::path root = base / "beseda";
    fs::path dir = root / SafeFileName(self);
    fs::create_directories(dir);
    for (const auto& d : {root, dir}) fs::permissions(d, fs::perms::owner_all);
    return (dir / (SafeFileName(partner) + ".log")).string();
}

}  // namespace beseda
