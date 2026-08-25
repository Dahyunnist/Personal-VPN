# Tunnel packet routing

`TunnelRouter` is the boundary between packets read from the Linux TUN device and
authenticated TLS sessions. It maps each server-issued virtual IPv4 lease to a weak
`TunnelPeer` reference.

## Invariants

- Only one live peer may own a virtual address. A second peer cannot replace it.
- Registration by the same peer is idempotent.
- An expired weak reference can be reclaimed without retaining a closed session.
- Unregistration includes the expected peer pointer, so a late close callback cannot
  remove a newer route for a reused address.
- IPv4 version, header length, total length, and destination are validated before a
  packet is delivered.
- Packet buffers are passed by value to preserve ownership across asynchronous session
  strands.
- The registry is mutex-protected. The mutex is released before calling a peer, so
  packet delivery cannot re-enter the registry while it is locked.

The router intentionally does not configure interfaces, execute shell commands, or
hold TLS details. The Linux TUN adapter and session acceptor will compose around this
portable, unit-tested core.
