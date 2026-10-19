#pragma once
#include "command_parser.hpp"
#include <csignal>
#include <memory>
namespace kv {
struct ServerConfig {
    std::string bind = "127.0.0.1";
    uint16_t port = 8080;
    size_t workers = 4, queue = 1024, max_connections = 1024, max_requests = 1000000;
    unsigned idle_seconds = 30;
};
class TcpServer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    TcpServer(ServerConfig config, KeyValueStore& store);
    ~TcpServer();
    uint16_t port() const;
    void run(const std::atomic<bool>* signal = nullptr);
    void request_stop();
};
}
