#include "test_support.hpp"
#include "kv_store.hpp"
#include <fstream>
int main() {
    Temp temp;
    { kv::KeyValueStore store(temp.wal()); store.set("k", "old"); store.set("k", "new"); store.set("deleted", "v"); store.remove("deleted"); }
    auto valid = std::filesystem::file_size(temp.wal());
    { kv::KeyValueStore replay(temp.wal()); CHECK(replay.get("k") == "new"); CHECK(!replay.exists("deleted"));
      fails([&] { kv::KeyValueStore duplicate(temp.wal()); }); }
    { std::ofstream out(temp.wal(), std::ios::binary|std::ios::app); out << "CKV"; }
    { kv::KeyValueStore replay(temp.wal()); CHECK(replay.get("k") == "new"); CHECK(std::filesystem::file_size(temp.wal()) == valid); replay.set("tail", "a value"); }
    std::filesystem::resize_file(temp.wal(), std::filesystem::file_size(temp.wal())-3);
    { kv::KeyValueStore replay(temp.wal()); CHECK(!replay.exists("tail")); CHECK(replay.get("k") == "new"); CHECK(std::filesystem::file_size(temp.wal()) == valid); replay.set("after", "repair"); }
    { kv::KeyValueStore replay(temp.wal()); CHECK(replay.get("after") == "repair"); }
    { std::fstream file(temp.wal(), std::ios::binary|std::ios::in|std::ios::out); file.seekp(21); file.put('!'); }
    auto corrupt_size = std::filesystem::file_size(temp.wal());
    fails([&] { kv::KeyValueStore corrupt(temp.wal()); });
    CHECK(std::filesystem::file_size(temp.wal()) == corrupt_size);
    { std::ofstream file(temp.wal(), std::ios::binary|std::ios::trunc); file << "BAD!abcdefgh"; }
    fails([&] { kv::KeyValueStore corrupt(temp.wal()); });
    std::cout << "restart, lock, truncated header/body, corruption checks passed\n";
}
