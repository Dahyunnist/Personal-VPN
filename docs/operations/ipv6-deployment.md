# IPv6 隧道部署说明

## 当前支持范围

协议 v2 同时传输 IPv4 与 IPv6 内层数据包。服务端为每个 IPv4 租约派生唯一 IPv6 地址：默认 ULA 为 `fd00:8::/64`，低 32 位嵌入 IPv4 地址。当前地址分配器要求 IPv6 `/64`，IPv6 网关必须属于该前缀。

客户端可以安装 IPv6 地址和路由；服务端可监听 IPv4 或 IPv6 TCP 地址。内层数据仍由服务端 Linux TUN 接口交给主机网络栈。应用程序不会自动开启 Linux 转发、配置云防火墙、添加上游路由或设置 NAT66。

## 上线前配置

1. 配置服务器公网 IPv6、云安全组和主机防火墙，允许 VPN TCP 端口；客户端 `server.host` 可使用 IPv6 字面地址。
2. 确认 VPN 服务器具有 IPv6 上游路由，并按网络拓扑启用 Linux IPv6 转发。
3. 在服务器网络侧为 ULA 前缀配置到 TUN 的路由，或配置经过安全评审的 NAT66/出口策略。不要假设启用 VPN 就自动获得公网 IPv6。
4. 客户端配置需要实际承载的 IPv6 CIDR；全隧道加入 `::/0` 并设置 `allow_default_route: true`。确认 Windows WFP 持久 kill switch 已按操作手册部署和恢复演练。
5. 确认 MTU 至少为 1,280 字节，并在真实双端环境测试 ICMPv6 Packet Too Big、PMTU、DNS、断线和恢复。

## 兼容性和边界

协议版本从 v1 升至 v2。v1/v2 不互通，必须在同一维护窗口升级客户端和服务端。IPv6 数据面当前不实现 DHCPv6、SLAAC、IPv6 邻居发现代理、NAT66 或多 IPv6 子网分配。Linux 内核及云网络必须提供转发能力；WSL 单元测试不能证明真实 IPv6 可达性。
