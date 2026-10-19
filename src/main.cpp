#include "tcp_server.hpp"
#include <charconv>
#include <iostream>
namespace {
std::atomic<bool> stopped{false};
static_assert(std::atomic<bool>::is_always_lock_free, "signal notification requires lock-free bool atomics");
extern "C" void handle_signal(int) { stopped.store(true, std::memory_order_relaxed); }
unsigned number(const std::string& s, unsigned min, unsigned max) {
    unsigned value = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data()+s.size(), value);
    if (ec != std::errc() || p != s.data()+s.size() || value < min || value > max) throw std::invalid_argument("invalid number: " + s);
    return value;
}
}
int main(int argc, char** argv) {
    try {
        kv::ServerConfig config;
        std::string wal = "concurrentkv.wal";
        kv::Durability durability = kv::Durability::Sync;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "kv_server [--bind IPv4] [--port 8080] [--workers 4] [--queue 1024] [--max-connections 1024] [--max-requests 1000000] [--idle-seconds 30] [--wal path] [--durability sync|shutdown]\n";
                return 0;
            }
            if (i+1 >= argc) throw std::invalid_argument("missing argument value");
            std::string value = argv[++i];
            if (arg == "--bind") config.bind = value;
            else if (arg == "--port") config.port = static_cast<uint16_t>(number(value, 1, 65535));
            else if (arg == "--workers") config.workers = number(value, 1, 256);
            else if (arg == "--queue") config.queue = number(value, 1, 1000000);
            else if (arg == "--max-connections") config.max_connections = number(value, 1, 100000);
            else if (arg == "--max-requests") config.max_requests = number(value, 1, 1000000000);
            else if (arg == "--idle-seconds") config.idle_seconds = number(value, 1, 86400);
            else if (arg == "--wal") wal = value;
            else if (arg == "--durability") {
                if (value == "sync") durability = kv::Durability::Sync;
                else if (value == "shutdown") durability = kv::Durability::Shutdown;
                else throw std::invalid_argument("durability must be sync or shutdown");
            } else throw std::invalid_argument("unknown option: " + arg);
        }
        struct sigaction action{}; action.sa_handler = handle_signal; sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr)) throw kv::system_error("sigaction");
        kv::KeyValueStore store(wal, durability);
        kv::TcpServer server(config, store);
        server.run(&stopped);
    } catch (const std::exception& e) { kv::log("ERROR", e.what()); return 1; }
}
