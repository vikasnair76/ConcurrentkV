#include "kv_store.hpp"
#include <mutex>
namespace kv {
KeyValueStore::KeyValueStore(const std::string& path, Durability policy) : wal_(path, policy) {
    wal_.replay([this](bool set, const auto& key, const auto& value) {
        if (set) data_[key] = value; else data_.erase(key);
    });
}
void KeyValueStore::set(const std::string& key, const std::string& value) {
    if (!valid_key(key) || !valid_value(value)) throw std::invalid_argument("invalid key/value");
    std::unique_lock lock(mutex_);
    // Allocate before committing the WAL; swap/erase after append cannot allocate.
    auto [it, inserted] = data_.try_emplace(key);
    try {
        std::string replacement(value);
        wal_.append(true, key, value);
        it->second.swap(replacement);
    } catch (...) { if (inserted) data_.erase(it); throw; }
}
std::optional<std::string> KeyValueStore::get(const std::string& key) const {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    return it->second;
}
bool KeyValueStore::remove(const std::string& key) {
    if (!valid_key(key)) throw std::invalid_argument("invalid key");
    std::unique_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    wal_.append(false, key, "");
    data_.erase(it);
    return true;
}
bool KeyValueStore::exists(const std::string& key) const { return get(key).has_value(); }
size_t KeyValueStore::size() const { std::shared_lock lock(mutex_); return data_.size(); }
void KeyValueStore::flush() { std::unique_lock lock(mutex_); wal_.sync(); }
}
