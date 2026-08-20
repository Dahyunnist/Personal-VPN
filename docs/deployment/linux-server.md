# Linux server deployment

The redesigned server is intentionally split into an unprivileged packet process and
host network provisioning. The executable never invokes `ip`, `sysctl`, `iptables`, or
`nft`, and it never embeds certificate material in configuration output.

## Build and install

```bash
cmake -S . -B out/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build out/release
ctest --test-dir out/release --output-on-failure
sudo cmake --install out/release
```

## Host preparation

Create a dedicated service account and a persistent TUN interface using the host's
normal provisioning system. A manual development example is:

```bash
sudo useradd --system --home /nonexistent --shell /usr/sbin/nologin personal-vpn
sudo ip tuntap add dev pvpn0 mode tun user personal-vpn
sudo ip address add 10.8.0.1/24 dev pvpn0
sudo ip link set dev pvpn0 mtu 1400 up
```

IP forwarding and NAT/forwarding policy are environment-specific. Manage them in the
host firewall configuration; do not flush existing rules from the VPN process or an
installation script.

Install the server certificate, private key, and client CA bundle under
`/etc/personal-vpn/pki`. The service account needs read access to the server key; other
users should not.

## Direct execution

```bash
sudo -u personal-vpn /usr/local/bin/personal-vpn-server \
  --listen-address 0.0.0.0 \
  --port 8443 \
  --tun-name pvpn0 \
  --server-cert /etc/personal-vpn/pki/server.crt \
  --server-key /etc/personal-vpn/pki/server.key \
  --client-ca /etc/personal-vpn/pki/client-ca.crt \
  --lease-start 10.8.0.2 \
  --lease-end 10.8.0.254 \
  --gateway 10.8.0.1 \
  --prefix-length 24 \
  --mtu 1400 \
  --threads 4 \
  --max-sessions 1024
```

`SIGINT` and `SIGTERM` stop admission, cancel TUN I/O, close sessions, release leases,
and allow outstanding asynchronous callbacks to drain.

## systemd

An example hardened unit is provided at
[`deploy/systemd/personal-vpn-server.service`](../../deploy/systemd/personal-vpn-server.service).
Review paths, port, user, limits, and kernel/firewall provisioning before enabling it.
The unit assumes `pvpn0` is already created and owned by `personal-vpn`.
