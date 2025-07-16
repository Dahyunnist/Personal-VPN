#!/bin/bash

echo "=== SSL证书生成脚本 ==="

# 创建证书目录
mkdir -p certs

# 生成服务端证书
echo "正在生成服务端证书..."
openssl req -x509 -newkey rsa:2048 -keyout certs/server.key -out certs/server.crt \
    -days 365 -nodes -subj "/C=CN/ST=Test/L=Test/O=TestVPN/CN=vpn-server"

# 生成客户端证书（可选，用于双向认证）
echo "正在生成客户端证书..."
openssl req -x509 -newkey rsa:2048 -keyout certs/client.key -out certs/client.crt \
    -days 365 -nodes -subj "/C=CN/ST=Test/L=Test/O=TestVPN/CN=vpn-client"

# 复制证书给客户端使用
if [ ! -d "../client/certs" ]; then
    mkdir -p ../client/certs
fi

cp certs/server.crt ../client/certs/
cp certs/client.crt ../client/certs/
cp certs/client.key ../client/certs/

# 设置正确的权限
chmod 600 certs/*.key
chmod 644 certs/*.crt

echo "✅ 证书生成完成！"
echo ""
echo "生成的文件："
echo "  服务端证书: certs/server.crt"
echo "  服务端私钥: certs/server.key"
echo "  客户端证书: certs/client.crt"
echo "  客户端私钥: certs/client.key"
echo ""
echo "客户端证书已复制到: ../client/certs/"
echo ""
echo "现在可以运行VPN服务端了："
echo "sudo ./server_fixed 443"