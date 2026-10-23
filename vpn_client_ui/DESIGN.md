# Windows 桌面客户端设计

## 组件与所有权

```text
UIMain
└── VPNClientCore
    └── BrokerClientRuntimeControl
        └── authenticated Windows named pipe
            └── SCM broker service
                └── BrokerRuntimeRegistry (one runtime per caller SID)
                    └── ClientRuntime
                        ├── io_context, resolver, connection deadline
                        ├── TLS, protocol session, frame decoder
                        ├── bounded serialized write queue
                        ├── WindowsNetworkBackend, Wintun, network transaction
                        └── I/O and interruptible packet-reader threads
```

UI 只持有 broker IPC 客户端，不持有适配器、路由或特权运行时。管道中断、用户注销或服务停止时，服务会停止并 join 运行时，再回滚其创建的网络对象。线程不会 detach。broker 以 LocalSystem 运行，目前只支持活动物理控制台会话；服务隔离和真实驱动生命周期仍待虚拟机验证。

## 状态模型

```text
Stopped -> Starting -> Resolving -> Connecting -> ConfiguringNetwork -> Connected
Resolving / Connecting / Connected -- temporary failure --> Reconnecting
Reconnecting -- retry timer --> Resolving
any active state -- user stop --> Stopping -> Stopped
certificate, protocol, or local setup failure --> Failed
```

DNS、TCP、超时和暂时性传输故障会使用带上限的抖动指数退避重试（0.5–30 秒）。每次重试前，运行时停止包读取线程，并回滚路由、接口地址、MTU、包会话和适配器状态。服务端拒绝或不可重试的 TLS 错误会结束运行时。

`VPNClientCore::Start` 仅表示已发送连接请求。UI 根据 broker 状态更新；只有 TLS、协议地址分配和 Windows 网络配置全部完成后，才显示已连接。

运行时设有连接 deadline，覆盖 DNS 解析至认证后的 IP 分配。连接成功后启动一个阻塞式 Wintun 读取线程；关闭时通过专用 Windows event 唤醒。下行数据仍由串行化 TLS strand 处理。

## 错误边界

IPC 只传不透明 profile ID、请求 ID、状态和有界诊断，不传凭据、路由、任意路径或命令。回调不会暴露证书或密钥内容；运行时边界会捕获回调异常。所有故障都进入同一幂等清理路径。broker 或 IPC 失败时，UI 不得回退到本地特权网络操作。

## 已移除的旧行为

- JSON 内嵌 PEM 私钥；
- 客户端自行选择 TUN 地址及换行地址协商；
- 未分帧的 TLS 数据包读取；
- 硬编码 `10.8.0.1` 路由网关；
- 全局替换 `std::cout` / `std::cerr`；
- 用命令行拼接 ping/curl 测试；
- 基于 sleep 的连接检测；
- 超时后 detach 线程；
- GUI 直接创建 Wintun 或配置系统路由。

旧 `client_core_impl.cpp` 和重复的 `client/` 实现已在新可执行程序构建成功后删除。
