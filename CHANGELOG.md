# Changelog

All notable changes use semantic versioning. This file describes the redesigned root
implementation; `vpn_base/` is historical prototype material.

## [Unreleased]

### Added

- Protocol v1 framing, incremental decoder, control messages, and deterministic state
  machines.
- Certificate-fingerprint identities, server-authoritative generation-isolated leases,
  mutual TLS, hostname verification, client CRL enforcement, and key permission checks.
- Multi-client Linux TUN server with bounded queues, routing registry, admission
  control, handshake/idle deadlines, JSON metrics, and graceful signal shutdown.
- Portable serialized TLS client runtime and transactional Windows Wintun network
  backend with real UI lifecycle states.
- Cross-platform tests, Linux TLS/TUN end-to-end tests, ASan/UBSan, protocol fuzzing,
  microbenchmark, CI, CPack archives, checksums, pinned Wintun fetch, and release flow.
- Architecture, threat model, deployment, operations, performance, portfolio, and
  credential-governance documentation.

### Removed

- Root-build use of unframed packet reads, client-selected virtual addresses, embedded
  private keys, process-global client state, ping/curl shell construction, sleep-based
  connection detection, detached shutdown threads, and VPN-process firewall mutation.

### Security

- Historical credentials are treated as compromised and excluded from current source
  and packages. Repository policy rejects credential and generated-artifact patterns.

## [0.1.0] - Unreleased

First planned engineering release after ownership review, authenticated CI verification,
and controlled release-asset smoke testing.
