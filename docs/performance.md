# Measured performance

Collected October 19, 2026, using the final Release build on macOS 26.5.2, Apple M3, 8 logical CPUs (4 performance/4 efficiency), 8 GB RAM, arm64, AppleClang 21.0.0. Server and client ran on the same host over IPv4 loopback. Storage was the local filesystem under the user's Documents directory; physical storage model and cache state were not controlled. [System metadata](results/system.json).

These are **short smoke benchmarks**, not production capacity measurements. Each policy ran three times with a fresh WAL and a new server process. Other desktop workloads were not controlled; the runs show substantial variance. No sanitizer or build jobs were intentionally run during these final samples. No performance numbers are inferred or invented.

Configuration: 4 server workers, queue capacity 1024, 8 client connections, 1000 preloaded keys, 64-byte values, 20% SET / 80% GET (all expected hits), seed 42 + connection index, 100 warmup requests per connection, then 2000 measured requests per connection. Preload/warmup are excluded from timing. Each sample comprises 16000 measured requests; all six samples had **zero failures**. Values remain constant so concurrent updates cannot create false read validation failures.

## Median of three runs

Each column is independently the median of that metric across three runs; it does not represent a single combined latency distribution.

| WAL policy | Successful requests/s | Average µs | p50 µs | p95 µs | p99 µs |
| --- | ---: | ---: | ---: | ---: | ---: |
| sync (fsync before mutation acknowledgement) | 25,188.7 | 289.2 | 202.0 | 638.7 | 1,183.7 |
| shutdown (no per-write fsync) | 31,963.0 | 215.3 | 153.7 | 432.2 | 941.4 |

| Sample | sync requests/s | shutdown requests/s |
| --- | ---: | ---: |
| 1 | 38,657.3 | 42,878.1 |
| 2 | 25,188.7 | 31,963.0 |
| 3 | 18,498.7 | 23,432.3 |

Raw output: [sync 1](results/sync-1.txt), [2](results/sync-2.txt), [3](results/sync-3.txt); [shutdown 1](results/shutdown-1.txt), [2](results/shutdown-2.txt), [3](results/shutdown-3.txt). [Machine-readable median summary](results/benchmark-summary.json).

The synchronous policy provides a different guarantee from shutdown-only syncing; compare throughput together with that durability cost. An fsync benchmark does not prove power-loss recovery, storage firmware behavior, or distributed durability.

## Reproduce

Build Release, choose an unused port, and use a fresh, isolated WAL path for every run. In separate terminals:

```sh
./build/kv_server --port 8080 --workers 4 --queue 1024 \
  --wal data/benchmark-sync-1.wal --durability sync

./build/kv_bench --port 8080 --connections 8 --requests 2000 --warmup 100 \
  --keys 1000 --value-size 64 --set-percent 20
```

Stop via SIGTERM; repeat three times with fresh filenames. Repeat for `--durability shutdown`. The benchmark writes `bench:` keys, so never point it at important data. `--requests` and `--warmup` are per connection; `--set-percent 0` supports a read-only measured workload after preload.

Latency uses steady_clock around each synchronous request/response, including network, queueing, execution, locking and fsync. Quantiles sort all samples and choose floor(p × (N−1)). Throughput is completed attempted or successful operations divided by the measured phase's wall time. Successes and failures have separate distributions; samples with failures exit nonzero. Connection creation, preload, warmup, and percentile calculation are excluded from timing; request construction and collection overhead contribute to elapsed throughput.

The generator is closed loop: every connection waits for a response before sending the next request. It does not model externally scheduled arrivals or coordinated-omission correction. It allocates latency samples proportional to measured requests; choose sizes that fit available memory. A connected client uses five-second socket I/O timeouts. A short localhost run can be limited by the generator, shared machine scheduling, and TCP behavior.

For a defensible capacity study, run longer measured phases on a dedicated Linux machine with a separate load host, record storage/fsync characteristics and CPU utilization, vary concurrency/read-write mix/value sizes, report repeated-run intervals, and include disk failure and sustained overload tests. The current data supports a functional performance demonstration only.
