#include "tcp_server.hpp"
#include "socket_io.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unordered_map>
namespace kv {
namespace {
using Clock = std::chrono::steady_clock;
void nonblocking(int fd, bool socket = true) {
    int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || ::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        throw system_error("configure socket");
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (socket && ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0) throw system_error("SO_NOSIGPIPE");
#else
    (void)socket;
#endif
}

}
struct TcpServer::Impl {
    struct Connection {
        Fd fd;
        std::string input, output;
        size_t sent = 0, requests = 0;
        bool busy = false, eof = false, closing = false;
        Clock::time_point activity = Clock::now();
    };
    struct Completion { uint64_t id; std::string response; };
    ServerConfig config;
    KeyValueStore& store;
    Metrics metrics;
    Fd listener, wake_read, wake_write;
    std::mutex completions_mutex;
    std::vector<Completion> completions;
    std::unordered_map<uint64_t, Connection> clients;
    std::atomic<bool> stop{false};
    uint64_t next_id = 0;
    uint16_t bound_port;
    // Last member: workers join before anything used by tasks is destroyed.
    ThreadPool pool;
    Impl(ServerConfig c, KeyValueStore& s) : config(std::move(c)), store(s), pool(config.workers, config.queue) {
        int wake[2];
        if (::pipe(wake) < 0) throw system_error("completion pipe");
        wake_read = Fd(wake[0]); wake_write = Fd(wake[1]);
        nonblocking(wake_read.get(), false); nonblocking(wake_write.get(), false);
        if (!config.max_connections || !config.max_requests || !config.idle_seconds) throw std::invalid_argument("limits must be positive");
        listener = Fd(::socket(AF_INET, SOCK_STREAM, 0));
        if (listener.get() < 0) throw system_error("socket");
        nonblocking(listener.get());
        int one = 1;
        if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) throw system_error("reuseaddr");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(config.port);
        if (::inet_pton(AF_INET, config.bind.c_str(), &address.sin_addr) != 1) throw std::invalid_argument("bind must be numeric IPv4");
        if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(listener.get(), 128) < 0)
            throw system_error("bind/listen");
        socklen_t length = sizeof(address);
        if (::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0) throw system_error("getsockname");
        bound_port = ntohs(address.sin_port);
    }
    void collect() {
        std::vector<Completion> ready;
        { std::lock_guard lock(completions_mutex); ready.swap(completions); }
        for (auto& done : ready) {
            auto it = clients.find(done.id);
            if (it == clients.end()) continue;
            it->second.busy = false;
            it->second.output = std::move(done.response);
            it->second.sent = 0;
            it->second.activity = Clock::now();
        }
    }
    void dispatch(uint64_t id, Connection& c) {
        if (c.busy || !c.output.empty() || c.closing) return;
        auto end = c.input.find('\n');
        if ((end == std::string::npos && c.input.size() > max_command) || (end != std::string::npos && end > max_command)) {
            c.output = "ERR TOO_LARGE\n"; c.closing = true; return;
        }
        if (end == std::string::npos) { if (c.eof) c.closing = true; return; }
        auto command = c.input.substr(0, end); c.input.erase(0, end + 1);
        ++c.requests;
        c.busy = true;
        if (!pool.submit([this, id, command = std::move(command)] {
            std::string response = execute(command, store, metrics, pool);
            std::lock_guard lock(completions_mutex);
            completions.push_back({id, std::move(response)});
            char byte = 1;
            ssize_t written;
            do { written = ::write(wake_write.get(), &byte, 1); } while (written < 0 && errno == EINTR);
            // EAGAIN means a wakeup is already pending; the queue is authoritative.
        })) { c.busy = false; c.output = "ERR BUSY\n"; ++metrics.rejected; }
        if (c.requests >= config.max_requests) c.closing = true;
    }
    void accept_clients() {
        // Bound work per poll turn to avoid an accept flood starving existing clients.
        for (int i = 0; i < 64; ++i) {
            Fd fd(::accept(listener.get(), nullptr, nullptr));
            if (fd.get() < 0) {
                if (errno == EINTR || errno == ECONNABORTED || errno == EPROTO ||
                    errno == ENETDOWN || errno == ENETUNREACH || errno == EHOSTUNREACH) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EMFILE || errno == ENFILE) break;
                throw system_error("accept");
            }
            ++metrics.accepted;
            // A reset peer can make socket options fail after accept (macOS EINVAL).
            // Per-connection setup failure must never terminate the reactor.
            try { nonblocking(fd.get()); }
            catch (const std::runtime_error&) { ++metrics.rejected; continue; }
            if (clients.size() >= config.max_connections) { ++metrics.rejected; continue; }
            Connection c; c.fd = std::move(fd);
            clients.emplace(++next_id, std::move(c)); ++metrics.active;
        }
    }
    void loop(const std::atomic<bool>* signal) {
        log("INFO", "listening port=" + std::to_string(bound_port));
        while (!stop.load() && !(signal && signal->load(std::memory_order_relaxed))) {
            collect();
            std::vector<pollfd> fds{{listener.get(), POLLIN, 0}, {wake_read.get(), POLLIN, 0}};
            std::vector<uint64_t> ids;
            for (auto& [id, c] : clients) {
                dispatch(id, c);
                short events = 0;
                if (!c.busy && !c.closing && !c.eof && c.output.empty()) events |= POLLIN;
                if (!c.output.empty()) events |= POLLOUT;
                // Do not poll idle in-flight sockets: HUP would otherwise cause a busy loop.
                fds.push_back({events ? c.fd.get() : -1, events, 0}); ids.push_back(id);
            }
            int n = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 50);
            if (n < 0) { if (errno == EINTR) continue; throw system_error("poll"); }
            if (fds[0].revents & POLLIN) accept_clients();
            if (fds[1].revents & POLLIN) {
                char bytes[256];
                while (::read(wake_read.get(), bytes, sizeof(bytes)) > 0) {}
                collect();
            }
            for (size_t i = 0; i < ids.size(); ++i) {
                auto it = clients.find(ids[i]); if (it == clients.end()) continue;
                auto& c = it->second; short events = fds[i + 2].revents;
                bool dead = (events & (POLLERR | POLLNVAL)) != 0;
                if (!dead && (events & (POLLIN | POLLHUP)) && !c.busy && c.output.empty()) {
                    char buffer[4096];
                    // At most max_input bytes buffered per connection.
                    size_t room = max_input - c.input.size();
                    ssize_t r = ::recv(c.fd.get(), buffer, std::min(room, sizeof(buffer)), 0);
                    if (r > 0) { c.input.append(buffer, static_cast<size_t>(r)); c.activity = Clock::now(); }
                    else if (!r) c.eof = true;
                    else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) dead = true;
                }
                if (!dead && (events & POLLOUT) && !c.output.empty()) {
                    auto before = c.sent;
                    auto status = write_pending(c.fd.get(), c.output, c.sent);
                    if (c.sent != before) c.activity = Clock::now();
                    if (status == WriteResult::Done) { c.output.clear(); c.sent = 0; }
                    if (status == WriteResult::Closed) dead = true;
                }
                if (!c.busy && c.output.empty()) dispatch(ids[i], c);
                if (dead || (!c.busy && c.closing && c.output.empty()) ||
                    (!c.busy && Clock::now() - c.activity > std::chrono::seconds(config.idle_seconds))) {
                    clients.erase(it); --metrics.active;
                }
            }
        }
        listener = Fd(); // Stop admission; queued and running operations finish.
        pool.shutdown(); collect(); store.flush();
        for (auto& [id, c] : clients) { (void)id; c.output += "ERR SHUTDOWN\n"; }
        auto deadline = Clock::now() + std::chrono::seconds(2);
        while (!clients.empty() && Clock::now() < deadline) {
            std::vector<pollfd> fds; std::vector<uint64_t> ids;
            for (auto& [id, c] : clients) { fds.push_back({c.fd.get(), POLLOUT, 0}); ids.push_back(id); }
            int result = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 10);
            if (result < 0 && errno != EINTR) break;
            for (size_t i = 0; i < ids.size(); ++i) {
                auto& c = clients.at(ids[i]);
                if (!fds[i].revents) continue;
                auto status = write_pending(c.fd.get(), c.output, c.sent);
                if (status != WriteResult::Pending) clients.erase(ids[i]);
            }
        }
        clients.clear(); metrics.active = 0;
        log("INFO", "shutdown complete");
    }
};
TcpServer::TcpServer(ServerConfig config, KeyValueStore& store) : impl_(std::make_unique<Impl>(std::move(config), store)) {}
TcpServer::~TcpServer() = default;
uint16_t TcpServer::port() const { return impl_->bound_port; }
void TcpServer::run(const std::atomic<bool>* signal) { impl_->loop(signal); }
void TcpServer::request_stop() { impl_->stop = true; }
}
