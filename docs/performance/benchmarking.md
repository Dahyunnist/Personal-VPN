# 性能与稳定性验证

仓库中的性能结论必须可复现，并且要区分协议编解码器与真实端到端隧道。编解码微基准适合检测回归，但不包含 TLS、内核/用户态复制、Wintun/TUN、路由、NAT、丢包或网络延迟。

## 协议微基准

构建不含平台传输层的 Release 基准：

```bash
cmake -S . -B out/benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_TESTS=OFF \
  -DPERSONAL_VPN_BUILD_BENCHMARKS=ON
cmake --build out/benchmark
out/benchmark/protocol-benchmark 200000
```

程序会输出一条 JSON 记录，供 CI 和后续对比工具保存原始结果。CI 会验证记录字段，确保基准错误通过输出管道传递，并将原始 JSON 写入工作流运行摘要。不得将微基准结果用作对外宣传的隧道吞吐量。

### 已记录的开发结果

以下冒烟结果用于验证基准程序，不代表产品 SLA：

| 日期 | 平台 | 编译器 | 帧大小 | 迭代次数 | 编码 | 解码 |
|---|---|---|---:|---:|---:|---:|
| 2026-10-20 | Windows 10 x64 | GCC 14.2.0（MSYS2） | 1,400 字节 IPv4 载荷 | 200,000 | 3,761.38 MiB/s | 4,471.05 MiB/s |

原始结果：

```json
{"benchmark":"protocol_frame_1400","iterations":200000,"encoded_bytes":1420,"encode_mib_per_second":3761.38,"decode_mib_per_second":4471.05,"checksum":73977231}
```

## 端到端隧道实验

在同一受控网络中使用两台空闲的物理机或虚拟机：

- Windows 客户端：客户端构建标识、CPU、操作系统、Wintun 版本、以太网速率。
- Linux 服务端：服务端构建标识、CPU、内核、OpenSSL 版本、网卡速率。
- 网络：拓扑、链路速率、MTU、RTT，以及任一主机是否虚拟化。
- 配置：协商的密码套件、隧道 MTU、路由集合、服务端线程数和客户端数量。

先测直连基线，再通过 VPN 发送相同流量。预热一次后至少重复三次，并保留原始 `iperf3 -J` 输出。

```bash
# 在通过 Linux 服务端可达的目标主机上运行：
iperf3 --server

# 直连基线和 VPN 路径均至少重复三次：
iperf3 --client <destination> --time 30 --parallel 1 --json
iperf3 --client <destination> --time 30 --parallel 4 --json
ping -n 100 <destination>       # Windows
ping -c 100 <destination>       # Linux
```

报告吞吐量中位数、RTT 的 p50/p95、丢包、客户端/服务端 CPU 以及峰值 RSS。开销计算为 `(direct - vpn) / direct * 100%`。端点、路由、MTU 或链路条件不同的运行结果不可直接比较。

## 扩展性矩阵

实验环境允许时，以 1、5、10、50 个客户端运行相同负载。记录总吞吐量和单客户端吞吐量、服务端 CPU/RSS、打开文件数、TLS 握手失败、拒绝会话数和重连耗时。必须分别测试低于和高于配置的 `--max-sessions` 上限。

## 长稳与故障测试

执行 1、6、24 小时长稳测试，每 60 秒采样：

- 隧道连通性和 RTT；
- 客户端/服务端 RSS 和 CPU；
- JSON 指标中的活动会话、字节数、帧数、丢弃和协议错误；
- 重连事件，以及关闭前后的 Windows 路由表。

每次只注入一个故障：重启服务端进程、网络中断、客户端证书过期或吊销、格式错误帧、出站队列满、握手超时和空闲超时。通过标准：无崩溃或内存无界增长；按文档策略重连；客户端退出后没有遗留 Wintun 地址或路由。

原始结果保存在带日期的外部证据目录。只提交脱敏摘要：抓包、配置文件、主机名、IP 地址和证书信息可能包含敏感数据。
