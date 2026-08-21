# Application and recruiting materials

These drafts deliberately separate verified implementation facts from facts that still
require confirmation by the internship mentor. Replace bracketed fields only with
documented evidence.

## English CV bullets

- Conducted secure-communication technical research and built a cross-platform IPv4
  tunnel in C++17 using Boost.Asio, OpenSSL, Linux TUN, and Windows Wintun, supporting
  mutual TLS and multi-client asynchronous packet forwarding.
- Designed a bounded versioned framing protocol and identity-bound server-side address
  leases; applied serialized writes, owned buffers, backpressure, deadlines, and
  transactional route rollback to address TCP stream boundaries and asynchronous
  lifetime failures.
- Established cross-platform unit/integration/E2E tests, Clang ASan/UBSan, libFuzzer,
  CodeQL, reproducible dependency pins, CI, and checksummed release packages; verified
  18/18 Linux tests and 8/8 Windows core tests in local upgrade validation.

Do not add customer, production, throughput, concurrency, uptime, or percentage-
improvement claims until the mentor/lab evidence exists.

## Personal-statement paragraph (English)

During my internship, I developed a C++ prototype that connected a Windows Wintun
client to a Linux TUN server through mutually authenticated TLS. What initially looked
like a packet-forwarding task exposed deeper systems questions: TLS over TCP does not
preserve packet boundaries, asynchronous operations must own their buffers until
completion, authentication alone does not authorize a client to claim an IP address,
and a partially failed route update must not damage the host network. I subsequently
redesigned the prototype around an explicit protocol state machine, server-authoritative
leases, serialized I/O, bounded resources, transactional rollback, and adversarial
testing. This experience motivated me to study [computer networks / systems security /
distributed systems] more rigorously, particularly how protocol invariants and
operational evidence turn a functional prototype into a dependable system.

## 90-second Chinese interview introduction

这个项目是我在实习中完成并在之后持续工程化改造的跨平台 VPN 技术预研。Windows 端通过
Wintun 捕获三层 IP 包，Linux 端通过 TUN 接入内核路由，中间使用 Boost.Asio 和 OpenSSL
建立双向 mTLS 隧道。最初版本能完成转发，但存在几个典型系统问题：把 TLS read 当成包边界、
异步写并发和 buffer 生命周期不清晰、客户端自行声明地址、Windows 路由失败后缺少精确回滚。
我把系统拆成协议、平台无关状态机、Linux/Windows 适配器和运行时所有权层，设计了有界帧协议，
用 strand 与单写队列保证 TLS 串行访问，以证书身份绑定服务端租约，并把 Wintun 地址和路由做成
事务。验证层面加入了 18 项 Linux 测试、8 项跨平台核心测试、sanitizer、fuzz、CI 和发布包。
这个项目最有价值的地方不是“用了很多库”，而是让我能够完整解释一个 IP 包的内核/用户态路径、
异步资源所有权、安全授权和故障回滚。

## Mentor fact-check message

> 老师您好，我正在整理当年在物联安全产品线参与的 VPN 项目，用于研究生申请和校招。为了确保
> 表述准确、不涉及公司敏感信息，想请您帮我确认几个事实：当时项目属于实习培养、技术预研、
> POC 还是正式产品模块？主要验证的业务/技术问题是什么？我的代码或方案后来是否被团队参考、
> 演示、合并或继续开发？哪些个人职责、性能/稳定性数据和产品方向可以公开描述？另外，项目代码
> 公开展示和选择开源许可证是否获得公司允许？如果有不能公开的部分，我会删除或只做抽象描述。
> 谢谢老师。

Keep the reply as evidence, but do not commit private correspondence or company-
confidential details to this repository.
