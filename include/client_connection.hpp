#pragma once
#include "common.hpp"
#include <netdb.h>
#include <sys/socket.h>
namespace kv {
// Blocking client socket with bounded receive/send waits; server sockets are nonblocking.
class ClientConnection {
    Fd fd_;
    std::string buffered_;
public:
    ClientConnection(const std::string& host, const std::string& port) {
        addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_INET;
        addrinfo* addresses = nullptr;
        int result = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
        if (result) throw std::runtime_error(::gai_strerror(result));
        for (auto* a = addresses; a; a = a->ai_next) {
            Fd candidate(::socket(a->ai_family, a->ai_socktype, a->ai_protocol));
            if (candidate.get() < 0) continue;
            timeval timeout{5, 0};
            ::setsockopt(candidate.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(candidate.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
            int one = 1; ::setsockopt(candidate.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
            if (::connect(candidate.get(), a->ai_addr, a->ai_addrlen) == 0) { fd_ = std::move(candidate); break; }
        }
        ::freeaddrinfo(addresses);
        if (fd_.get() < 0) throw system_error("connect");
    }
    int fd() const { return fd_.get(); }
    void send(const std::string& message) {
        size_t done = 0;
        while (done < message.size()) {
#ifdef MSG_NOSIGNAL
            ssize_t n = ::send(fd_.get(), message.data()+done, message.size()-done, MSG_NOSIGNAL);
#else
            ssize_t n = ::send(fd_.get(), message.data()+done, message.size()-done, 0);
#endif
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw system_error("send");
            done += static_cast<size_t>(n);
        }
    }
    std::string receive() {
        for (;;) {
            auto end = buffered_.find('\n');
            if (end != std::string::npos) {
                auto line = buffered_.substr(0, end); buffered_.erase(0, end+1); return line;
            }
            if (buffered_.size() > max_input) throw std::runtime_error("oversized response");
            char chunk[4096]; ssize_t n = ::recv(fd_.get(), chunk, sizeof(chunk), 0);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) throw system_error("receive");
            if (!n) throw std::runtime_error("server disconnected");
            buffered_.append(chunk, static_cast<size_t>(n));
        }
    }
    std::string request(const std::string& line) { send(line + '\n'); return receive(); }
};
}
