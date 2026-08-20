# Tunnel Protocol v1

## Status

This document defines the first versioned wire format for Personal-VPN. It replaces the legacy assumption that one TLS read is equal to one IP packet. TLS over TCP is a byte stream and can split or coalesce application writes at any boundary.

All integer fields use network byte order (big endian).

## Frame header

| Offset | Size | Field | v1 rule |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `PVPN`, hexadecimal `0x5056504e` |
| 4 | 1 | Version | `1` |
| 5 | 1 | Message type | One of the registered types below |
| 6 | 2 | Flags | Must be zero in v1 |
| 8 | 4 | Payload length | At most 65,535 bytes |
| 12 | 8 | Sequence | Monotonically increasing within one direction of a session |

The 20-byte header is immediately followed by exactly `payload length` bytes. Receivers must read and validate a complete header before waiting for or allocating storage based on the payload length.

## Message types

| Value | Name | Direction | Purpose |
|---:|---|---|---|
| 1 | `CLIENT_HELLO` | Client to server | Advertise protocol capabilities after TLS authentication |
| 2 | `IP_ASSIGN` | Server to client | Assign the server-authoritative virtual IPv4 lease |
| 3 | `DATA_IPV4` | Both | Carry one complete IPv4 packet |
| 4 | `PING` | Both | Session liveness probe |
| 5 | `PONG` | Both | Reply to a liveness probe |
| 6 | `ERROR` | Both | Report a protocol or policy error before closing when possible |
| 7 | `CLOSE` | Both | Request an orderly tunnel shutdown |

Control-message payload schemas and the connection state machine will be finalized before the protocol is connected to the legacy client and server. Unknown message types, versions, or flags are fatal protocol errors in v1.

## Control payloads

`CLIENT_HELLO` contains an unsigned 16-bit requested MTU, a zero 16-bit reserved field, and a 32-bit capability mask. The IPv4 capability bit is mandatory in v1.

`IP_ASSIGN` is server-authoritative and contains, in order: the four-byte client IPv4 address, four-byte gateway IPv4 address, one-byte prefix length, one zero reserved byte, unsigned 16-bit MTU, and unsigned 32-bit lease duration in seconds. A client never selects or claims its own address.

`PING` and `PONG` contain one unsigned 64-bit nonce. A `PONG` returns the nonce from the corresponding `PING`.

`ERROR` contains an unsigned 16-bit error code followed by at most 1,024 bytes of diagnostic UTF-8 text. Sensitive configuration or packet data must not be included.

`CLOSE` contains one unsigned 16-bit reason code.

## Decoder behavior

- A partial header or partial payload is retained until more bytes arrive.
- Multiple frames received in one TLS read are emitted in wire order.
- Invalid magic, version, type, flags, or payload length fails the decoder closed.
- A failed decoder cannot resume without an explicit reset and a new transport session.
- The transport integration must bound its read buffers and outbound queues independently of the wire-format limit.

## Server session state

The first application frame in each authenticated TLS session must be `CLIENT_HELLO` with sequence number 1. The server acquires a lease for the authenticated identity and replies with `IP_ASSIGN`; a client-supplied virtual address is never accepted.

After establishment, every `DATA_IPV4` payload must contain exactly one structurally valid IPv4 packet. Its source address must match the authenticated session lease and its total length must equal the frame payload length. Packets exceeding the negotiated MTU are rejected before they reach TUN.

Sequence numbers increase by one independently in each direction. A gap, duplicate, invalid state transition, malformed control message, or source-address mismatch produces an `ERROR` and closes the transport. This fail-closed policy keeps the v1 state machine deterministic.

## Security boundary

Framing provides message boundaries and parser limits; it does not authenticate a peer or encrypt data. Authentication and confidentiality are provided by the mTLS transport. Session authorization and virtual IP ownership are enforced above this codec.
