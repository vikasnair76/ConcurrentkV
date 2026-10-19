#pragma once
#include "kv_store.hpp"
#include "metrics.hpp"
#include "thread_pool.hpp"
namespace kv {
std::string execute(const std::string& line, KeyValueStore& store, Metrics& metrics, const ThreadPool& pool);
}
