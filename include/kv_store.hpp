#pragma once
#include "write_ahead_log.hpp"
#include <optional>
#include <shared_mutex>
#include <unordered_map>
namespace kv {
class KeyValueStore {
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
    WriteAheadLog wal_;
public:
    explicit KeyValueStore(const std::string& path, Durability policy = Durability::Sync);
    void set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key) const;
    bool remove(const std::string& key);
    bool exists(const std::string& key) const;
    size_t size() const;
    void flush();
};
}
