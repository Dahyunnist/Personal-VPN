# 可观测性与运行限制

服务端通过 `ServerMetrics` 暴露有界、单调递增的进程内计数器，并按可配置周期输出指标。默认格式为 JSON Lines；使用 `--metrics-format prometheus` 可输出带版本的 Prometheus 文本格式。两种格式均写到标准输出，供 journald/日志收集器采集。服务端不会开放未经认证的指标监听端口。

```json
{"event":"server_metrics","schema_version":1,"accepted":12,"capacity_rejections":1,"handshake_failures":2,"handshake_timeouts":1,"idle_timeouts":3,"established":8,"closed":4,"active":4,"queue_overflows":0,"uplink_packets":942,"uplink_bytes":120576,"downlink_packets":881,"downlink_bytes":112768}
```

Prometheus 输出以 `personal_vpn_metrics_schema_info{version="1"} 1` 开始；计数器名称以 `_total` 结尾，`personal_vpn_active_sessions` 是 gauge。指标不含客户端专属标签，因此基数有界，客户端身份也不会泄露。JSON `schema_version` 和 Prometheus schema-info gauge 定义 schema v1。`--metrics-interval 0` 会关闭周期输出。

计数器包括已接入及因容量拒绝的套接字、TLS 失败/超时、已建立和已关闭会话、空闲超时、出站队列溢出，以及双向数据包/字节总量。周期记录不会输出证书主题、指纹、客户端地址、凭据路径或数据包内容。

## 拒绝服务防护边界

- `--max-sessions` 限制已接入的 TCP/TLS 会话数。
- `--handshake-timeout` 会关闭未能在 1 到 300 秒内完成双向 TLS 的 TCP 客户端，默认 10 秒。
- `--idle-timeout` 会关闭没有产生完整协议帧的已认证连接，默认 300 秒。持续发送不完整帧不会重置超时。
- 每个会话只允许一个串行写操作，队列默认最多 256 帧或 1 MiB。
- 协议载荷、TUN 数据包、工作线程、租约、客户端路由、配置文件大小和 Wintun 环形缓冲区均有明确上限。

超时处理器与读、写和关闭操作运行在同一会话 strand 上，因此截止时间和成功操作不会并发释放同一租约或套接字。测试用 100 ms 截止时间证明：握手过慢的客户端和握手后空闲的客户端都会被关闭，且只计数一次。
