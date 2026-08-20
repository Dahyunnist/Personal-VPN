# Personal-VPN

Personal-VPN is an educational cross-platform IP tunnel prototype written in C++. It connects a Windows client using Wintun to a Linux server using TUN, with TLS transport implemented with Boost.Asio and OpenSSL.

> [!WARNING]
> The current branch is undergoing a correctness and security redesign. It is not ready for production deployment. Do not reuse credentials from earlier revisions of this repository.

## Current scope

- Windows client and ImGui desktop UI
- Linux TUN server and IPv4 forwarding
- Selective `/32` routing through Wintun
- TLS-protected client-to-server transport
- Multi-client prototype and virtual IP pool

## Enterprise upgrade

The active redesign focuses on:

1. An explicitly framed tunnel protocol over TLS
2. Server-authoritative virtual IP leases
3. Correct asynchronous write ownership, serialization, and backpressure
4. Mutual TLS with per-client identities and credential rotation
5. Reproducible builds, automated tests, CI, and benchmarks

See [vpn_base/README.md](vpn_base/README.md) for the legacy prototype instructions. Those instructions will be replaced as each upgrade phase becomes runnable.

## Repository layout

- `vpn_base/` — Linux server and Windows command-line client prototype
- `vpn_client_ui/` — Windows ImGui client UI
- `journal/` — original internship engineering journal and packet-capture evidence
- `notes/` — networking study notes retained as learning material
- `docs/` — security and architecture documentation for the redesign

## Security

No real private keys or client profiles belong in this repository. See [docs/security/credential-handling.md](docs/security/credential-handling.md) before generating development credentials.
