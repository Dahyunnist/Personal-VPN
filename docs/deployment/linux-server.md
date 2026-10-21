# Linux 服务端部署

重构后的服务端有意将无特权的数据包进程与主机网络配置分开。可执行文件不会调用 `ip`、`sysctl`、`iptables` 或 `nft`，也不会把证书材料写进配置输出。

## 构建与安装

```bash
cmake -S . -B out/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build out/release
ctest --test-dir out/release --output-on-failure
sudo cmake --install out/release
```

## 主机准备

使用主机常规配置工具创建专用服务账号和持久 TUN 接口。手动开发示例：

```bash
sudo useradd --system --home /nonexistent --shell /usr/sbin/nologin personal-vpn
sudo ip tuntap add dev pvpn0 mode tun user personal-vpn
sudo ip address add 10.8.0.1/24 dev pvpn0
sudo ip link set dev pvpn0 mtu 1400 up
```

IP 转发、NAT 和转发策略取决于具体环境。应在主机防火墙配置中管理这些策略；不要让 VPN 进程或安装脚本清空已有规则。

将服务端证书、私钥和客户端 CA 证书包安装到 `/etc/personal-vpn/pki`。服务账号需要读取服务端密钥；其他用户不应具有读取权限。

若私钥包含任何组用户或其他用户权限位，服务端会拒绝加载。请设置 `0600`，并将文件所有者设为服务账号。Linux 私钥文件不得超过 1 MiB。需要吊销设备时，安装最新的 PEM 客户端 CRL。

## 直接运行

```bash
sudo -u personal-vpn /usr/local/bin/personal-vpn-server \
  --listen-address 0.0.0.0 \
  --port 8443 \
  --tun-name pvpn0 \
  --server-cert /etc/personal-vpn/pki/server.crt \
  --server-key /etc/personal-vpn/pki/server.key \
  --client-ca /etc/personal-vpn/pki/client-ca.crt \
  --client-crl /etc/personal-vpn/pki/client.crl \
  --lease-start 10.8.0.2 \
  --lease-end 10.8.0.254 \
  --gateway 10.8.0.1 \
  --prefix-length 24 \
  --mtu 1400 \
  --threads 4 \
  --max-sessions 1024 \
  --handshake-timeout 10 \
  --idle-timeout 300 \
  --metrics-interval 60
```

收到 `SIGINT` 或 `SIGTERM` 后，服务会停止接入、取消 TUN I/O、关闭会话、释放租约，并等待未完成的异步回调排空。

`SIGHUP` 会重新加载配置的服务端证书/密钥、客户端 CA 和可选 CRL。服务端先验证完整替换材料再切换上下文。验证失败时记录 `{"event":"tls_context_reload","success":false}`，并继续使用当前上下文。验证成功时记录相同事件且 `success:true`；已有会话继续使用原上下文，新握手使用替换后的上下文。发送 `SIGHUP` 前，应以严格密钥权限原子安装新凭据文件。重载不会关闭当前会话。可用 `sudo systemctl kill --signal=HUP personal-vpn-server` 向服务进程发送重载信号。

周期 JSON 指标写入标准输出，由 journald 收集。见[可观测性与运行限制](../architecture/observability-and-limits.md)。

## systemd

已提供[加固版服务单元示例](../../deploy/systemd/personal-vpn-server.service)。启用前请审查路径、端口、用户、资源限制及内核/防火墙配置。该服务单元假设 `pvpn0` 已创建且归 `personal-vpn` 所有。
