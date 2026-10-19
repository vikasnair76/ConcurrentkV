#include "client_connection.hpp"
#include <algorithm>
#include <barrier>
#include <charconv>
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>
using Clock = std::chrono::steady_clock;
struct Result { std::vector<double> success_latency, failed_latency; };
size_t number(const std::string& s, size_t maximum, bool zero = false) {
    size_t n = 0; auto [p, ec] = std::from_chars(s.data(), s.data()+s.size(), n);
    if (ec != std::errc() || p != s.data()+s.size() || (!zero && !n) || n > maximum) throw std::invalid_argument("invalid number: " + s);
    return n;
}
void report(const char* label, std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    double sum = 0; for (double n : samples) sum += n;
    auto percentile = [&](double p) { return samples.empty() ? 0 : samples[static_cast<size_t>(p*(samples.size()-1))]; };
    std::cout << label << "=" << samples.size() << " avg_us=" << (samples.empty()?0:sum/samples.size())
              << " p50_us=" << percentile(.50) << " p95_us=" << percentile(.95) << " p99_us=" << percentile(.99) << '\n';
}
int main(int argc, char** argv) {
    try {
        std::string host = "127.0.0.1", port = "8080";
        size_t connections = 8, requests = 2000, warmup = 100, keys = 1000, value_size = 64, set_percent = 20;
        for (int i = 1; i < argc; i += 2) {
            if (i+1 >= argc) throw std::invalid_argument("expected --option value");
            std::string arg = argv[i], value = argv[i+1];
            if (arg == "--host") host = value;
            else if (arg == "--port") port = value;
            else if (arg == "--connections") connections = number(value, 512);
            else if (arg == "--requests") requests = number(value, 10000000);
            else if (arg == "--warmup") warmup = number(value, 1000000);
            else if (arg == "--keys") keys = number(value, 1000000);
            else if (arg == "--value-size") value_size = number(value, kv::max_value);
            else if (arg == "--set-percent") set_percent = number(value, 100, true);
            else throw std::invalid_argument("unknown benchmark option");
        }
        std::string value(value_size, 'x');
        { kv::ClientConnection preload(host, port);
          for (size_t i = 0; i < keys; ++i) if (preload.request("SET bench:"+std::to_string(i)+" "+value) != "OK") throw std::runtime_error("preload failed"); }
        std::vector<Result> results(connections);
        std::barrier phase(static_cast<std::ptrdiff_t>(connections + 1));
        std::vector<std::thread> threads;
        // Connect before launching: connection failure cannot strand a barrier participant.
        std::vector<std::unique_ptr<kv::ClientConnection>> sockets;
        for (size_t i = 0; i < connections; ++i) sockets.push_back(std::make_unique<kv::ClientConnection>(host, port));
        for (size_t i = 0; i < connections; ++i) threads.emplace_back([&, i] {
            std::mt19937 random(static_cast<unsigned>(42+i));
            auto run = [&](bool measure) {
                size_t count = measure ? requests : warmup;
                for (size_t j = 0; j < count; ++j) {
                    auto key = "bench:"+std::to_string(random()%keys);
                    bool set = random()%100 < set_percent;
                    auto start = Clock::now(); bool ok = false;
                    try { auto response = sockets[i]->request(set ? "SET "+key+" "+value : "GET "+key);
                          ok = set ? response == "OK" : response == "VALUE "+value; }
                    catch (const std::exception&) {}
                    if (measure) (ok ? results[i].success_latency : results[i].failed_latency).push_back(std::chrono::duration<double, std::micro>(Clock::now()-start).count());
                }
            };
            run(false); phase.arrive_and_wait(); phase.arrive_and_wait(); run(true);
        });
        phase.arrive_and_wait(); auto start = Clock::now(); phase.arrive_and_wait();
        for (auto& thread : threads) thread.join();
        double seconds = std::chrono::duration<double>(Clock::now()-start).count();
        std::vector<double> success, failed;
        for (auto& r : results) { success.insert(success.end(), r.success_latency.begin(), r.success_latency.end()); failed.insert(failed.end(), r.failed_latency.begin(), r.failed_latency.end()); }
        std::cout << "seed=42 connections=" << connections << " requests_per_connection=" << requests
                  << " warmup_per_connection=" << warmup << " keys=" << keys << " value_bytes=" << value_size << " set_percent=" << set_percent
                  << " seconds=" << seconds << " attempted_rps=" << (success.size()+failed.size())/seconds << " successful_rps=" << success.size()/seconds << '\n';
        report("successful", success); report("failed", failed);
        return failed.empty() ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
