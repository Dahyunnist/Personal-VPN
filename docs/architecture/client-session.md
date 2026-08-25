# Client session state machine

`ClientSessionController` is the portable protocol and policy boundary for the
redesigned Windows client. TLS and Wintun adapters may only exchange frames and packets
through this controller.

## Lifecycle

1. After an authenticated TLS client handshake, `start()` emits `CLIENT_HELLO` with
   sequence 1, the requested MTU, and IPv4 capability.
2. The first server frame must be sequence-1 `IP_ASSIGN`.
3. The client validates that the assigned address and gateway are distinct usable
   hosts in the same subnet and that the negotiated MTU does not exceed its request.
4. Only then may the Windows adapter be configured and routes installed.
5. Local packets become `DATA_IPV4` only when their source equals the assigned address.
6. Server packets reach Wintun only when their destination equals the assigned address.
7. Sequence gaps, invalid transitions, malformed controls, and misaddressed downlink
   packets emit a bounded `ERROR` and fail the transport closed.

PING/PONG nonces are correlated. Only one client liveness probe may be outstanding, and
an unsolicited or mismatched PONG is a protocol error. A structured server `ERROR` is
retained in the result so the UI can show a safe diagnostic instead of scraping global
standard output.

## Legacy boundary

The files under `vpn_client_ui/client/` and `vpn_client_ui/client_core_impl.cpp` still
contain the original connection and Wintun implementation. They currently assume TLS
read boundaries are packet boundaries, accept a client-selected virtual IP, embed key
material in JSON, and use process-global state. They must not be connected directly to
the redesigned server.

The next adapter phase will replace those behaviors with a serialized TLS frame
transport, path-based certificate configuration, and transactional Wintun route
application driven by the validated `IP_ASSIGN` result.
