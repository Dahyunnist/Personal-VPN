# System architecture

## Trust and ownership boundaries

The Windows and Linux kernels own routing and virtual-interface state. The client and
server own only resources they create for one process lifetime. TLS authenticates a
device certificate, while the protocol state machine authorizes that identity to use
exactly one server-issued address lease. Host firewall/NAT policy remains outside the
VPN process.

```text
Windows kernel                     Untrusted network                Linux kernel
┌──────────────┐                 ┌─────────────────┐              ┌──────────────┐
│ IP stack     │                 │ ciphertext only │              │ routing/NAT  │
│ route table  │                 └────────┬────────┘              │ TUN pvpn0    │
│ Wintun       │                          │                       └──────▲───────┘
└──────┬───────┘                          │                              │
       │ packet                  mutual TLS/TCP                         │ packet
┌──────▼────────────────┐        framed stream        ┌────────────────▼──────┐
│ ClientRuntime         │◄───────────────────────────►│ TlsTunnelServer       │
│ policy + transaction  │                             │ sessions + lease/router│
└───────────────────────┘                             └───────────────────────┘
```

## Outbound packet path

1. A profile route sends a Windows IPv4 packet into the Wintun interface.
2. The client reader copies Wintun-owned bytes before releasing the receive packet.
3. `ClientSessionController` verifies structure, MTU, and the assigned source address.
4. `TlsTunnelClient` encodes `DATA_IPV4`; its strand and bounded queue guarantee one
   outstanding TLS write with owned storage.
5. The server frame decoder reconstructs records across arbitrary TLS read boundaries.
6. The server controller verifies state, sequence, IPv4 length, MTU, and lease source.
7. The TUN device performs one serialized asynchronous write using owned bytes.
8. Linux routing/firewall/NAT forwards the packet according to administrator policy.

## Return packet path

1. Linux routing sends a packet destined for a leased address into `pvpn0`.
2. The single TUN read loop copies it and asks the router for the destination lease.
3. The registry resolves only the current generation owner and releases its mutex
   before invoking the session.
4. The owning TLS session queues one `DATA_IPV4` frame under backpressure.
5. The client verifies sequence, IPv4 length, MTU, and destination lease.
6. The Windows backend allocates a Wintun send packet, copies the bytes, and submits it.
7. The Windows stack delivers the original IP packet to the requesting application.

## Server concurrency model

- One acceptor admits no more than `max_sessions`.
- One `io_context` runs on a bounded thread pool.
- Each TLS stream has a strand; reads, writes, timers, and close transitions cannot
  concurrently mutate session state.
- One TUN owner serializes device reads and writes.
- Per-session and global queues have explicit byte/frame bounds.
- Lease generations prevent a delayed callback from releasing a reassigned address.

## Failure convergence

DNS, TCP, TLS, protocol, timeout, adapter, route, TUN, and callback failures converge
on idempotent stop paths. Timers are cancelled, admissions/routes/leases are released,
outstanding operations drain through their executors, Wintun wait is interrupted, and
all owned threads are joined. No error path invokes shell commands or deletes network
state it did not create.
