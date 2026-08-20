# Client TLS transport

`personal_vpn_client_transport` is the portable network boundary for the redesigned
client. It deliberately does not configure a virtual adapter or routes. Those
privileged operations are supplied through the assignment and packet callbacks,
which keeps transport behavior independently testable.

## Connection sequence

1. The caller creates a TLS context with `make_client_tls_context` and opens the TCP
   socket.
2. `TlsTunnelClient::start` configures SNI for DNS names and performs an asynchronous
   TLS client handshake.
3. OpenSSL verifies the chain and the exact DNS name or IP address configured by the
   caller. The client certificate and matching private key are also mandatory.
4. After the TLS handshake succeeds, the protocol controller sends `CLIENT_HELLO`.
5. The server-authoritative `IP_ASSIGN` is validated before it is published to the
   adapter callback. Only then may IPv4 data flow.

The same server name must be passed to the TLS-context factory and the transport.
Configuration code owns that invariant; a later client-runtime layer will expose a
single validated configuration object so the two values cannot diverge.

## Concurrency and memory ownership

All public operations are dispatched onto one Boost.Asio strand. TLS reads, protocol
state transitions, callbacks, queue mutation, and shutdown therefore have a single
serialized execution order even when the I/O context uses multiple worker threads.

The frame decoder accepts arbitrary TLS record boundaries. Outbound frames are
encoded into owned buffers in a bounded queue, and exactly one `async_write` is active
at a time. The default limit is 256 frames or 1 MiB. Queue exhaustion fails closed
instead of allowing an unbounded memory increase.

## Shutdown contract

An orderly local stop queues one protocol `CLOSE` frame after preceding writes and
closes the socket when the queue drains. TLS, decoder, protocol, callback, or queue
failures close the transport immediately. The close callback is moved and invoked at
most once, and a structured remote `ERROR` is retained for the caller.

## Test evidence

`tls_tunnel_client_tests` runs a real client and the multi-client Linux server over
loopback with generated mutual-TLS credentials and a socket-pair-backed test TUN. It
verifies assignment negotiation, full framed uplink/downlink delivery, orderly
cleanup, and rejection of a certificate whose identity does not match the expected
server name.
