#!/bin/bash

echo "=== VPN服务端修复版编译脚本 ==="

# 检查依赖
echo "检查编译依赖..."
if ! command -v g++ &> /dev/null; then
    echo "错误: g++ 未安装"
    exit 1
fi

# 检查所需库
echo "检查库依赖..."
MISSING_LIBS=""

if ! pkg-config --exists openssl; then
    MISSING_LIBS="$MISSING_LIBS libssl-dev"
fi

if ! ldconfig -p | grep -q libboost_system; then
    MISSING_LIBS="$MISSING_LIBS libboost-all-dev"
fi

if [ ! -z "$MISSING_LIBS" ]; then
    echo "错误: 缺少以下库: $MISSING_LIBS"
    echo "请运行: sudo apt update && sudo apt install $MISSING_LIBS"
    exit 1
fi

# 检查证书文件
echo "检查SSL证书..."
if [ ! -f "certs/server.crt" ] || [ ! -f "certs/server.key" ]; then
    echo "警告: SSL证书文件不存在"
    echo "请确保以下文件存在:"
    echo "  - certs/server.crt"
    echo "  - certs/server.key"
    echo ""
    echo "如果没有证书，可以使用以下命令生成自签名证书："
    echo "mkdir -p certs"
    echo "openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes"
fi

# 编译
echo "开始编译..."
g++ server_fixed.cpp -o server_fixed \
    -lboost_system \
    -lboost_thread \
    -lpthread \
    -lssl \
    -lcrypto \
    -std=c++11

if [ $? -eq 0 ]; then
    echo "✅ 编译成功！"
    echo ""
    echo "运行方法："
    echo "sudo ./server_fixed 443"
    echo ""
    echo "注意事项："
    echo "1. 需要root权限来创建TUN设备和配置iptables"
    echo "2. 确保服务器的物理网卡名称正确（当前设置为eth0）"
    echo "3. 如果防火墙开启，请允许端口443"
else
    echo "❌ 编译失败！"
    exit 1
fi