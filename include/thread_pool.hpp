#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace kv {
class ThreadPool {
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    size_t capacity_;
    bool stopping_ = false;
public:
    ThreadPool(size_t workers, size_t capacity);
    ~ThreadPool();
    bool submit(std::function<void()> task);
    size_t depth() const;
    size_t capacity() const { return capacity_; }
    void shutdown();
};
}
