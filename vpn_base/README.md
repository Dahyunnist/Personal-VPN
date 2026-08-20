# VPN Base 使用说明

## 项目简介

VPN Base 是一个基于 TUN/TAP 设备的 VPN 系统，支持 Linux 服务器和 Windows 客户端。系统使用 SSL/TLS 加密通信，提供安全的网络隧道服务。

## 快速开始

### 服务器端（Linux）

#### 1. 编译

```bash
cd server
make
```

#### 2. 准备证书

将 SSL 证书和私钥放置在 `certs/` 目录：
- `server.crt` - 服务器证书
- `server.key` - 服务器私钥

#### 3. 运行服务器

```bash
sudo ./vpn_server <端口号>
```

例如：
```bash
sudo ./vpn_server 10043
```

**注意**：需要 root 权限以创建 TUN 设备和配置网络。

#### 4. 生成客户端配置

在服务器运行时，输入命令：
```
genconfig
```

这将在当前目录生成 `config.json` 文件，包含客户端连接所需的所有信息。

### 客户端（Windows）

#### 1. 编译

使用 MinGW 或 Visual Studio 编译：

```bash
cd client
g++ client.cpp -o client.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
```

#### 2. 准备文件

- 将 `wintun.dll` 放置在可执行文件同目录
- 将服务器生成的 `config.json` 放置在可执行文件同目录

#### 3. 运行客户端

**以管理员身份运行**（需要管理员权限创建网络适配器）：

```bash
client.exe <路由IP>
```

例如：
```bash
client.exe 110.242.68.66
```

其中 `<路由IP>` 是您希望通过 VPN 访问的目标 IP 地址。

## 配置文件说明

### 服务器配置

服务器配置在代码中定义（`include/config.h`），主要配置项：

- `TUN_DEV`: TUN 设备名称（默认：`tun0`）
- `SERVER_TUN_IP`: 服务器 TUN IP（默认：`10.8.0.1`）
- `TUN_MASK`: 子网掩码（默认：`24`）
- `PHYSICAL_NIC`: 物理网卡名称（默认：`ens33`）
- `IP_POOL_START`: IP 池起始地址（默认：`10.8.0.2`）
- `IP_POOL_END`: IP 池结束地址（默认：`10.8.0.254`）

### 客户端配置

客户端配置文件 `config.json` 格式：

```json
{
    "server": {
        "ip": "192.168.10.14",
        "port": 10043
    },
    "tun": {
        "ip": "10.8.0.2",
        "mask": "24"
    },
    "certs": {
        "client_crt": "-----BEGIN CERTIFICATE-----\n...",
        "client_key": "<load from a local credential file; never commit it>",
        "server_crt": "-----BEGIN CERTIFICATE-----\n..."
    }
}
```

## 服务器命令

服务器运行时支持以下命令：

- `genconfig` - 生成客户端配置文件
- `quit` - 退出服务器

## 使用示例

### 基本使用流程

1. **启动服务器**：
   ```bash
   sudo ./vpn_server 10043
   ```

2. **生成客户端配置**：
   在服务器控制台输入：`genconfig`

3. **配置客户端**：
   将生成的 `config.json` 复制到客户端

4. **启动客户端**：
   ```bash
   # 以管理员身份运行
   client.exe 110.242.68.66
   ```

5. **验证连接**：
   - 检查 TUN 设备是否创建
   - 测试目标 IP 的连通性

### Windows 客户端验证

检查 VPN 适配器：
```powershell
Get-NetAdapter -Name "*VPN*"
```

检查路由：
```cmd
route print | findstr "110.242.68.66"
```

## 网络配置

### 服务器端自动配置

服务器启动时会自动：
1. 启用 IP 转发
2. 创建并配置 TUN 设备
3. 配置 iptables NAT 规则

### 客户端路由

客户端会自动添加路由规则，将指定 IP 的流量路由到 VPN 隧道。

## 故障排除

### 服务器端问题

1. **TUN 设备创建失败**：
   - 检查是否有 root 权限
   - 检查 TUN 模块是否加载：`lsmod | grep tun`

2. **端口被占用**：
   - 检查端口是否被其他程序使用
   - 使用 `netstat -tuln` 查看端口占用

3. **iptables 配置失败**：
   - 检查 iptables 是否安装
   - 检查是否有 root 权限

### 客户端问题

1. **Wintun 初始化失败**：
   - 确保 `wintun.dll` 在可执行文件目录
   - 确保以管理员身份运行

2. **SSL 连接失败**：
   - 检查服务器是否运行
   - 检查防火墙设置
   - 验证证书是否正确

3. **路由添加失败**：
   - 确保以管理员身份运行
   - 检查 IP 地址格式是否正确

## 安全注意事项

1. **证书安全**：
   - 妥善保管私钥文件
   - 定期更新证书
   - 使用强密码保护私钥

2. **网络安全**：
   - 配置防火墙规则
   - 限制服务器访问
   - 使用强密码

3. **权限管理**：
   - 服务器需要 root 权限
   - 客户端需要管理员权限
   - 最小权限原则

## 系统要求

### 服务器端

- Linux 操作系统
- Root 权限
- Boost 库
- OpenSSL 库
- iptables

### 客户端

- Windows 操作系统
- 管理员权限
- Boost 库
- OpenSSL 库
- Wintun DLL

## 依赖库

### 服务器端

- `boost_system`
- `boost_thread`
- `pthread`
- `ssl`
- `crypto`

### 客户端

- `ws2_32` (Windows Socket)
- `iphlpapi` (IP Helper API)
- `ole32` (COM)
- `ssl` (OpenSSL)
- `crypto` (OpenSSL)
- `boost_thread-mt` (Boost Thread)

## 许可证

本项目仅供学习和研究使用。

## 联系方式

如有问题或建议，请提交 Issue 或联系项目维护者。

