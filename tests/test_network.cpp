#include "test_support.hpp"
#include "tcp_server.hpp"
#include "client_connection.hpp"
#include <future>
#include <sys/socket.h>
struct Running {
    kv::TcpServer server;
    std::future<void> run;
    Running(kv::KeyValueStore& store, kv::ServerConfig config) : server(config, store), run(std::async(std::launch::async, [this] { try { server.run(); } catch (const std::exception& e) { kv::log("ERROR", e.what()); throw; } })) {}
    ~Running() { server.request_stop(); if (run.valid()) run.wait(); }
    void stop() { server.request_stop(); run.get(); }
    kv::ClientConnection connect() { return kv::ClientConnection("127.0.0.1", std::to_string(server.port())); }
};
int main() {
    Temp temp;
    {
        kv::KeyValueStore store(temp.wal()); kv::ServerConfig config; config.port = 0; config.workers = 2;
        Running server(store, config);
        // More idle clients than workers must not starve a live connection.
        std::vector<std::unique_ptr<kv::ClientConnection>> idle;
        for (int i = 0; i < 12; ++i) idle.push_back(std::make_unique<kv::ClientConnection>(server.connect()));
        auto c = server.connect();
        c.send("SE"); c.send("T fragmented va"); c.send("lue\nGET fragmented\nEXISTS fragmented\n");
        CHECK(c.receive() == "OK"); CHECK(c.receive() == "VALUE value"); CHECK(c.receive() == "EXISTS 1");
        CHECK(c.request("SET empty ") == "OK"); CHECK(c.request("GET empty") == "VALUE ");
        CHECK(c.request("BAD") == "ERR COMMAND");
        CHECK(c.request("STATS").find("keys=2") != std::string::npos);
        std::string large(kv::max_value, 'v'); CHECK(c.request("SET large "+large) == "OK");
        // Small receive buffer and pipelining force incremental delivery without unbounded output.
        int small = 1024; ::setsockopt(c.fd(), SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
        std::string batch; for (int i = 0; i < 40; ++i) batch += "GET large\n";
        c.send(batch); std::this_thread::sleep_for(std::chrono::milliseconds(30));
        for (int i = 0; i < 40; ++i) CHECK(c.receive() == "VALUE "+large);
        { auto half = server.connect(); half.send("SET half yes\nGET half\n"); ::shutdown(half.fd(), SHUT_WR);
          CHECK(half.receive() == "OK"); CHECK(half.receive() == "VALUE yes"); fails([&] { half.receive(); }); }
        { auto too_big = server.connect(); too_big.send(std::string(kv::max_command+1, 'x')+"\n"); CHECK(too_big.receive() == "ERR TOO_LARGE"); fails([&] { too_big.receive(); }); }
        { auto broken = server.connect(); broken.send("GET large\n"); linger reset{1,0}; ::setsockopt(broken.fd(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)); }
        CHECK(c.request("SET persisted yes") == "OK");
        auto shutdown_client = server.connect(); CHECK(shutdown_client.request("GET persisted") == "VALUE yes");
        server.stop(); CHECK(shutdown_client.receive() == "ERR SHUTDOWN");
    }
    { kv::KeyValueStore recovered(temp.wal()); kv::ServerConfig config; config.port = 0; config.max_requests = 2; config.max_connections = 1;
      Running server(recovered, config); auto c = server.connect(); CHECK(c.request("GET persisted") == "VALUE yes");
      { auto extra = server.connect(); fails([&] { extra.request("STATS"); }); }
      CHECK(c.request("DELETE persisted") == "OK"); fails([&] { c.request("GET persisted"); }); server.stop(); }
    { kv::KeyValueStore recovered(temp.wal()); CHECK(!recovered.exists("persisted")); }
    std::cout << "fragmentation, pipeline, idle clients, writes, disconnects, limits, shutdown/restart checks passed\n";
}
