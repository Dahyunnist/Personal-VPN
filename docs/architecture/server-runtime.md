# Server runtime

`TlsTunnelServer` composes the authenticated session transport, lease manager, bounded
Linux TUN adapter, and destination router into one multi-client runtime.

## Connection lifecycle

1. The listener accepts TCP only while the configured session limit has capacity.
2. `TlsTunnelSession` completes mTLS and derives the certificate fingerprint.
3. `CLIENT_HELLO` acquires an exclusive, generation-tagged lease.
4. The session registers its virtual IPv4 address before `IP_ASSIGN` is sent.
5. Valid client IPv4 frames are queued to TUN; TUN packets are routed by destination
   lease to the owning session.
6. Close or transport failure releases the exact lease generation, removes the route,
   and erases the session from the listener registry.

## Safety policy

- The default active-session limit is 1024 and can be lowered at construction time.
  Connections above the limit are closed before allocating TLS/session state.
- A certificate identity may hold only one live lease. A concurrent connection with
  the same certificate receives `LeaseUnavailable` and cannot disturb the first
  session.
- Reissued leases carry a monotonically increasing identifier. A stale session cannot
  renew or release a newer lease, even if identity and address were reused.
- The listener registry is confined to one Asio strand. Individual sessions keep their
  own strands, while the lease manager and router provide thread-safe shared state.
- An unrecoverable accept or TUN error stops the runtime rather than entering a retry
  loop or continuing with partial packet forwarding.
- `SO_REUSEADDR` is enabled before bind; listener setup failures are reported by
  constructor exceptions.

The integration test runs the server on four I/O workers with two distinct client
certificates, a third duplicate-identity connection, and an over-capacity connection.
It verifies distinct leases, duplicate rejection, admission control, bidirectional TUN
traffic, route isolation, and complete cleanup.
