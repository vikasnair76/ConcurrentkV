#pragma once
#include <atomic>
#include <chrono>
#include <string>
namespace kv {
struct Metrics {
    std::atomic<uint64_t> accepted{0}, active{0}, requests{0}, success{0}, failed{0};
    std::atomic<uint64_t> gets{0}, sets{0}, deletes{0}, latency_ns{0}, rejected{0};
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::string snapshot(size_t keys, size_t depth, size_t capacity) const;
};
void log(const std::string& severity, const std::string& message);
}
