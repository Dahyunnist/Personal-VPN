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

The Linux server opens the private key without following a symlink, then validates the
opened regular file's owner and permissions before parsing from that same descriptor.
This prevents a path swap between the permission check and OpenSSL's key read. Keep the
key owned by the service account with mode `0600`. The Windows client rejects key
reparse points, null DACLs, and read grants to broad built-in principals on the opened
handle, then reads at most 1 MiB from that handle into a cleared-after-use buffer for
OpenSSL parsing. The key handle denies write/delete sharing during the operation. Use a
private-key ACL limited to the client identity; Windows CI exercises valid PEM loading
and rejects broad-principal grants at the TLS context entry point.

Production client revocation uses a PEM CRL configured with `--client-crl`. Replace
the server certificate, key, client CA, and CRL atomically, then send `SIGHUP`. The
server validates the candidate TLS context before swapping it; a failed reload keeps
the active context. New handshakes use refreshed trust material, while established
sessions retain their original TLS context. A coordinated server restart is required
to terminate all existing sessions; the server does not selectively evict a revoked
client. See the [operations runbook](../operations/runbook.md#client-certificate-revocation).

## History cleanup

History rewriting will be performed as a separate, coordinated operation after the replacement branch is stable. It changes commit IDs and requires force-updating the public repository and fresh clones for all collaborators.
