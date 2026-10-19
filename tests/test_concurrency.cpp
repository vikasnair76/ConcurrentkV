#include "test_support.hpp"
#include "kv_store.hpp"
#include "thread_pool.hpp"
#include <future>
#include <thread>
int main() {
    Temp temp;
    std::string final;
    {
        kv::KeyValueStore store(temp.wal(), kv::Durability::Shutdown);
        std::vector<std::thread> workers;
        for (int i = 0; i < 8; ++i) workers.emplace_back([&, i] {
            for (int j = 0; j < 250; ++j) {
                auto key = std::to_string(i)+":"+std::to_string(j);
                store.set(key, "value"); CHECK(store.get(key) == "value");
                store.set("shared", key); CHECK(store.exists("shared"));
                if (j%2) CHECK(store.remove(key));
            }
        });
        for (auto& t : workers) t.join();
        CHECK(store.size() == 1001); final = *store.get("shared"); store.flush();
    }
    { kv::KeyValueStore replay(temp.wal()); CHECK(replay.get("shared") == final); CHECK(replay.size() == 1001); }
    kv::ThreadPool pool(1, 1);
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    CHECK(pool.submit([&] { entered.set_value(); released.wait(); })); entered.get_future().wait();
    std::atomic<int> completed{0};
    CHECK(pool.submit([&] { ++completed; })); CHECK(pool.depth() == 1);
    CHECK(!pool.submit([] {})); release.set_value(); pool.shutdown();
    CHECK(completed == 1); CHECK(!pool.submit([] {}));
    std::cout << "concurrency, WAL order, saturation and drain checks passed\n";
}
