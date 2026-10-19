#include "test_support.hpp"
#include "command_parser.hpp"
int main() {
    Temp temp;
    kv::KeyValueStore store(temp.wal()); kv::Metrics metrics; kv::ThreadPool pool(1, 1);
    CHECK(!store.exists("missing")); CHECK(!store.get("missing")); CHECK(!store.remove("missing"));
    store.set("a", "one"); store.set("a", "two"); CHECK(store.get("a") == "two"); CHECK(store.size() == 1);
    store.set("empty", ""); CHECK(store.get("empty") == "");
    CHECK(store.remove("a")); CHECK(!store.exists("a"));
    fails([&] { store.set("bad key", "v"); });
    fails([&] { store.set("k", "bad\nvalue"); });
    auto run = [&](const auto& line) { return kv::execute(line, store, metrics, pool); };
    CHECK(run("GET absent") == "NOT_FOUND\n"); CHECK(run("SET x hello world ") == "OK\n");
    CHECK(run("GET x") == "VALUE hello world \n"); CHECK(run("EXISTS x") == "EXISTS 1\n");
    CHECK(run("DELETE x") == "OK\n"); CHECK(run("EXISTS x") == "EXISTS 0\n");
    CHECK(run("SET x ") == "OK\n"); CHECK(run("GET x") == "VALUE \n");
    for (auto line : {"", "wat", "GET", "GET x y", "SET x", "SET  v", "STATS x", "GET x\r"}) CHECK(run(line).starts_with("ERR"));
    CHECK(run(std::string(kv::max_command+1, 'a')) == "ERR TOO_LARGE\n");
    CHECK(run("STATS").starts_with("STATS accepted="));
    CHECK(metrics.requests == metrics.success + metrics.failed);
    std::cout << "store/parser checks passed\n";
}
