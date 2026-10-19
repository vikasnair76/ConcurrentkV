#include "command_parser.hpp"
namespace kv {
std::string execute(const std::string& line, KeyValueStore& store, Metrics& m, const ThreadPool& pool) {
    auto start = std::chrono::steady_clock::now();
    std::string response;
    try {
        if (line.size() > max_command) response = "ERR TOO_LARGE\n";
        else if (line == "STATS") response = m.snapshot(store.size(), pool.depth(), pool.capacity());
        else {
            auto split = line.find(' ');
            auto verb = line.substr(0, split);
            std::string rest = split == std::string::npos ? "" : line.substr(split + 1);
            if (verb == "SET") {
                ++m.sets;
                auto separator = rest.find(' ');
                if (separator == std::string::npos) response = "ERR SYNTAX\n";
                else {
                    auto key = rest.substr(0, separator), value = rest.substr(separator + 1);
                    if (!valid_key(key) || !valid_value(value)) response = "ERR SYNTAX\n";
                    else { store.set(key, value); response = "OK\n"; }
                }
            } else if (verb == "GET" || verb == "DELETE" || verb == "EXISTS") {
                if (verb == "GET") ++m.gets;
                if (verb == "DELETE") ++m.deletes;
                if (!valid_key(rest)) response = "ERR SYNTAX\n";
                else if (verb == "GET") {
                    auto value = store.get(rest); response = value ? "VALUE " + *value + "\n" : "NOT_FOUND\n";
                } else if (verb == "DELETE") response = store.remove(rest) ? "OK\n" : "NOT_FOUND\n";
                else response = store.exists(rest) ? "EXISTS 1\n" : "EXISTS 0\n";
            } else response = "ERR COMMAND\n";
        }
    } catch (const std::exception&) { response = "ERR STORAGE\n"; }
    if (response.starts_with("ERR")) ++m.failed; else ++m.success;
    m.latency_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    ++m.requests;
    return response;
}
}
