#pragma once
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
namespace kv {
class Fd {
    int fd_ = -1;
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd_(other.release()) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) { if (fd_ >= 0) ::close(fd_); fd_ = other.release(); }
        return *this;
    }
    int get() const { return fd_; }
    int release() { int fd = fd_; fd_ = -1; return fd; }
};
inline std::runtime_error system_error(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}
inline constexpr size_t max_key = 256, max_value = 7900, max_command = 8192, max_input = 16384;
inline bool valid_key(const std::string& key) {
    if (key.empty() || key.size() > max_key) return false;
    for (unsigned char c : key) if (c <= 32 || c > 126) return false;
    return true;
}
inline bool valid_value(const std::string& value) {
    if (value.size() > max_value) return false;
    for (unsigned char c : value) if (c < 32 || c > 126) return false;
    return true;
}
}
