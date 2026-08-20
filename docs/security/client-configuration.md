# Client configuration security

The redesigned client accepts a versioned JSON document containing references to
credential files. Certificates and private keys are never embedded in the document,
and the server assigns the virtual client address after mutual TLS succeeds.

## Schema version 1

```json
{
  "schema_version": 1,
  "server": {
    "host": "vpn.example.test",
    "port": 8443,
    "expected_name": "vpn.example.test"
  },
  "tls": {
    "ca_file": "credentials/ca.crt",
    "certificate_chain_file": "credentials/client.crt",
    "private_key_file": "credentials/client.key"
  },
  "tunnel": {
    "requested_mtu": 1400,
    "routes": ["10.20.0.0/16"],
    "allow_default_route": false
  }
}
```

Credential paths relative to the profile are resolved against the profile directory,
not the process working directory. All three paths must identify regular files. The
same validated `expected_name` is used for OpenSSL hostname verification and client
SNI, while `host` is used only for network resolution. This permits a pinned DNS
identity when connecting to a fixed IP without weakening certificate checks.

## Fail-closed validation

- Profiles are limited to 64 KiB and must use exactly schema version 1.
- Unknown and duplicate object fields are rejected.
- Inline or legacy `certs` objects are rejected.
- Ports, MTU, field length, route count, CIDR syntax, and canonical network addresses
  are bounded before any connection starts.
- `0.0.0.0/0` requires `allow_default_route: true`; a malformed or accidental default
  route cannot silently turn a split tunnel into a full tunnel.
- The profile contains no client-selected TUN address or gateway. Those values are
  authenticated, server-authoritative protocol data.

## Windows storage policy

Production profiles and keys should live below a per-user application-data directory,
with an ACL limited to that user, Administrators, and SYSTEM. They must not be placed
beside the executable, in the repository, or in a shared Downloads/Desktop directory.
Packaging work will add an ACL preflight and optional Windows protected-key import;
until then, the client must treat a profile with broadly readable key permissions as
an operator error.
