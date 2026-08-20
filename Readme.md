# Personal-VPN

[![CI](https://github.com/Dahyunnist/Personal-VPN/actions/workflows/ci.yml/badge.svg)](https://github.com/Dahyunnist/Personal-VPN/actions/workflows/ci.yml)

Personal-VPN is an educational cross-platform IP tunnel prototype written in C++. It connects a Windows client using Wintun to a Linux server using TUN, with TLS transport implemented with Boost.Asio and OpenSSL.

> [!WARNING]
> The current branch is undergoing a correctness and security redesign. It is not ready for production deployment. Do not reuse credentials from earlier revisions of this repository.

## Current redesign status

- Versioned, bounded binary tunnel framing
- Server-authoritative, generation-isolated IPv4 leases
- Mutual TLS with certificate-fingerprint client identities
- Serialized asynchronous TLS and Linux TUN I/O with backpressure
- Multi-client Linux server runtime with admission control
- Cross-platform core tests and Linux end-to-end integration tests
- Portable, serialized mTLS client transport with bounded writes and strict hostname verification
- Transactional Windows Wintun/address/route setup with exact reverse rollback

The redesigned Linux server now builds as `personal-vpn-server`. Host interface,
forwarding, and firewall configuration remain explicit deployment responsibilities;
see [docs/deployment/linux-server.md](docs/deployment/linux-server.md).

## Enterprise upgrade

The active redesign focuses on:

1. An explicitly framed tunnel protocol over TLS
2. Server-authoritative virtual IP leases
3. Correct asynchronous write ownership, serialization, and backpressure
4. Mutual TLS with per-client identities and credential rotation
5. Reproducible builds, automated tests, CI, and benchmarks

See [vpn_base/README.md](vpn_base/README.md) for the legacy prototype instructions. Those instructions will be replaced as each upgrade phase becomes runnable.

## Build and test

On Linux with CMake, Ninja, Boost, and OpenSSL development packages:

```bash
cmake -S . -B out/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/release
ctest --test-dir out/release --output-on-failure
```

Show the server's validated command-line options without requiring root access:

```bash
out/release/personal-vpn-server --help
```

## Repository layout

- `vpn_base/` — Linux server and Windows command-line client prototype
- `vpn_client_ui/` — Windows ImGui client UI
- `journal/` — original internship engineering journal and packet-capture evidence
- `notes/` — networking study notes retained as learning material
- `docs/` — security and architecture documentation for the redesign

## Security

No real private keys or client profiles belong in this repository. See [docs/security/credential-handling.md](docs/security/credential-handling.md) before generating development credentials.
