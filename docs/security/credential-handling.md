# Credential handling

## Repository policy

Private keys, PKCS#12 bundles, generated client profiles, and machine-specific configuration files must never be committed. Development credentials are generated locally and stored below an ignored `credentials/` or server certificate directory.

Certificates and keys that appeared in repository history before the enterprise redesign must be treated as compromised. Deleting them from the latest commit does not make them safe to reuse.

## Required rotation procedure

1. Revoke and stop using every historical server and client key.
2. Create a dedicated development CA that is not also used as a server leaf certificate.
3. Issue a unique certificate and private key for every client identity.
4. Give server and client certificates the appropriate Extended Key Usage and Subject Alternative Name extensions.
5. Store private keys with least-privilege filesystem ACLs; see [client-configuration.md](client-configuration.md) for the Windows profile boundary.
6. Generate test credentials during local setup or CI and destroy them after the test run.

The Linux server enforces a regular private key with no group/other permission bits.
The Windows client rejects key reparse points, null DACLs, and broadly readable ACL
entries before OpenSSL loads the key. Production client revocation uses a PEM CRL via
`--client-crl`; updating trust material requires a graceful service restart.

## History cleanup

History rewriting will be performed as a separate, coordinated operation after the replacement branch is stable. It changes commit IDs and requires force-updating the public repository and fresh clones for all collaborators.
