# Security policy

## Supported versions

Only the latest commit on the default branch and the latest published release are
eligible for security fixes. This educational project has no production support SLA.

## Reporting a vulnerability

Use GitHub private vulnerability reporting for this repository when available. Do not
open a public issue containing exploit steps, private keys, certificates, tokens,
packet payloads, personal IP addresses, or unredacted logs.

Include the affected commit/version, platform, threat scenario, minimal reproduction,
security impact, and any proposed mitigation. Use disposable credentials and a lab
network. Receipt and remediation timing are best effort until a formal maintainer
policy is established.

## Credential exposure

Any private key ever committed or publicly shared must be considered compromised:
revoke it, issue a distinct replacement, and update the relevant CRL/trust store.
Deleting a key from the latest tree is not credential rotation.

## Scope

Useful reports include protocol parsing, authentication/authorization bypass, address
spoofing, memory safety, unbounded resource use, credential/path permission failures,
route rollback corruption, and release supply-chain issues. Missing product features,
unsupported IPv6, or the documented lack of an independent audit are not themselves
vulnerabilities.
