# Operations and troubleshooting runbook

This runbook assumes a pre-created Linux TUN interface, administrator-managed
forwarding/firewall policy, and disposable test PKI. Never troubleshoot by disabling
certificate verification, broadening key permissions, or flushing the host firewall.

## Preflight

Linux server:

```bash
ip -details link show pvpn0
ip address show dev pvpn0
sysctl net.ipv4.ip_forward
ss -lntp | grep 8443 || true
stat -c '%a %U:%G %n' /etc/personal-vpn/pki/server.key
openssl x509 -in /etc/personal-vpn/pki/server.crt -noout -subject -issuer -dates
openssl crl -in /etc/personal-vpn/pki/client.crl -noout -lastupdate -nextupdate
```

Windows client (elevated PowerShell):

```powershell
Get-NetAdapter | Where-Object InterfaceDescription -Match 'Wintun'
Get-NetIPAddress -AddressFamily IPv4
Get-NetRoute -AddressFamily IPv4 | Sort-Object DestinationPrefix
Test-NetConnection vpn.example.test -Port 8443
```

Confirm that the profile's `expected_name` appears in the server certificate SAN and
that every credential path resolves to the intended external file.

## Start and healthy-state checks

Start the server through systemd, then inspect structured metrics and bounded logs:

```bash
sudo systemctl start personal-vpn-server
systemctl status personal-vpn-server --no-pager
journalctl -u personal-vpn-server --since '-5 min' --no-pager
```

A healthy connection progresses through TCP/TLS authentication, `CLIENT_HELLO`,
`IP_ASSIGN`, and Windows network commit. The assigned address must appear on the
Wintun adapter, only profile routes should reference it, active-session metrics should
increase by one, and data counters should move during traffic.

## Failure decision table

| Symptom | Evidence to collect | Likely boundary | Safe action |
|---|---|---|---|
| TCP connection fails | DNS result, `Test-NetConnection`, listener | network/listener | fix address, port, ACL; do not weaken TLS |
| TLS fails | bounded client error, cert SAN/dates/issuer, CRL dates | PKI/clock/name | issue correct cert or repair trust/time |
| Client rejected after TLS | server protocol error and metrics | capability/state/lease/admission | compare versions and configured limits |
| Connected but no traffic | Windows routes, Wintun address, TUN, forwarding/firewall | data plane | trace one packet hop at a time |
| Upload only | source validation, server TUN write, NAT/forward | server egress | inspect route/NAT policy without flushing it |
| Download only | TUN destination, lease registry, Wintun send | return routing | verify destination lease and host route |
| Memory/queue growth | RSS, queue-drop/backpressure metrics, traffic pattern | slow peer/limits | preserve evidence; reduce load or limits safely |
| Disconnect leaves route | before/after route snapshots, client shutdown log | Windows rollback | stop client once; remove only verified orphan entries |
| Repeated idle close | timestamps and byte counters | configured deadline | fix traffic expectations or documented timeout |

## Packet tracing

Trace from the inside out: Windows application → route → Wintun → client frame/TLS →
server session → TUN → Linux route/firewall/NAT → destination. Then trace the return
path. Capture only a disposable lab; TLS hides tunnel payload on the physical link,
but TUN/Wintun captures contain clear IP packets and may be sensitive.

Useful commands:

```bash
sudo tcpdump -ni pvpn0 -w /tmp/pvpn-tun.pcap
sudo tcpdump -ni any tcp port 8443 -w /tmp/pvpn-tls.pcap
ip route get <destination>
nft list ruleset   # or the host's chosen firewall tooling
```

## Graceful stop and rollback validation

1. Snapshot Windows address/routes and Linux active sessions.
2. Disconnect in the client; avoid killing it during the first check.
3. Confirm `CLOSE`/EOF, zero active session, lease release, joined client threads, and
   removal of the transaction-created address/routes.
4. Confirm unrelated adapters, addresses, and routes are unchanged.
5. Stop the server and verify its listener is closed and TUN remains managed by the
   host provisioning layer.

## Incident evidence bundle

Record build/tag/commit, UTC timestamps, sanitized profiles, OS/kernel versions,
OpenSSL/Wintun versions, exact reproduction steps, metrics, bounded logs, route tables,
resource samples, and crash/sanitizer output. Remove private keys, tokens, certificate
serials where sensitive, public IPs, personal hostnames, and packet payloads before
sharing.

## Recovery principles

- Prefer restart and exact transaction rollback to manual broad network edits.
- Never flush all routes, firewall rules, adapters, or certificates.
- Revoke a suspected credential, issue a distinct replacement, update the CRL, and
  restart/reload according to the current release capability.
- Preserve the failure evidence before changing configuration.
