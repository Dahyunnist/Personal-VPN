# Known limitations and roadmap

## Current limitations

- IPv4 only; no IPv6 data plane or dual-stack policy.
- TLS over TCP can suffer TCP-over-TCP performance collapse under loss.
- Automatic reconnect handles transient DNS/TCP/transport failures with capped jittered
  exponential backoff; session resumption, roaming, and seamless network change remain
  unsupported.
- Server certificates, client CA, and CRL reload on `SIGHUP`; client credentials still
  require client restart. Existing server sessions retain their original TLS context.
- JSON metrics go to stdout; no native Prometheus endpoint or distributed tracing.
- Linux network provisioning and Windows elevation remain administrator tasks.
- No signed first-party Windows executable/installer and no reproducible-build claim.
- Release archives and checksums now have keyless GitHub artifact-attestation steps;
  remote execution and consumer-side verification remain pending authentication.
- End-to-end throughput, latency, CPU, concurrency, and 24-hour soak evidence awaits a
  controlled two-host lab.
- No independent security audit or production incident/rollback experience.

## Prioritized next work

1. Produce controlled direct-vs-tunnel performance and soak evidence with profiles.
2. Add Windows code signing and a verified installer/uninstaller. Provenance/SBOM
   generation is implemented; authenticate and verify the release workflow first.
3. Evaluate a UDP-based data transport after protocol/security requirements are fixed.
4. Design IPv6 authorization and routing rather than treating it as a parser extension.

## Completed roadmap work

- Added transient client reconnect with capped jittered exponential backoff, user-stop
  cancellation, non-retryable TLS/protocol failure handling, and network rollback before
  retry. Linux integration tests exercise server loss, route cleanup, recovery, and stop
  during the next retry window.
- Added server-side TLS context replacement on `SIGHUP`; candidate key, certificate,
  CA, and CRL are validated before swap. Failed reload keeps the current context, and
  active sessions retain their original context until close.
- Added schema-v1 JSON Lines and Prometheus text exposition for all server counters and
  active sessions, without client-specific labels or an unauthenticated listener.
- Hardened Windows private-key ACL checks against 11 broad built-in principals and
  added Windows CI coverage for least-privilege and broad-read ACL cases.
- Added Windows dependency SPDX packaging and release gates for Linux/Windows packages.
  Keyless GitHub attestations cover Linux archive/checksum and Windows ZIP assets.
  The public Actions run and downloaded-asset verification remain unconfirmed.

Every performance optimization must begin with a retained profile and end with the
same controlled experiment. Every new protocol capability requires a compatibility
rule, parser bounds, negative tests, and threat-model review.
