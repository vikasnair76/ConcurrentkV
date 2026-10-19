#include "write_ahead_log.hpp"
#include <array>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
namespace kv {
namespace {
void put32(std::string& s, uint32_t n) {
    for (int shift = 24; shift >= 0; shift -= 8) s.push_back(static_cast<char>(n >> shift));
}
uint32_t get32(const char* p) {
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n = (n << 8) | static_cast<unsigned char>(p[i]);
    return n;
}
uint32_t crc32(const std::string& s) {
    uint32_t crc = ~0u;
    for (unsigned char c : s) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
size_t read_bytes(int fd, char* p, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t r = ::read(fd, p + done, n - done);
        if (r < 0) { if (errno == EINTR) continue; throw system_error("read WAL"); }
        if (!r) break;
        done += static_cast<size_t>(r);
    }
    return done;
}
}
WriteAheadLog::WriteAheadLog(const std::string& path, Durability policy) : policy_(policy) {
    // Sync the containing directory so a newly created WAL name survives a crash.
    auto slash = path.find_last_of('/');
    std::string parent = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    Fd directory(::open(parent.c_str(), O_RDONLY | O_CLOEXEC));
    if (directory.get() < 0) throw system_error("open WAL directory");
    fd_ = Fd(::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600));
    if (fd_.get() < 0) throw system_error("open WAL");
    if (::flock(fd_.get(), LOCK_EX | LOCK_NB) != 0) throw system_error("lock WAL (another server?)");
    if (::fsync(directory.get()) != 0) throw system_error("sync WAL directory");
}
void WriteAheadLog::replay(const std::function<void(bool, const std::string&, const std::string&)>& apply) {
    if (::lseek(fd_.get(), 0, SEEK_SET) < 0) throw system_error("seek WAL");
    off_t good = 0;
    for (;;) {
        std::array<char, 12> header{};
        size_t n = read_bytes(fd_.get(), header.data(), header.size());
        if (!n) break;
        if (n != header.size()) {
            if (::ftruncate(fd_.get(), good) != 0) throw system_error("truncate WAL tail");
            break;
        }
        if (std::string(header.data(), 4) != "CKV1") throw std::runtime_error("invalid WAL magic/version");
        auto length = get32(header.data() + 4);
        if (length < 9 || length > 9 + max_key + max_value) throw std::runtime_error("invalid WAL length");
        std::string body(length, '\0');
        if (read_bytes(fd_.get(), body.data(), length) != length) {
            if (::ftruncate(fd_.get(), good) != 0) throw system_error("truncate WAL tail");
            break;
        }
        if (crc32(body) != get32(header.data() + 8)) throw std::runtime_error("WAL checksum mismatch");
        bool set = body[0] == 'S';
        uint32_t kl = get32(body.data() + 1), vl = get32(body.data() + 5);
        if ((body[0] != 'S' && body[0] != 'D') || kl > max_key || vl > max_value ||
            9ull + kl + vl != length || (!set && vl)) throw std::runtime_error("invalid WAL record");
        std::string key = body.substr(9, kl), value = body.substr(9 + kl, vl);
        if (!valid_key(key) || !valid_value(value)) throw std::runtime_error("invalid WAL payload");
        apply(set, key, value);
        good += static_cast<off_t>(12 + length);
    }
    sync();
}
void WriteAheadLog::append(bool set, const std::string& key, const std::string& value) {
    if (failed_) throw std::runtime_error("WAL is failed; restart required");
    std::string body(1, set ? 'S' : 'D');
    put32(body, static_cast<uint32_t>(key.size())); put32(body, static_cast<uint32_t>(value.size()));
    body += key; body += value;
    std::string record = "CKV1";
    put32(record, static_cast<uint32_t>(body.size())); put32(record, crc32(body)); record += body;
    size_t done = 0;
    while (done < record.size()) {
        ssize_t n = ::write(fd_.get(), record.data() + done, record.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { failed_ = true; throw system_error("append WAL"); }
        done += static_cast<size_t>(n);
    }
    if (policy_ == Durability::Sync) sync();
}
void WriteAheadLog::sync() {
    if (failed_) throw std::runtime_error("WAL failed; durability uncertain");
    int result;
    do { result = ::fsync(fd_.get()); } while (result < 0 && errno == EINTR);
    if (result < 0) { failed_ = true; throw system_error("fsync WAL"); }
}
}
