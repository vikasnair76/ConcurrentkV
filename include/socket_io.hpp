#pragma once
#include <cerrno>
#include <string>
#include <sys/socket.h>
namespace kv {
enum class WriteResult { Pending, Done, Closed };
// Exactly one send attempt: the reactor retains the offset across short writes/EAGAIN.
inline WriteResult write_pending(int fd, const std::string& output, size_t& offset) {
#ifdef MSG_NOSIGNAL
    ssize_t n = ::send(fd, output.data()+offset, output.size()-offset, MSG_NOSIGNAL);
#else
    ssize_t n = ::send(fd, output.data()+offset, output.size()-offset, 0);
#endif
    if (n > 0) {
        offset += static_cast<size_t>(n);
        return offset == output.size() ? WriteResult::Done : WriteResult::Pending;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return WriteResult::Pending;
    return WriteResult::Closed;
}
}
