# Observability and runtime limits

The server exposes bounded, monotonic in-process counters through `ServerMetrics` and
emits metrics at a configurable interval. JSON Lines is the default; versioned
Prometheus text exposition is available with `--metrics-format prometheus`. Both go to
stdout for journald/log-collector ingestion. The server does not open an unauthenticated
metrics listener.

```json
{"event":"server_metrics","schema_version":1,"accepted":12,"capacity_rejections":1,"handshake_failures":2,"handshake_timeouts":1,"idle_timeouts":3,"established":8,"closed":4,"active":4,"queue_overflows":0,"uplink_packets":942,"uplink_bytes":120576,"downlink_packets":881,"downlink_bytes":112768}
```

Prometheus output starts with `personal_vpn_metrics_schema_info{version="1"} 1`;
counter names end in `_total`, and `personal_vpn_active_sessions` is a gauge. Metrics
have no client-specific labels, so cardinality stays bounded and identities remain
private. JSON `schema_version` and the Prometheus schema-info gauge define schema v1.
`--metrics-interval 0` disables periodic output. Counters include accepted and
capacity-rejected sockets, TLS failures/timeouts, established and closed sessions,
idle timeouts, outbound queue overflows, and packet/byte totals in both directions.
No certificate subject, fingerprint, client address, credential path, or packet
content is written to the periodic record.

## Denial-of-service boundaries

- `--max-sessions` bounds admitted TCP/TLS sessions.
- `--handshake-timeout` closes a TCP client that does not finish mutual TLS within
  1-300 seconds (10 seconds by default).
- `--idle-timeout` closes an authenticated connection that produces no complete
  protocol frame (300 seconds by default). Partial-frame trickling does not reset it.
- Every session has one serialized write with a default maximum of 256 frames or
  1 MiB queued data.
- Protocol payloads, TUN packets, worker threads, leases, client routes, profile size,
  and Wintun ring size all have explicit upper bounds.

Timeout handlers run on the same per-session strand as reads, writes, and shutdown,
so a deadline and a successful operation cannot concurrently release the same lease
or socket. Tests use 100 ms deadlines to prove both slow-handshake and post-handshake
idle clients are closed and counted exactly once.
