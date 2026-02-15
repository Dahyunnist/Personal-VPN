# VPN Base 系统设计文档

## 1. 项目概述

VPN Base 是一个基于 TUN/TAP 设备的 VPN 系统，实现了客户端-服务器架构的虚拟专用网络。系统支持多客户端并发连接，使用 SSL/TLS 加密通信，提供安全的网络隧道服务。

### 1.1 核心特性

- **跨平台支持**：服务器端支持 Linux，客户端支持 Windows
- **SSL/TLS 加密**：使用 OpenSSL 实现端到端加密通信
- **TUN 设备**：服务器端使用 Linux TUN 设备，客户端使用 Wintun
- **IP 地址池管理**：服务器端自动分配和管理客户端 IP 地址
- **多客户端支持**：支持多个客户端同时连接
- **异步网络编程**：基于 Boost.Asio 实现高性能异步 I/O

### 1.2 技术栈

- **编程语言**：C++17
- **网络库**：Boost.Asio
- **加密库**：OpenSSL
- **TUN 设备**：
  - Linux: `/dev/net/tun`
  - Windows: Wintun
- **配置格式**：JSON (nlohmann/json)
- **构建系统**：Makefile / CMake

## 2. 系统架构

### 2.1 整体架构

```
┌─────────────┐         SSL/TLS          ┌─────────────┐
│  客户端      │ ◄─────────────────────► │  服务器      │
│  (Windows)  │                          │  (Linux)    │
└─────────────┘                          └─────────────┘
      │                                          │
      │ TUN/Wintun                              │ TUN
      │                                          │
      ▼                                          ▼
┌─────────────┐                          ┌─────────────┐
│  本地网络    │                          │  互联网      │
└─────────────┘                          └─────────────┘
```

### 2.2 数据流向

#### 客户端到服务器（上行）
1. 本地应用发送数据包 → 客户端 TUN 设备
2. 客户端读取 TUN 设备数据包
3. 通过 SSL/TLS 加密发送到服务器
4. 服务器接收并解密数据包
5. 服务器将数据包写入 TUN 设备
6. TUN 设备转发到互联网

#### 服务器到客户端（下行）
1. 互联网响应数据包 → 服务器 TUN 设备
2. 服务器读取 TUN 设备数据包
3. 解析目标 IP，查找对应客户端会话
4. 通过 SSL/TLS 加密发送到客户端
5. 客户端接收并解密数据包
6. 客户端将数据包写入 TUN 设备
7. 本地应用接收数据包

## 3. 服务器端设计

### 3.1 目录结构

```
server/
├── src/              # 源代码目录
│   ├── vpn_server.cpp    # 主程序入口
│   ├── Server.cpp        # 服务器核心类
│   ├── Session.cpp       # 客户端会话管理
│   ├── TunDevice.cpp     # TUN 设备封装
│   ├── IpPoolManager.cpp # IP 地址池管理
│   ├── SystemConfig.cpp  # 系统配置工具
│   ├── BasicFunc.cpp     # 基础工具函数
│   └── config.cpp        # 配置管理
├── include/          # 头文件目录
│   ├── Server.h
│   ├── Session.h
│   ├── TunDevice.h
│   ├── IpPoolManager.h
│   ├── SystemConfig.h
│   ├── BasicFunc.h
│   └── config.h
├── certs/            # 证书目录
├── config.json       # 配置文件
└── makefile          # 构建文件
```

### 3.2 核心组件

#### 3.2.1 Server 类

**职责**：
- 管理服务器生命周期
- 接受客户端连接
- 管理所有客户端会话
- 处理 TUN 设备到客户端的数据转发

**关键方法**：
- `Server()`: 构造函数，初始化服务器
- `start_accept()`: 异步接受客户端连接
- `tun_read_loop()`: 从 TUN 设备读取数据并转发给客户端
- `assign_client_ip()`: 为客户端分配 IP 地址
- `release_client_ip()`: 释放客户端 IP 地址

**数据结构**：
```cpp
class Server {
    tcp::acceptor acceptor_;                    // TCP 接受器
    TunDevice& tun_;                            // TUN 设备引用
    ssl::context& ssl_ctx_;                     // SSL 上下文
    asio::io_context& io_context_;              // IO 上下文
    std::vector<std::shared_ptr<Session>> sessions_;  // 会话列表
    std::unordered_map<std::string, std::shared_ptr<Session>> ip_to_session_;  // IP 到会话映射
    std::mutex session_mutex_;                  // 会话互斥锁
    std::thread tun_to_client_thread_;           // TUN 读取线程
    std::atomic<bool> running_;                  // 运行标志
};
```

#### 3.2.2 Session 类

**职责**：
- 管理单个客户端连接
- 处理 SSL/TLS 握手
- 双向数据转发（客户端 ↔ TUN 设备）

**关键方法**：
- `start()`: 启动会话
- `stop()`: 停止会话
- `do_handshake()`: 执行 SSL/TLS 握手
- `start_reading()`: 从客户端读取数据并写入 TUN
- `async_write()`: 异步写入数据到客户端

**数据流**：
```
客户端数据 → SSL 流 → Session::start_reading() → TUN 设备
TUN 设备 → Server::tun_read_loop() → Session::async_write() → SSL 流 → 客户端
```

#### 3.2.3 TunDevice 类

**职责**：
- 封装 Linux TUN 设备操作
- 提供读写接口

**关键方法**：
- `TunDevice()`: 创建并配置 TUN 设备
- `read()`: 从 TUN 设备读取数据包
- `write()`: 向 TUN 设备写入数据包

**实现细节**：
- 使用 `ioctl()` 创建 TUN 设备
- 文件描述符模式：`O_RDWR`
- 标志：`IFF_TUN | IFF_NO_PI`

#### 3.2.4 IpPoolManager 类

**职责**：
- 管理客户端 IP 地址池
- 分配和回收 IP 地址
- 生成客户端配置文件

**关键方法**：
- `get_available_ip()`: 获取可用 IP 地址
- `mark_ip_used()`: 标记 IP 为已使用
- `release_ip()`: 释放 IP 地址
- `generate_config_file()`: 生成客户端配置文件

**IP 地址范围**：
- 默认范围：`10.8.0.2` - `10.8.0.254`
- 服务器 IP：`10.8.0.1`
- 子网掩码：`/24`

#### 3.2.5 SystemConfig 类

**职责**：
- 配置系统网络参数
- 设置 TUN 设备
- 配置 iptables NAT 规则
- 启用 IP 转发

**关键方法**：
- `configure_tun()`: 配置 TUN 设备 IP 和路由
- `enable_ip_forward()`: 启用 IP 转发
- `setup_iptables_nat()`: 配置 NAT 规则

### 3.3 服务器启动流程

```
1. 解析命令行参数（端口号）
2. 注册信号处理（SIGINT, SIGTERM）
3. 启动命令处理线程
4. 启用 IP 转发
5. 创建并配置 TUN 设备
6. 配置 iptables NAT 规则
7. 加载 SSL 证书和私钥
8. 创建 Server 实例
9. 启动 IO 上下文多线程运行
10. 进入事件循环
```

### 3.4 客户端连接流程

```
1. 客户端发起 TCP 连接
2. Server::start_accept() 接受连接
3. 创建 SSL 流
4. 创建 Session 实例
5. Session::start() 启动会话
6. Session::do_handshake() 执行 SSL 握手
7. Server::assign_client_ip() 分配 IP
8. Session::handle_assigned_ip() 处理 IP 分配
9. Session::start_reading() 开始数据转发
```

## 4. 客户端设计

### 4.1 目录结构

```
client/
├── client.cpp        # 主程序实现
├── client.h          # 接口头文件
├── config.json       # 客户端配置
├── certs/            # 证书目录
├── bin/              # Wintun DLL 目录
│   ├── amd64/
│   ├── arm/
│   ├── arm64/
│   └── x86/
└── Makefile          # 构建文件
```

### 4.2 核心功能

#### 4.2.1 Wintun 集成

**Wintun 初始化流程**：
1. 加载 `wintun.dll` 动态库
2. 获取函数指针
3. 创建或打开 TUN 适配器
4. 获取适配器 LUID
5. 启动会话

**关键函数**：
- `InitializeWintun()`: 初始化 Wintun
- `init_wintun_adapter()`: 创建/配置 TUN 适配器
- `CleanupWintun()`: 清理 Wintun 资源

#### 4.2.2 SSL/TLS 连接

**连接流程**：
1. 从配置文件读取证书
2. 初始化 SSL 上下文
3. 连接到服务器
4. 执行 SSL 握手
5. 发送客户端 TUN IP 地址

**证书加载**：
- 从 JSON 配置文件中读取证书内容（PEM 格式）
- 使用 OpenSSL BIO 从内存加载证书

#### 4.2.3 数据转发

**双向转发**：
- **TUN → SSL**：从 TUN 设备读取数据包，通过 SSL 发送到服务器
- **SSL → TUN**：从 SSL 接收数据包，写入 TUN 设备

**线程模型**：
- 使用 `boost::asio::thread_pool` 管理转发线程
- 主线程运行 `io_context` 事件循环

#### 4.2.4 路由管理

**路由添加**：
- 使用 Windows IP Helper API
- `CreateIpForwardEntry2()` 添加路由
- 目标 IP 通过 `10.8.0.1` 网关路由

**路由配置**：
- 接口：TUN 适配器 LUID
- 目标：用户指定的路由 IP
- 网关：`10.8.0.1`
- 掩码：`255.255.255.255` (32位)

### 4.3 客户端启动流程

```
1. 读取配置文件
2. 初始化 Winsock
3. 初始化 OpenSSL
4. 初始化 Wintun
5. 创建 TUN 适配器
6. 配置 TUN 适配器 IP
7. 添加路由规则
8. 连接到服务器
9. 执行 SSL 握手
10. 发送客户端 TUN IP
11. 启动数据转发线程
12. 进入事件循环
```

## 5. 配置文件设计

### 5.1 服务器配置

配置文件位置：`server/config.json`

**配置项**：
- `certs`: 证书配置
  - `server_crt`: 服务器证书（PEM 格式）
  - `server_key`: 服务器私钥（PEM 格式）
- `tun`: TUN 设备配置
  - `ip`: 服务器 TUN IP
  - `mask`: 子网掩码
- `network`: 网络配置
  - `physical_nic`: 物理网卡名称

### 5.2 客户端配置

配置文件位置：`client/config.json`

**配置项**：
- `server`: 服务器信息
  - `ip`: 服务器 IP 地址
  - `port`: 服务器端口
- `tun`: TUN 设备配置
  - `ip`: 客户端分配的 TUN IP
  - `mask`: 子网掩码
- `certs`: 证书配置
  - `client_crt`: 客户端证书（PEM 格式）
  - `client_key`: 客户端私钥（PEM 格式）
  - `server_crt`: 服务器证书（用于验证）

### 5.3 配置生成

服务器端提供 `genconfig` 命令自动生成客户端配置：
1. 从 IP 池分配可用 IP
2. 读取服务器 IP 和端口
3. 读取证书文件内容
4. 生成 JSON 配置文件

## 6. 安全设计

### 6.1 加密通信

- **协议**：TLS 1.2+
- **加密算法**：由 OpenSSL 协商
- **证书验证**：客户端验证服务器证书

### 6.2 证书管理

- **服务器证书**：自签名或 CA 签发
- **客户端证书**：由服务器生成
- **证书存储**：JSON 配置文件（PEM 格式）

### 6.3 网络安全

- **防火墙规则**：服务器端配置 iptables
- **NAT 转换**：MASQUERADE 规则
- **IP 转发**：服务器端启用 IP 转发

## 7. 性能优化

### 7.1 异步 I/O

- 使用 Boost.Asio 异步操作
- 避免阻塞操作
- 多线程处理 IO 事件

### 7.2 内存管理

- 使用智能指针管理资源
- 缓冲区复用
- 避免不必要的内存拷贝

### 7.3 线程模型

**服务器端**：
- 主线程：IO 上下文事件循环
- TUN 读取线程：从 TUN 设备读取数据
- 命令处理线程：处理用户命令

**客户端**：
- 主线程：IO 上下文事件循环
- TUN → SSL 线程：转发 TUN 数据到服务器
- SSL → TUN 线程：转发服务器数据到 TUN

## 8. 错误处理

### 8.1 异常处理

- 使用 C++ 异常机制
- 捕获网络异常
- 捕获设备操作异常

### 8.2 资源清理

- RAII 原则管理资源
- 析构函数确保资源释放
- 信号处理确保优雅退出

### 8.3 日志记录

- 控制台输出日志
- 错误信息详细记录
- 调试信息可选输出

## 9. 扩展性设计

### 9.1 模块化设计

- 各组件独立封装
- 清晰的接口定义
- 易于替换和扩展

### 9.2 配置灵活性

- JSON 配置文件
- 命令行参数
- 环境变量支持（可选）

### 9.3 平台抽象

- TUN 设备抽象层
- 网络 API 封装
- 系统配置工具类

## 10. 已知限制

1. **平台限制**：
   - 服务器端仅支持 Linux
   - 客户端仅支持 Windows

2. **功能限制**：
   - 仅支持 IPv4
   - 不支持 IPv6
   - 不支持 UDP 协议转发

3. **性能限制**：
   - 单服务器实例
   - 无负载均衡
   - 无连接池

## 11. 未来改进方向

1. **多平台支持**：
   - macOS 客户端
   - Linux 客户端
   - 移动平台支持

2. **功能增强**：
   - IPv6 支持
   - UDP 协议支持
   - 流量统计
   - 连接监控

3. **性能优化**：
   - 零拷贝技术
   - 多服务器负载均衡
   - 连接池管理

4. **安全性增强**：
   - 证书自动更新
   - 双因素认证
   - 访问控制列表

