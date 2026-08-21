# Known limitations and roadmap

## Current limitations

- IPv4 only; no IPv6 data plane or dual-stack policy.
- TLS over TCP can suffer TCP-over-TCP performance collapse under loss.
- No automatic reconnect, session resumption, roaming, or seamless network change.
- CRLs and certificates are loaded at process start; no live rotation.
- JSON metrics go to stdout; no native Prometheus endpoint or distributed tracing.
- Linux network provisioning and Windows elevation remain administrator tasks.
- No signed first-party Windows executable/installer and no reproducible-build claim.
- End-to-end throughput, latency, CPU, concurrency, and 24-hour soak evidence awaits a
  controlled two-host lab.
- No independent security audit or production incident/rollback experience.

## Prioritized next work

1. Produce controlled direct-vs-tunnel performance and soak evidence with profiles.
2. Add reconnect with exponential backoff, jitter, cancellation, and route-safe state.
3. Add certificate/CRL reload using atomic context replacement and rotation tests.
4. Export versioned Prometheus metrics and structured event logs with redaction.
5. Add Windows code signing, provenance/SBOM, and a verified installer/uninstaller.
6. Evaluate a UDP-based data transport after protocol/security requirements are fixed.
7. Design IPv6 authorization and routing rather than treating it as a parser extension.

Every performance optimization must begin with a retained profile and end with the
same controlled experiment. Every new protocol capability requires a compatibility
rule, parser bounds, negative tests, and threat-model review.
