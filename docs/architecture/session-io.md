# Session I/O invariants

The redesigned server transport separates byte transport, protocol framing, session policy, and TUN delivery. `TlsTunnelSession` owns only transport orchestration; it delegates parsing to `FrameDecoder`, authorization to `SessionController`, and buffering limits to `OutboundFrameQueue`.

## Concurrency

Every callback and externally requested send is dispatched through one Boost.Asio strand per session. Session state, decoder state, and queue state are therefore never accessed concurrently even when the server runs one `io_context` on multiple worker threads.

## Read path

1. TLS produces an arbitrary byte chunk.
2. `FrameDecoder` emits zero or more complete frames and retains any partial frame.
3. `SessionController` validates message order, sequence, lease ownership, control payloads, and IPv4 source policy.
4. Only authorized complete IPv4 packets are passed to the TUN sink.

A framing or transport error closes the connection. A policy error queues one bounded `ERROR` frame and closes after that write completes.

## Write path

1. A frame is fully encoded into an owning byte vector.
2. The byte vector is inserted into a bounded FIFO queue.
3. Exactly one `async_write` is outstanding for the session.
4. Its completion handler removes the front buffer and starts the next write.

The completion handler also captures the current shared buffer. The bytes therefore remain alive until the asynchronous operation completes. This replaces the legacy code that passed pointers to temporary packet storage.

The initial limits are 256 frames and 1 MiB per session. A client that cannot drain its queue is closed instead of consuming unbounded server memory.

## Current boundary

The transport library is built on Linux in both Debug and Release configurations. TLS handshake, peer-certificate identity extraction, TUN dispatch, and end-to-end loopback tests are added in the following security/server integration phases.
