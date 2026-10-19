# Wire protocol v1

IPv4 TCP, one ASCII command per LF (`0x0a`), one LF-terminated response per command. Commands are case sensitive. CRLF is not accepted. No quoting, backslash escapes, binary data, tabs, or Unicode. Key: 1–256 printable ASCII bytes excluding space (`0x21`–`0x7e`). Value: 0–7900 printable ASCII bytes including space (`0x20`–`0x7e`). SET splits at the first two spaces: everything after the second space is the literal value, preserving leading/trailing spaces. `SET key ` sets an empty value. No second space is a syntax error. Lines are limited to 8192 bytes excluding LF; each connection buffers at most 16384 input bytes and one bounded response. An unterminated line above the limit receives `ERR TOO_LARGE`, then closes.

| Request | Response |
| --- | --- |
| `SET key value` | `OK` |
| `GET key` | `VALUE value` or `NOT_FOUND` |
| `DELETE key` | `OK` or `NOT_FOUND` |
| `EXISTS key` | `EXISTS 1` or `EXISTS 0` |
| `STATS` | `STATS name=value ...` (one line) |

`VALUE ` represents an empty value. Missing keys are normal successful queries. Extra arguments to GET/DELETE/EXISTS/STATS are rejected. Unknown commands return `ERR COMMAND`; malformed arguments return `ERR SYNTAX`. Invalid commands leave the connection usable. `ERR STORAGE` means the mutation's outcome may be uncertain after an I/O failure; restart and inspect the WAL before retrying. Writes are disabled for the lifetime of a failed WAL. Reads remain available.

The queue rejects a command with `ERR BUSY` when full; the client can retry with backoff. A rejected mutation was not executed. Requests on one connection execute sequentially; pipelining is supported. There is no total ordering between different connections beyond the store's mutation lock. Each connection has one operation in flight. Persistent connections close after `--max-requests` commands (including rejected/invalid commands). At `--max-connections`, newly accepted sockets close immediately without a response. Idle/slow clients close after `--idle-seconds` without progress, except during an in-flight operation. These limits bound connections and request buffers, not the number of keys or total WAL/dataset size.

EOF on the read side finishes complete buffered commands and drops an incomplete final command. A full disconnect does not cancel an already submitted mutation. During orderly shutdown, submitted commands finish, their responses are sent where possible, followed by `ERR SHUTDOWN`; unsubmitted input is discarded. Responses have a shared two-second delivery deadline, after which sockets close. A lost response does not imply a lost write.

STATS fields: accepted TCP sockets (including connection-limit rejections), active admitted connections, requests completed, successful/failed command executions, attempted get/set/delete commands, keys, queue_depth (waiting tasks only), queue_capacity, rejected (queue plus connection-limit rejections), uptime_s, avg_latency_us (worker execution time, including storage waits/fsync; excludes queue/network). STATS snapshots are approximate across independent atomic counters and include earlier requests; the STATS command itself is counted after its response is constructed. Framing errors and transport failures are not completed command executions. Failed operations are `ERR` responses from the executor; NOT_FOUND and EXISTS 0 count as successful. Queue rejection is tracked separately.
