# Threat model

## Assets and security goals

- Client and server private keys remain confidential.
- A tunnel peer is authenticated before receiving an address or packet access.
- One client cannot claim, spoof, or receive another client's virtual address traffic.
- Untrusted network bytes cannot cause unbounded allocation, concurrent TLS access,
  undefined behavior, or host route corruption.
- Shutdown removes only process-owned resources and remains safe after partial setup.

Availability against an adversary with unlimited traffic and protection of a fully
compromised endpoint are out of scope.

## Adversaries

1. A passive network observer sees TCP metadata and ciphertext.
2. An active network attacker can intercept, replay, fragment, coalesce, delay, or
   modify traffic and present an untrusted certificate.
3. An authenticated but malicious client sends malformed controls/IP packets, spoofs
   sources, floods queues, or opens many sessions.
4. A local unprivileged user attempts to read key files or replace a credential path.
5. A configuration author supplies unsafe routes, malformed limits, or an unexpected
   server identity.
6. A dependency or release artifact is replaced upstream or in transit.

## Controls

| Threat | Control | Residual risk / verification |
|---|---|---|
| Network interception | TLS, CA validation, exact server-name verification, mTLS | TCP endpoints/timing remain visible; verify with disposable PKI E2E tests |
| Stolen/revoked client cert | CRL verification and fingerprint identity | CRL distribution is operational and not live-reloaded |
| Address spoofing | Server-issued leases plus source/destination validation | IPv6 is unsupported and rejected |
| Stream confusion | Magic/version/type/flags/length/sequence validation | Protocol v1 has no extension negotiation |
| Memory/CPU exhaustion | Payload, queue, sessions, threads, MTU, profile, timeout bounds | Volumetric DoS needs upstream firewall/rate controls |
| Async lifetime bugs | Serialized stream access, owned buffers, weak/shared ownership, joined shutdown | Enforced by tests, sanitizers, and fuzzing, not a formal proof |
| Route damage | Strict route parsing and transactional exact rollback | Client still requires elevation; admin must review profile routes |
| Local key disclosure | External paths, Linux mode checks, Windows ACL/reparse checks | Malware/admin access and memory extraction are out of scope |
| Supply-chain replacement | pinned ImGui tag, pinned Wintun version/hash, checksums | Boost/OpenSSL/toolchain trust the build environment; no signed first-party binary yet |
| Secret committed to Git | ignore/policy checks and credential rotation guidance | Historical commits require coordinated secret rotation/history treatment |

## Trust boundaries

- TLS termination is not authorization: protocol and lease checks remain mandatory.
- A valid client certificate identifies a credential, not a human or a secure device.
- Wintun/TUN and IP Helper calls cross into privileged kernel state.
- Configuration and PKI files are administrator-controlled inputs, not remote inputs.
- Host routing, firewall, NAT, DNS policy, endpoint hardening, and key issuance are
  deployment responsibilities.

## Security test cases

- unknown CA, expired certificate, hostname mismatch, and revoked client fail before
  protocol establishment;
- duplicate/gapped sequence, invalid state/type/flags/length, oversized payload, and
  malformed IPv4 fail closed;
- spoofed client source and misaddressed downlink never reach TUN/Wintun;
- queue/session limits reject excess work without unbounded growth;
- handshake and idle deadlines close and release the exact lease generation;
- partial Windows setup rolls back only completed steps;
- private-key permission/ACL failures occur before OpenSSL loads the key.

Report security issues using GitHub private vulnerability reporting where available;
do not publish exploitable details or real credentials in a public issue.
