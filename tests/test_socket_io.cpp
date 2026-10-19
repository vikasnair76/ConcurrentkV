#include "test_support.hpp"
#include "socket_io.hpp"
#include <fcntl.h>
int main() {
    int pair[2]; CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    kv::Fd sender(pair[0]), receiver(pair[1]);
    for (int fd : pair) CHECK(::fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
#ifdef SO_NOSIGPIPE
    int one = 1; CHECK(::setsockopt(sender.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) == 0);
#endif
    int small = 1024; CHECK(::setsockopt(sender.get(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    std::string output(1024*1024, 'x'); size_t offset = 0;
    CHECK(kv::write_pending(sender.get(), output, offset) == kv::WriteResult::Pending);
    CHECK(offset > 0 && offset < output.size()); // A genuine kernel short write.
    while (true) { auto before = offset; CHECK(kv::write_pending(sender.get(), output, offset) == kv::WriteResult::Pending); if (offset == before) break; }
    size_t blocked_offset = offset;
    CHECK(kv::write_pending(sender.get(), output, offset) == kv::WriteResult::Pending);
    CHECK(offset == blocked_offset); // EAGAIN leaves unsent bytes intact.
    std::string received; char buffer[4096];
    while (received.size() < output.size()) {
        ssize_t n = ::recv(receiver.get(), buffer, sizeof(buffer), 0);
        if (n > 0) received.append(buffer, static_cast<size_t>(n));
        else CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        if (offset < output.size()) CHECK(kv::write_pending(sender.get(), output, offset) != kv::WriteResult::Closed);
    }
    CHECK(offset == output.size()); CHECK(received == output);
    receiver = kv::Fd(); offset = 0;
    CHECK(kv::write_pending(sender.get(), output, offset) == kv::WriteResult::Closed);
    std::cout << "short send, EAGAIN, byte preservation and broken peer checks passed\n";
}
