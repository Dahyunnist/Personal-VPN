# 运维与故障排查手册

本手册假设 Linux TUN 接口已创建、转发/防火墙策略由管理员管理，且使用可丢弃的测试 PKI。排障时不得关闭证书验证、放宽密钥权限或清空主机防火墙。

## 预检查

Linux 服务端：

```bash
ip -details link show pvpn0
ip address show dev pvpn0
sysctl net.ipv4.ip_forward
ss -lntp | grep 8443 || true
stat -c '%a %U:%G %n' /etc/personal-vpn/pki/server.key
openssl x509 -in /etc/personal-vpn/pki/server.crt -noout -subject -issuer -dates
openssl crl -in /etc/personal-vpn/pki/client.crl -noout -lastupdate -nextupdate
```

Windows 客户端（管理员 PowerShell）：

```powershell
Get-NetAdapter | Where-Object InterfaceDescription -Match 'Wintun'
Get-NetIPAddress -AddressFamily IPv4
Get-NetRoute -AddressFamily IPv4 | Sort-Object DestinationPrefix
Test-NetConnection vpn.example.test -Port 8443
```

确认配置文件中的 `expected_name` 出现在服务端证书 SAN 中，并确认每个凭据路径都解析到预期的外部文件。

## 启动与健康检查

通过 systemd 启动服务端，再检查结构化指标和有界日志：

```bash
sudo systemctl start personal-vpn-server
systemctl status personal-vpn-server --no-pager
journalctl -u personal-vpn-server --since '-5 min' --no-pager
```

健康连接依次完成 TCP/TLS 认证、`CLIENT_HELLO`、`IP_ASSIGN` 和 Windows 网络事务提交。分配地址应出现在 Wintun 适配器上；只有配置文件中的路由应指向该接口；活动会话指标应增加 1；传输数据时数据包和字节计数应增长。

## 故障判断表

| 现象 | 收集证据 | 可能故障边界 | 安全处理 |
|---|---|---|---|
| TCP 连接失败 | DNS 结果、`Test-NetConnection`、监听状态 | 网络/监听器 | 修正地址、端口或 ACL；不要削弱 TLS |
| TLS 失败 | 有界客户端错误、证书 SAN/日期/签发者、CRL 日期 | PKI/时钟/名称 | 签发正确证书或修复信任/时间 |
| TLS 后客户端被拒绝 | 服务端协议错误与指标 | 能力/状态/租约/接入控制 | 对比版本与配置限制 |
| 已连接但没有流量 | Windows 路由、Wintun 地址、TUN、转发/防火墙 | 数据面 | 逐跳追踪一个数据包 |
| 只有上行 | 源地址校验、服务端 TUN 写入、NAT/转发 | 服务端出口 | 检查路由/NAT 策略，不要清空规则 |
| 只有下行 | TUN 目标地址、租约注册表、Wintun 发送 | 回程路由 | 验证目标租约和主机路由 |
| 内存/队列增长 | RSS、队列丢弃/背压指标、流量模式 | 慢速对端/限制 | 保留证据；安全地降低负载或限制 |
| 断开后仍有路由 | 前后路由快照、客户端关闭日志 | Windows 回滚 | 只停止客户端一次；仅删除已确认孤立的条目 |
| 反复因空闲超时断开 | 时间戳和字节计数 | 配置的截止时间 | 修正流量预期或按文档调整超时 |

## 数据包追踪

从内向外追踪：Windows 应用 → 路由 → Wintun → 客户端帧/TLS → 服务端会话 → TUN → Linux 路由/防火墙/NAT → 目标。随后追踪回程。只在可丢弃的实验环境中抓包；TLS 会隐藏物理链路上的隧道载荷，但 TUN/Wintun 抓包中是明文 IP 数据包，可能含敏感信息。

可用命令：

```bash
sudo tcpdump -ni pvpn0 -w /tmp/pvpn-tun.pcap
sudo tcpdump -ni any tcp port 8443 -w /tmp/pvpn-tls.pcap
ip route get <destination>
nft list ruleset   # 或主机选用的防火墙工具
```

## 优雅停止与回滚验证

1. 快照 Windows 地址/路由和 Linux 活动会话。
2. 在客户端中断开连接；首次检查时不要强制结束进程。
3. 确认收到 `CLOSE`/EOF、活动会话归零、租约释放、客户端线程 join 完成，并移除由本事务创建的地址/路由。
4. 确认无关适配器、地址和路由均未变化。
5. 停止服务端，确认监听端口关闭，TUN 仍由主机配置层管理。

## 事故证据包

记录构建/标签/提交、UTC 时间戳、脱敏配置文件、操作系统/内核版本、OpenSSL/Wintun 版本、准确复现步骤、指标、有界日志、路由表、资源采样和崩溃/sanitizer 输出。分享前移除私钥、令牌、敏感证书序列号、公网 IP、个人主机名和数据包载荷。

## 恢复原则

- 优先重启并执行精确事务回滚，不要手动大范围改网络状态。
- 绝不清空全部路由、防火墙规则、适配器或证书。
- 怀疑凭据泄露时立即吊销并签发不同的新凭据，更新 CRL，再按当前版本能力重启或重载。
- 仓库历史凭据的影响范围、CA 替换决策和切换验收步骤见[历史凭据轮换操作手册](credential-rotation.md)。
- 修改配置前先保留故障证据。

## 客户端证书吊销

吊销证书不会终止已完成 TLS 握手的隧道。服务端会对新握手检查更新后的 CRL；已建立会话仍保留原 TLS 上下文。

1. 在签发 CA 中吊销客户端证书并生成新 CRL。
2. 将 CRL 原子安装到配置路径。保持所有者和文件权限受限；不要原地编辑正在使用的 CRL。
3. 重载服务端，并确认结构化成功事件：

   ```bash
   sudo systemctl kill --signal=HUP personal-vpn-server
   journalctl -u personal-vpn-server --since '-2 min' --no-pager |
     grep '"event":"tls_context_reload","success":true'
   ```

4. 使用已吊销证书测试新连接，连接必须失败；再使用有效证书测试，连接应成功。
5. 若策略要求立即终止已经建立的吊销客户端会话，则安排协调重启服务端。此操作会断开所有活动客户端；当前服务端不能按证书单独逐出会话。
