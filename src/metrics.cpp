#include "metrics.hpp"
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
namespace kv {
std::string Metrics::snapshot(size_t keys, size_t depth, size_t capacity) const {
    auto count = requests.load();
    std::ostringstream out;
    out << "STATS accepted=" << accepted << " active=" << active << " requests=" << count
        << " successful=" << success << " failed=" << failed << " get=" << gets
        << " set=" << sets << " delete=" << deletes << " keys=" << keys
        << " queue_depth=" << depth << " queue_capacity=" << capacity << " rejected=" << rejected
        << " uptime_s=" << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started).count()
        << " avg_latency_us=" << (count ? latency_ns.load() / (1000.0 * count) : 0) << '\n';
    return out.str();
}
void log(const std::string& severity, const std::string& message) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    auto now = std::time(nullptr);
    std::tm utc{}; gmtime_r(&now, &utc);
    auto quote = [](const std::string& text) {
        std::ostringstream out;
        out << '"';
        for (unsigned char c : text) {
            if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
            else if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
            else out << static_cast<char>(c);
        }
        out << '"'; return out.str();
    };
    std::ostringstream line;
    line << "{\"timestamp\":\"" << std::put_time(&utc, "%FT%TZ")
         << "\",\"severity\":" << quote(severity) << ",\"message\":" << quote(message) << "}\n";
    std::cerr << line.str();
}
}
