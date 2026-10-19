#include "thread_pool.hpp"
#include <stdexcept>
namespace kv {
ThreadPool::ThreadPool(size_t workers, size_t capacity) : capacity_(capacity) {
    if (!workers || !capacity) throw std::invalid_argument("workers and queue must be positive");
    try {
        for (size_t i = 0; i < workers; ++i) workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                { std::unique_lock lock(mutex_); ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                  if (tasks_.empty()) return;
                  task = std::move(tasks_.front()); tasks_.pop(); }
                task(); // All submitted application tasks catch their own exceptions.
            }
        });
    } catch (...) { shutdown(); throw; }
}
ThreadPool::~ThreadPool() { shutdown(); }
bool ThreadPool::submit(std::function<void()> task) {
    { std::lock_guard lock(mutex_);
      if (stopping_ || tasks_.size() >= capacity_) return false;
      tasks_.push(std::move(task)); }
    ready_.notify_one(); return true;
}
size_t ThreadPool::depth() const { std::lock_guard lock(mutex_); return tasks_.size(); }
void ThreadPool::shutdown() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    ready_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
}
}
