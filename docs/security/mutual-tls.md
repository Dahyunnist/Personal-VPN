# Mutual TLS policy

The tunnel transport uses mutual TLS (mTLS). A TCP connection is not a Personal VPN
session until the TLS server handshake succeeds and OpenSSL verifies a client
certificate against the configured client CA.

## Trust boundaries

- The server certificate chain, its private key, and the client CA bundle are runtime
  inputs. Private keys must never be committed to this repository.
- The server requests and requires a client certificate. Anonymous TLS clients are
  rejected before the frame decoder or lease manager is reached.
- The authenticated session identity is the SHA-256 fingerprint of the verified leaf
  certificate. Callers cannot provide or override this identity.
- The TLS minimum is 1.2. TLS 1.2 is limited to forward-secret AEAD suites; TLS 1.3
  uses AES-GCM or ChaCha20-Poly1305 suites.
- Clients must validate the server certificate chain and the expected DNS name or IP
  subject alternative name.

## Development PKI

Run the helper only for local development or automated tests:

```bash
scripts/generate-dev-pki.sh out/dev-pki
```

It creates a short-lived development CA, a `localhost` server certificate, and an
integration-test client certificate under an ignored output directory. Generated
private keys are mode `0600`. This CA is not suitable for a shared, staging, or
production environment.

## Production requirements

Production deployments should issue a unique certificate per device through an
external CA or secrets platform, store server keys in a restricted secret mount, and
rotate certificates before expiry. Compromise response must revoke the affected
device identity and terminate its active session. CRL/OCSP enforcement and live
certificate reload are follow-up controls; until they are implemented, operators must
remove compromised issuing trust and restart the service.

The integration test creates an isolated PKI and verifies these boundaries:

1. a valid client and server complete an mTLS handshake;
2. the server derives the exact client certificate fingerprint;
3. a client without a certificate is rejected by the server; and
4. a client rejects a server certificate with the wrong hostname.
