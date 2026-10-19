# Storage and engineering decisions

## WAL v1

All integers are unsigned, 32-bit, big endian. Each record:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `CKV1` magic/version |
| 4 | 4 | Body length |
| 8 | 4 | IEEE CRC-32 of body (polynomial 0xedb88320) |
| 12 | 1 | `S` SET or `D` DELETE |
| 13 | 4 | Key length |
| 17 | 4 | Value length (zero for DELETE) |
| 21 | variable | Key then value, no terminators |

Replay validates bounded lengths, checksum, opcode, and payload before applying. An incomplete final header/body is truncated to the last complete record and the repaired log is synced. A complete corrupt record causes startup failure and the file is preserved. Manual recovery should begin with a copy of the original file. A damaged length that still looks plausible can be indistinguishable from an incomplete final body; CRC is not protection against malicious modifications. The format is versioned, but there is no migration implementation yet.

An exclusive advisory flock prevents cooperating processes from sharing the WAL. Use a local filesystem with POSIX append/locking/fsync semantics. The containing directory is synced when opening the WAL, making initial file creation persistent on supporting filesystems. Filesystem/storage devices must honor fsync; this is not a guarantee against hardware that lies about flushes. Creation of ancestor directories is the operator's responsibility.

`--durability sync` is the default: each successful SET or effective DELETE appends the full record and completes fsync before changing memory and acknowledging. An acknowledged write survives process failure and, under the stated storage assumptions, machine failure. A record may be durable even if the client never receives OK. On write/fsync errors the WAL is poisoned and later mutations fail; a failed command may appear after recovery. No rollback or exactly-once retry guarantee is implied.

`--durability shutdown` appends before modifying memory but syncs only at startup replay or orderly shutdown. It is suitable for throughput experiments where losing acknowledged writes after OS/power failure is acceptable. Kernel writes often survive a process-only crash; this policy makes no per-acknowledgement crash durability guarantee. It does not implement periodic syncing.

## Deliberate limits

Single-node, single map, global mutation lock; no transactions, TTL, authentication, TLS, memory quota, log compaction, online snapshots, or replication. WAL and memory can grow until disk/memory is exhausted. Bind defaults to loopback. Do not expose directly to the public internet. No claim of production readiness or distributed consensus. No third-party runtime libraries beyond libc/libstdc++ (libc++ on Apple).

## Distributed milestones

1. Add consistent snapshots and crash-safe WAL compaction, version compatibility, and explicit log sequence numbers.
2. Introduce a tested Raft implementation with persistent term/vote/log metadata and deterministic state-machine application; no homegrown mock election.
3. A leader proposes commands, replicates entries, and acknowledges after a quorum commits and the state machine applies. Followers serve only reads with explicitly defined consistency.
4. Add election timeouts, failure detection, term fencing, leader failover, membership changes, and log conflict recovery.
5. Transfer snapshots for lagging replicas; test partitions, disk failures, duplicate proposals, restart, and stale leaders.

These are a roadmap. Current acknowledgements involve one local WAL, and the implementation has no consensus/replication stubs.
