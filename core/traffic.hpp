#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace th {
struct TrafficEntry {
    uint64_t sequence = 0, milliseconds = 0;
    char direction = 'E';
    std::array<char, 192> text{};
};
struct TrafficActivity {
    uint64_t setting = 0, query = 0;
};
// Fixed RAM ring, shared by the protocol worker and the UI. Never writes flash.
class TrafficLog {
    std::mutex mutex;
    std::array<TrafficEntry, 80> entries{};
    uint64_t sequence = 0;
    size_t count = 0;
    TrafficActivity tx;

  public:
    void add(uint64_t ms, char direction, const std::string &text) {
        std::lock_guard<std::mutex> lock(mutex);
        auto &e = entries[sequence % entries.size()];
        e = {};
        e.sequence = ++sequence;
        e.milliseconds = ms;
        e.direction = direction;
        if (direction == 'T') {
            if (text.find('?') != std::string::npos)
                tx.query = sequence;
            else
                tx.setting = sequence;
        }
        size_t n = 0;
        for (unsigned char c : text) {
            if (c == '\r' || c == '\n')
                break;
            if (n == e.text.size() - 1)
                break;
            e.text[n++] = c >= 32 && c < 127 && c != '#' ? c : '?';
        }
        if (count < entries.size())
            ++count;
    }
    TrafficActivity activity() {
        std::lock_guard<std::mutex> lock(mutex);
        return tx;
    }
    uint64_t latest() {
        std::lock_guard<std::mutex> lock(mutex);
        return sequence;
    }
    std::vector<TrafficEntry> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<TrafficEntry> result;
        result.reserve(count);
        for (uint64_t i = sequence - count; i < sequence; ++i)
            result.push_back(entries[i % entries.size()]);
        return result;
    }
};
inline TrafficLog &traffic_log() {
    static TrafficLog log;
    return log;
}
} // namespace th
