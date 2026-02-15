# VPN Client UI 设计文档

## 1. 项目概述

VPN Client UI 是一个基于 ImGui 的 Windows VPN 客户端图形界面应用程序。它将 `vpn_base/client` 的核心功能封装为图形界面，提供直观的 VPN 连接管理、配置导入、日志查看和连接测试功能。

### 1.1 核心特性

- **图形化界面**：基于 ImGui 的现代化 UI
- **配置管理**：支持 JSON 配置文件导入
- **连接管理**：一键连接/断开 VPN
- **路由管理**：连接与路由分离，支持多次连接/断开
- **实时日志**：实时显示 VPN 客户端运行日志
- **连接测试**：内置 ping 和 curl 测试功能
- **中文支持**：完整的中文界面和字体支持

### 1.2 技术栈

- **UI 框架**：ImGui (Dear ImGui)
- **渲染后端**：DirectX 11
- **窗口管理**：Win32 API
- **VPN 核心**：基于 `vpn_base/client`
- **网络库**：Boost.Asio
- **加密库**：OpenSSL
- **TUN 设备**：Wintun (Windows)
- **构建系统**：CMake
- **编程语言**：C++14

## 2. 系统架构

### 2.1 整体架构

```
┌─────────────────────────────────────────┐
│          Win32 窗口 (main.cpp)          │
│  ┌───────────────────────────────────┐  │
│  │    DirectX 11 渲染上下文          │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │    ImGui 渲染引擎                 │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │    UIMain (UI 逻辑层)             │  │
│  │  - 配置管理                        │  │
│  │  - 界面渲染                        │  │
│  │  - 日志管理                        │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │    VPNClientCore (核心包装层)     │  │
│  │  - 连接管理                        │  │
│  │  - 路由管理                        │  │
│  │  - 日志重定向                      │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │    client_core_impl (VPN 实现)    │  │
│  │  - Wintun 集成                    │  │
│  │  - SSL/TLS 连接                   │  │
│  │  - 数据转发                       │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

### 2.2 模块划分

#### 2.2.1 应用层 (main.cpp)

**职责**：
- Win32 窗口创建和管理
- DirectX 11 设备初始化
- 消息循环处理
- 管理员权限检查
- 资源清理

**关键组件**：
- `D3D11Data`: DirectX 11 全局数据
- `CreateDeviceD3D()`: 创建 DirectX 11 设备
- `WndProc()`: 窗口过程函数

#### 2.2.2 UI 层 (ui_main.h/cpp)

**职责**：
- ImGui 界面渲染
- 用户交互处理
- 配置导入和管理
- 日志显示和管理
- 连接测试功能

**关键类**：
```cpp
class UIMain {
    // DirectX 设备引用
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    HWND m_hwnd;
    
    // UI 状态
    char m_configPath[512];
    char m_serverIp[64];
    char m_serverPort[16];
    char m_tunIp[64];
    char m_routeIp[64];
    bool m_isConnected;
    bool m_testInProgress;
    
    // VPN 客户端
    std::unique_ptr<VPNClientCore> m_vpnClient;
    
    // 日志
    std::vector<std::string> m_logLines;
    std::mutex m_logMutex;
};
```

#### 2.2.3 核心包装层 (vpn_client_core.h/cpp)

**职责**：
- 封装 VPN 客户端核心功能
- 连接和路由分离管理
- 日志重定向
- 线程管理

**关键类**：
```cpp
class VPNClientCore {
    // 连接管理
    bool StartConnection(...);
    void Disconnect();
    
    // 路由管理
    bool AddRoute(const std::string& route_ip);
    void RemoveRoute();
    
    // 统一接口
    bool Start(...);  // 自动处理连接和路由
    void Stop();      // 只删除路由
    
    // 状态查询
    bool IsConnected();
    bool IsRouteActive();
};
```

**设计亮点**：
- **连接与路由分离**：连接建立后保持活跃，断开时只删除路由
- **日志重定向**：通过 `LogRedirector` 将 `std::cout/cerr` 重定向到 UI
- **线程安全**：使用原子变量和互斥锁保证线程安全

#### 2.2.4 VPN 实现层 (client_core_impl.cpp)

**职责**：
- Wintun 设备管理
- SSL/TLS 连接处理
- 数据包转发
- 路由管理

**关键功能**：
- `start_vpn_client()`: 启动 VPN 客户端
- `stop_vpn_client_impl()`: 停止 VPN 客户端
- `add_route()`: 添加路由
- `remove_route()`: 删除路由

## 3. 核心设计

### 3.1 连接与路由分离设计

#### 3.1.1 设计理念

传统 VPN 客户端在断开连接时会完全关闭与服务器的连接，再次连接时需要重新建立连接，这可能导致：
- 重复初始化资源
- 连接建立延迟
- 资源清理问题

**改进方案**：
- **连接保持**：与服务器的 SSL/TLS 连接在首次建立后保持活跃
- **路由分离**：连接和路由是两个独立的概念
- **快速切换**：断开时只删除路由，连接保持；再次连接时只需添加路由

#### 3.1.2 状态管理

```
初始状态: 未连接，无路由
    ↓
Start() → 建立连接 + 添加路由
    ↓
连接状态: 已连接，路由激活
    ↓
Stop() → 删除路由（连接保持）
    ↓
连接状态: 已连接，无路由
    ↓
Start() → 添加路由（连接已存在）
    ↓
连接状态: 已连接，路由激活
    ↓
程序退出 → Disconnect() → 断开连接
```

#### 3.1.3 实现细节

**VPNClientCore 状态**：
```cpp
std::atomic<bool> m_connected{false};      // 连接状态
std::atomic<bool> m_route_active{false};   // 路由状态
```

**Start() 实现**：
```cpp
bool VPNClientCore::Start(...) {
    // 如果未连接，先建立连接
    if (!m_connected) {
        if (!StartConnection(config_path, log_callback)) {
            return false;
        }
    }
    // 添加路由
    return AddRoute(route_ip);
}
```

**Stop() 实现**：
```cpp
void VPNClientCore::Stop() {
    // 只删除路由，不断开连接
    RemoveRoute();
}
```

### 3.2 日志重定向机制

#### 3.2.1 设计目标

将 VPN 客户端的 `std::cout` 和 `std::cerr` 输出重定向到 UI 界面，实现实时日志显示。

#### 3.2.2 实现方式

**LogRedirector 类**：
```cpp
class LogRedirector : public std::streambuf {
    // 重定向 std::cout 和 std::cerr
    std::cout.rdbuf(this);
    std::cerr.rdbuf(this);
    
    // 缓冲数据，按行分割
    // 调用回调函数将日志发送到 UI
};
```

**工作流程**：
1. 创建 `LogRedirector` 实例，替换标准输出流
2. VPN 客户端输出到 `std::cout/cerr`
3. `LogRedirector` 捕获输出，缓冲数据
4. 遇到换行符时，调用回调函数
5. UI 层接收日志并显示

#### 3.2.3 线程安全

- 使用互斥锁保护缓冲区
- 回调函数在 VPN 客户端线程中执行
- UI 线程通过互斥锁安全访问日志列表

### 3.3 资源管理

#### 3.3.1 RAII 原则

所有资源使用 RAII 原则管理：
- 智能指针管理对象生命周期
- 析构函数确保资源释放
- 异常安全

#### 3.3.2 关键资源

**DirectX 11 资源**：
- 设备、上下文、交换链
- 渲染目标视图
- 在 `CleanupDeviceD3D()` 中统一清理

**ImGui 资源**：
- Context
- 后端实现
- 在 `Shutdown()` 中清理，防止重复关闭

**VPN 资源**：
- Wintun 适配器和会话
- SSL/TLS 连接
- 线程资源
- 在 `Disconnect()` 中清理

#### 3.3.3 关闭顺序

```
1. 停止 VPN (StopVPN)
2. 断开连接 (Disconnect)
3. 结束 ImGui 帧 (EndFrame)
4. 关闭 ImGui 后端 (Shutdown)
5. 清理 DirectX 资源 (CleanupDeviceD3D)
6. 销毁窗口
```

### 3.4 字体和本地化

#### 3.4.1 中文字体支持

**字体加载策略**：
1. 优先加载 `msyh.ttc` (微软雅黑)
2. 其次加载 `simhei.ttf` (黑体)
3. 如果都失败，使用默认字体并尝试合并中文字符

**字符范围**：
```cpp
static const ImWchar ranges[] = {
    0x0020, 0x00FF,  // 基本拉丁字符
    0x4E00, 0x9FFF,  // 中日韩统一表意文字
    0x3000, 0x303F,  // CJK 符号和标点
    0xFF00, 0xFFEF,  // 全角字符
    0,
};
```

#### 3.4.2 编码处理

- 使用 UTF-8 编码存储字符串
- Windows API 调用使用 ANSI 版本（`MessageBoxA`）或 Unicode 版本（`MessageBoxW`）
- 文件路径使用宽字符处理

## 4. UI 设计

### 4.1 界面布局

#### 4.1.1 标签页结构

**连接配置标签**：
- 配置导入区域
- 设备配置区域（只读）
- 路由 IP 输入
- 连接/断开/测试按钮
- 测试结果显示

**日志输出标签**：
- 日志显示区域（可滚动）
- 清除日志按钮

#### 4.1.2 交互设计

**状态管理**：
- 连接时禁用配置导入和路由 IP 输入
- 断开时启用配置导入和路由 IP 输入
- 测试进行时禁用测试按钮

**反馈机制**：
- 实时日志显示
- 测试状态和结果显示
- 错误信息高亮显示

### 4.2 渲染流程

```
1. ImGui_ImplDX11_NewFrame()
2. ImGui_ImplWin32_NewFrame()
3. ImGui::NewFrame()
4. UIMain::Render() - 渲染 UI
5. ImGui::Render()
6. ImGui_ImplDX11_RenderDrawData()
7. SwapChain::Present()
```

## 5. 错误处理

### 5.1 异常处理

**多层异常捕获**：
- VPN 客户端层：捕获网络和设备异常
- UI 层：捕获配置解析异常
- 主程序层：捕获初始化异常

### 5.2 资源清理

**异常安全**：
- 使用智能指针
- 析构函数确保清理
- try-catch 保护关键操作

### 5.3 用户反馈

**错误提示**：
- 日志中显示错误信息
- 错误日志使用不同颜色（通过 `isError` 标志）
- 关键错误弹出消息框

## 6. 性能优化

### 6.1 渲染优化

- ImGui 使用即时模式，按需渲染
- DirectX 11 双缓冲
- 垂直同步控制

### 6.2 内存管理

- 日志行数限制（MAX_LOG_LINES = 1000）
- 缓冲区复用
- 避免不必要的内存分配

### 6.3 线程优化

- VPN 客户端在独立线程运行
- UI 线程不阻塞
- 使用原子变量减少锁竞争

## 7. 安全性

### 7.1 权限管理

**管理员权限检查**：
- 启动时检查是否以管理员身份运行
- 如果不是，提示用户并以管理员身份重启

**权限要求**：
- 创建 TUN 设备需要管理员权限
- 添加路由需要管理员权限

### 7.2 证书安全

- 证书存储在配置文件中
- 使用 SSL/TLS 加密通信
- 客户端验证服务器证书

### 7.3 输入验证

- 配置文件格式验证
- IP 地址格式验证
- 路径合法性检查

## 8. 扩展性

### 8.1 模块化设计

- UI 层与 VPN 核心层分离
- 清晰的接口定义
- 易于替换和扩展

### 8.2 配置灵活性

- JSON 配置文件
- 支持相对路径和绝对路径
- 可扩展的配置项

### 8.3 功能扩展

**可扩展功能**：
- 多配置文件管理
- 连接历史记录
- 流量统计
- 高级路由规则
- 代理设置

## 9. 已知限制

1. **平台限制**：
   - 仅支持 Windows
   - 需要 DirectX 11 支持

2. **功能限制**：
   - 单连接模式
   - 不支持多路由同时激活
   - 测试功能仅支持 ping 和 curl

3. **性能限制**：
   - 日志缓冲区大小限制
   - UI 刷新率受限于垂直同步

## 10. 未来改进方向

1. **功能增强**：
   - 多配置文件管理
   - 连接历史记录
   - 流量统计和监控
   - 高级路由规则配置
   - 代理服务器支持

2. **用户体验**：
   - 系统托盘图标
   - 开机自启动
   - 连接状态通知
   - 主题切换

3. **性能优化**：
   - 日志文件持久化
   - 配置缓存
   - 异步操作优化

4. **安全性增强**：
   - 配置文件加密
   - 自动证书更新
   - 双因素认证支持

