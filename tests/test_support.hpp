#pragma once
#include "common.hpp"
#include <filesystem>
#include <iostream>
#include <vector>
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string("check failed: ") + #condition + " at " + __FILE__ + ":" + std::to_string(__LINE__)); } while (false)
struct Temp {
    std::string path;
    Temp() {
        std::string pattern = (std::filesystem::temp_directory_path()/"concurrentkv-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
        char* dir = ::mkdtemp(name.data()); if (!dir) throw kv::system_error("mkdtemp");
        path = dir;
    }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string wal() const { return path + "/data.wal"; }
};
template<class F> void fails(F&& f) { bool thrown = false; try { f(); } catch (const std::exception&) { thrown = true; } CHECK(thrown); }
