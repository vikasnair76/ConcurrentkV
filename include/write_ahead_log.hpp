#pragma once
#include "common.hpp"
#include <functional>
namespace kv {
enum class Durability { Sync, Shutdown };
class WriteAheadLog {
    Fd fd_;
    Durability policy_;
    bool failed_ = false;
public:
    WriteAheadLog(const std::string& path, Durability policy);
    void replay(const std::function<void(bool, const std::string&, const std::string&)>& apply);
    void append(bool set, const std::string& key, const std::string& value);
    void sync();
};
}
