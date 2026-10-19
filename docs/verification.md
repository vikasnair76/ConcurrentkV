# Local verification

Executed October 19, 2026 on macOS 26.5.2, Apple M3 (8 logical CPUs, 8 GB RAM), arm64, AppleClang 21.0.0. CMake builds used C++20 with warnings enabled. No third-party runtime libraries were installed.

| Configuration | Result | Elapsed |
| --- | --- | --- |
| Release | 6/6 CTest suites passed | 5.70 s |
| Debug + AddressSanitizer/UndefinedBehaviorSanitizer | 6/6 passed; no sanitizer reports | 7.70 s |
| Debug + ThreadSanitizer | 6/6 passed; no race reports | 12.64 s |

Raw final [Release](results/release-tests.txt), [ASan/UBSan](results/asan-tests.txt), and [TSan](results/tsan-tests.txt) logs and build output are retained in `results/`. During development, all six Release suites also passed ten consecutive repetitions after correcting reset-socket handling. Final logs above follow the final signal-notification and logging changes.

## Coverage

- `store`: SET/GET/DELETE/EXISTS, missing keys, duplicate updates, empty/space-containing values, malformed/unknown/oversized commands, metrics.
- `concurrency`: eight concurrent writer/readers, 4000 SETs plus deletes, duplicate-key mutation ordering verified against replay; deterministic blocked worker, full queue rejection, depth, drain, and admission after shutdown.
- `recovery`: SET/DELETE replay, exclusive WAL process lock, incomplete header/body truncation, repair then new append/restart, checksum and magic corruption. Corrupt complete files remain unmodified.
- `network`: twelve idle connections with two workers, fragmented/pipelined requests, bounded large values, slow receive, read half-close, overlong input, reset peers, STATS, connection/request limits, shutdown notification, network restart recovery.
- `socket_io`: socketpair with constrained kernel send buffer; verifies a genuine short send, EAGAIN without offset loss, byte-perfect eventual output, and a broken peer without SIGPIPE termination.
- `process`: launches the actual server via posix_spawn; SIGTERM notification/exit, same-WAL restart, SIGKILL after a synchronous acknowledged write, recovery of that write, idle timeout, and SIGINT exit.

An aborted/reset connection during accept or per-socket configuration is isolated to that connection. This was tested across repeated runs, including the macOS case where SO_NOSIGPIPE returns EINVAL after a peer resets. Test diagnostic logging was removed after identifying the failure; the final TSan run has no concurrent unprotected test-stream writes.

The [end-to-end transcript](results/demo.txt) uses the actual `kv_client` executable with stdin commands, then stops/restarts the server and reads/deletes the recovered value. The [benchmark report](performance.md) documents separately collected measurements.

## Validation boundaries

Linux and Docker are supported targets in source/configuration, **not locally executed platforms** in this session. Docker CLI is installed but the daemon socket is absent. The multistage image builds and runs CTest when a daemon is available. GitHub Actions configures Ubuntu and macOS builds with separate sanitizer variants; remote CI has not been run here.

No power-loss simulation, disk-full/fsync fault injection, machine-wide leak instrumentation, fuzzing, or long-duration soak test was performed. Sanitizer passes cover the exercised paths and are not proof that every possible execution is race-free. Persistence guarantees depend on the documented local-filesystem/fsync contract.

## Resume bullets (implemented and verified)

- Built a C++20 TCP key-value server with a nonblocking POSIX poll reactor, fixed worker pool, bounded task queue, per-connection backpressure, and concurrent reads protected by shared_mutex.
- Implemented a versioned CRC-32 write-ahead log with fsync-before-acknowledgement durability, ordered mutation replay, torn-tail repair, and verified recovery after SIGKILL and restart.
- Developed six CTest suites covering concurrency, TCP framing, kernel short writes/EAGAIN, overload, corruption, and shutdown; passed Release, ASan/UBSan, and TSan builds and measured mixed-workload throughput with a C++ load generator.
