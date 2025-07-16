# VPN 编译和运行指南

## 问题修复说明

已修复以下关键问题：
1. **服务端缺少VPN转发功能** - 重新启用TUN设备和双向数据转发
2. **SSL初始化验证不足** - 添加完善的SSL握手和状态检查
3. **数据格式不匹配** - 统一客户端和服务端的数据处理格式
4. **错误处理不完整** - 改进SSL读写错误处理

## 编译服务端

### 在Linux虚拟机上编译服务端：
```bash
cd /workspace/task1/server
g++ server_fixed.cpp -o server_fixed -lboost_system -lboost_thread -lpthread -lssl -lcrypto
```

### 运行服务端（需要root权限）：
```bash
sudo ./server_fixed 443
```

## 编译客户端

### 在Windows上编译客户端：
```cmd
cd task1\client
g++ client.cpp -o client.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
```

### 运行客户端（需要管理员权限）：
```cmd
client.exe <服务器IP> 443
```

## 运行前检查

### 服务端检查（Linux）：
1. 确保证书文件存在：
   ```bash
   ls -la certs/server.crt certs/server.key
   ```

2. 检查TUN设备支持：
   ```bash
   ls -la /dev/net/tun
   ```

3. 检查防火墙设置：
   ```bash
   sudo ufw allow 443
   # 或
   sudo iptables -A INPUT -p tcp --dport 443 -j ACCEPT
   ```

### 客户端检查（Windows）：
1. 确保证书文件存在：
   ```cmd
   dir certs\client.crt certs\client.key certs\server.crt
   ```

2. 确保wintun.dll存在于客户端目录

3. 以管理员身份运行命令提示符

## 测试连接

### 1. 启动服务端：
```bash
sudo ./server_fixed 443
```

### 2. 启动客户端：
```cmd
client.exe <虚拟机IP> 443
```

### 3. 测试VPN连接：
在客户端Windows机器上测试：
```cmd
# 检查VPN接口
ipconfig
# 应该看到VPNTunnel接口，IP为10.8.0.2

# 测试通过VPN ping
ping 8.8.8.8
```

## 预期日志输出

### 服务端输出：
```
TUN设备创建成功: tun0 (fd: X)
TUN设备配置完成: tun0 10.8.0.1/24
iptables NAT规则配置完成: 10.8.0.0/24 -> eth0
SSL上下文配置成功
VPN服务器已启动，监听端口: 443
客户端已连接（TLS加密）: <客户端IP>:端口
SSL → TUN: 接收到 X 字节数据
SSL → TUN: 成功转发 X 字节到TUN设备
```

### 客户端输出：
```
TUN adapter LUID: 0xXXXXXXXX
SSL connection established with <服务器IP>:443
SSL handle verified, starting data forwarding threads...
TUN received a packet, size: X bytes
TUN -> SSL: Sent X bytes
SSL -> TUN: 接收到 X 字节数据
SSL -> TUN: 成功写入 X 字节到TUN设备
```

## 故障排除

### 1. SSL握手失败：
- 检查证书文件是否正确
- 验证服务器IP和端口
- 检查防火墙设置

### 2. TUN设备创建失败：
- 确保以管理员/root权限运行
- 检查TUN/TAP驱动是否安装

### 3. 数据传输失败：
- 检查路由配置
- 验证iptables规则
- 查看日志输出中的错误信息

### 4. 连接断开：
- 检查网络稳定性
- 查看SSL错误信息
- 验证证书有效期