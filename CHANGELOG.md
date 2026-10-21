# Changelog

All notable changes use semantic versioning. This file describes the redesigned root
implementation; `vpn_base/` is historical prototype material.

## [Unreleased]

### Added

- Client automatic reconnect for transient DNS, TCP, timeout, and tunnel-loss failures,
  with jittered exponential backoff and network rollback before each retry.
- Server TLS certificate, client CA, and CRL hot reload on `SIGHUP`, with validation
  before atomic context swap and established-session context retention.
- Versioned JSON Lines and Prometheus text metrics, including byte, queue, and lifecycle
  counters without client-specific labels.
- Windows release gates verify required package files and credentials exclusions, place
  vcpkg SPDX records under their original dependency paths, and publish only after both
  platform packages pass.
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
- Linux server rejects symlink paths for configured private keys before checking file
  type and restrictive permissions.

## [0.1.0] - Unreleased

First planned engineering release after ownership review, authenticated CI verification,
and controlled release-asset smoke testing.
