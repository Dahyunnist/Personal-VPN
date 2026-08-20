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

## Decoder behavior

- A partial header or partial payload is retained until more bytes arrive.
- Multiple frames received in one TLS read are emitted in wire order.
- Invalid magic, version, type, flags, or payload length fails the decoder closed.
- A failed decoder cannot resume without an explicit reset and a new transport session.
- The transport integration must bound its read buffers and outbound queues independently of the wire-format limit.

## Security boundary

Framing provides message boundaries and parser limits; it does not authenticate a peer or encrypt data. Authentication and confidentiality are provided by the mTLS transport. Session authorization and virtual IP ownership are enforced above this codec.
