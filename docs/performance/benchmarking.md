# Performance and stability validation

Performance claims in this repository must be reproducible and must distinguish the
protocol codec from a real end-to-end tunnel. A codec microbenchmark is useful for
regression detection, but it does not include TLS, kernel/user copies, Wintun/TUN,
routing, NAT, packet loss, or network latency.

## Protocol microbenchmark

Build a release-only benchmark without the platform transports:

```bash
cmake -S . -B out/benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_TESTS=OFF \
  -DPERSONAL_VPN_BUILD_BENCHMARKS=ON
cmake --build out/benchmark
out/benchmark/protocol-benchmark 200000
```

The program prints one JSON record so CI and later comparison tools can retain the
raw result. Never use a microbenchmark result as the advertised tunnel throughput.

### Recorded development result

The following smoke result verifies the harness rather than establishing a product
SLA:

| Date | Platform | Compiler | Frame | Iterations | Encode | Decode |
|---|---|---|---:|---:|---:|---:|
| 2026-10-20 | Windows 10 x64 | GCC 14.2.0 (MSYS2) | 1,400-byte IPv4 payload | 200,000 | 3,761.38 MiB/s | 4,471.05 MiB/s |

Raw result:

```json
{"benchmark":"protocol_frame_1400","iterations":200000,"encoded_bytes":1420,"encode_mib_per_second":3761.38,"decode_mib_per_second":4471.05,"checksum":73977231}
```

## End-to-end tunnel experiment

Use two otherwise idle physical or virtual hosts on the same controlled network:

- Windows client: client build identifier, CPU, OS, Wintun version, Ethernet speed.
- Linux server: server build identifier, CPU, kernel, OpenSSL version, NIC speed.
- Network: topology, link speed, MTU, RTT, and whether either host is virtualized.
- Configuration: cipher negotiated, tunnel MTU, route set, server thread count, and
  number of clients.

Measure a direct baseline first, then the same traffic through the VPN. Use at least
three trials after one warm-up and retain the raw `iperf3 -J` output.

```bash
# On the destination reachable through the Linux server:
iperf3 --server

# Direct baseline and VPN path, each repeated at least three times:
iperf3 --client <destination> --time 30 --parallel 1 --json
iperf3 --client <destination> --time 30 --parallel 4 --json
ping -n 100 <destination>       # Windows
ping -c 100 <destination>       # Linux
```

Report median throughput, p50/p95 RTT, packet loss, client/server CPU, and peak RSS.
Calculate overhead as `(direct - vpn) / direct * 100%`. Do not compare runs that use
different endpoints, routes, MTUs, or link conditions.

## Scaling matrix

Run the same workload at 1, 5, 10, and 50 clients where the lab can provide those
clients. Record aggregate and per-client throughput, server CPU/RSS, open file count,
TLS handshake failures, rejected sessions, and reconnect time. The configured
`--max-sessions` value must be tested both below and above its boundary.

## Soak and fault tests

Run 1-hour, 6-hour, and 24-hour soaks. Sample every 60 seconds:

- tunnel reachability and RTT;
- client/server RSS and CPU;
- active sessions, bytes, frames, drops, and protocol errors from JSON metrics;
- reconnect events and the Windows route table before and after shutdown.

Inject one fault at a time: server process restart, network interruption, expired or
revoked client certificate, malformed frame, full outbound queue, handshake timeout,
and idle timeout. A passing run has no crash or unbounded memory growth, reconnects
according to the documented policy, and leaves no Wintun address or route after the
client exits.

Keep raw results under a dated external evidence directory. Commit only sanitized
summaries: packet captures, profiles, hostnames, IP addresses, and certificate details
may contain sensitive data.
