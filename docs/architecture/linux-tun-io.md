# Linux TUN I/O

`LinuxTunDevice` owns a non-blocking `/dev/net/tun` descriptor configured with
`IFF_TUN | IFF_NO_PI`. It does not run shell commands or modify host routes,
forwarding, or firewall policy.

## I/O invariants

- All mutable adapter state runs on one Boost.Asio strand.
- Each successful descriptor read produces one owning packet vector.
- Outbound packets remain owned by a bounded FIFO until the kernel write completes.
- Only one packet write is outstanding at a time and partial packet writes fail closed.
- The default outbound limits are 256 packets and 4 MiB. Overflow closes the adapter
  instead of allowing unbounded memory growth.
- Oversized and empty writes are rejected.
- Explicit shutdown cancels pending operations without reporting a fatal device error.
- Fatal read, write, or queue errors close the descriptor and invoke the error handler
  once.

The adapter has a separate `adopt` factory for tests. Linux tests use a packet-preserving
Unix descriptor pair to exercise the same asynchronous read, write, cancellation, and
backpressure code without requiring root privileges or modifying the host network.

Interface address assignment, forwarding, and NAT are deployment responsibilities.
They will be applied declaratively by service-unit and provisioning examples rather
than by concatenating privileged shell commands inside the server process.
