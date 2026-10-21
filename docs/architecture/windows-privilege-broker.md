# Windows privilege broker

## Decision

The desktop UI must run unelevated. The current route backend calls
`CreateIpForwardEntry2`, which [Microsoft documents as requiring an administrator
token](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-createipforwardentry2).
A small Windows service owns the TLS client,
credential loading, Wintun session, interface configuration, and route transaction.
The service—not the UI—must authenticate the VPN server and validate `IP_ASSIGN`
before changing network state. This prevents a compromised or modified UI from asking
a privileged process to install arbitrary routes.

The existing UI self-elevation path stays in place until the service path passes the
acceptance gates below. Do not remove the old path or ship a UI that silently fails to
connect before broker integration is complete.

## Trust boundary and IPC contract

Use a local named pipe with a versioned, length-prefixed protocol. The server rejects
remote pipe clients, malformed or oversized messages, unknown versions, unknown message
types, and duplicate request IDs. Initial messages contain only:

- `Connect(profile_id)`;
- `Disconnect(request_id)`; and
- `GetStatus(request_id)`.

The protocol must not accept caller-supplied IP addresses, gateways, route prefixes,
interface names, adapter names, raw packets, executable paths, or arbitrary commands.
The broker resolves a profile only inside the requesting user's approved profile root.
It impersonates that user while opening configuration and credential files, checks the
existing private-key ACL and size rules on the opened handle, then reverts before
network work. Secret bytes never cross IPC.

The pipe DACL grants access only to explicitly authorized interactive user SIDs,
Administrators, and SYSTEM. For every connection, the broker impersonates the caller,
checks the token user and interactive session, and binds the request to that identity.
It must not trust a SID, username, profile path, or session ID supplied in message data.
Limit each user to one active tunnel. Disconnect, pipe loss, logoff, service stop, or
transport failure must use the existing idempotent shutdown and route rollback path.

Run the service under the least-privileged identity that can perform the required
Wintun and network operations. Determine and document the exact required token rights
in an elevated integration test; do not default to LocalSystem without evidence. Keep
the UI and service binaries, service configuration, and update path protected from
standard-user writes. Installation and service ACL changes require an explicit
administrator-approved installer.

## Integration sequence

1. Extract a broker-facing tunnel lifecycle interface from `ClientRuntime`; preserve
   its TLS, reconnect, transaction, and joined-shutdown invariants.
2. Implement and fuzz the bounded IPC codec. Add Windows tests for pipe ACLs, caller
   token binding, remote-client rejection, protocol versioning, message limits, and
   malformed requests.
3. Host the existing client runtime and `WindowsNetworkBackend` in the service. Ensure
   profile and private-key reads occur under the caller's identity and TLS verification
   remains in the service process.
4. Replace `VPNClientCore`'s direct runtime ownership with the IPC client. UI messages
   expose state and bounded diagnostics only; they never carry credentials or routes.
5. Add installer, service recovery policy, upgrade/rollback/uninstall, and elevated
   integration coverage. Then remove UI self-elevation and verify the UI token is not
   elevated while real tunnel setup and cleanup still pass.

## Acceptance gates

- UI starts and operates with a standard-user token; only install/upgrade prompts for
  elevation.
- A standard user cannot modify or replace service files, service configuration, or
  broker policy.
- Pipe callers cannot impersonate another user or session, connect remotely, or submit
  network mutations outside a TLS-authenticated tunnel assignment.
- Invalid, stale, oversized, replayed, and unsupported IPC messages fail closed.
- Broker stop, UI crash, pipe loss, reconnect, and service restart leave no stale route,
  address, adapter session, or worker thread.
- Real Wintun and route integration passes on a disposable Windows VM under the
  intended service identity, without running the UI elevated.

Do not claim the privilege split is complete until every gate passes. Current state is
still single-process UI plus direct Windows network backend; this document is a target
design, not evidence of implementation.
