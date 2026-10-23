# 客户端配置安全

重构后的客户端接受带版本号的 JSON 文档，其中只保存凭据文件路径，不嵌入证书或私钥。协议 v2 双向 TLS 成功后，虚拟 IPv4/IPv6 地址由服务端分配。配置 v1 保持原有信任规则；配置 v2 强制启用服务器叶证书 CRL 检查。

## Schema 版本 1

```json
{
  "schema_version": 1,
  "server": {
    "host": "vpn.example.test",
    "port": 8443,
    "expected_name": "vpn.example.test"
  },
  "tls": {
    "ca_file": "credentials/ca.crt",
    "certificate_chain_file": "credentials/client.crt",
    "private_key_file": "credentials/client.key"
  },
  "tunnel": {
    "requested_mtu": 1400,
    "routes": ["10.20.0.0/16", "fd00:8::/64"],
    "allow_default_route": false
  }
}
```

相对于配置文件的凭据路径以配置文件所在目录为基准解析，不以进程工作目录为基准。v1 的三个凭据路径都必须指向普通文件；v2 的 CRL 路径也必须指向 8 MiB 以下的普通文件。相同且已验证的 `expected_name` 同时用于 OpenSSL 主机名验证和客户端 SNI；`host` 仅用于网络解析。因此，即使连接固定 IP，也可以固定验证 DNS 身份，而不削弱证书检查。

## 失败即拒绝的校验规则

- 配置文件最大为 64 KiB；只接受 schema v1 和 v2。v1 沿用原字段；v2 必须显式包含非空 `tls.server_crl_file`，并启用吊销检查。
- 拒绝未知字段和重复对象字段。
- 拒绝内嵌凭据以及旧版 `certs` 对象。
- 在建立连接前限制端口、MTU、字段长度、路由数量，并校验 CIDR 语法及规范网络地址。
- `0.0.0.0/0` 和 `::/0` 必须显式设置 `allow_default_route: true`；格式错误或意外的默认路由不能静默地把分流隧道变为全隧道。
- 全隧道配置的 `server.host` 必须是 IPv4/IPv6 字面地址，不能使用域名；这样持久断线保护启用后，不需要向外部 DNS 发送查询。TLS 证书身份仍由 `expected_name` 单独验证。
- 配置文件中没有客户端指定的 TUN 地址或网关；这些值由经过认证的 v2 服务端协议消息决定。旧 v1 客户端与 v2 服务端不互通。

## Schema 版本 2：服务器证书吊销

v2 保留 v1 字段，并在 `tls` 中增加必需的 `server_crl_file`。相对路径仍以配置文件所在目录为基准。该字段不能留空，必须指向不超过 8 MiB 的 PEM CRL；加载失败、缺少合适 CRL 或服务器证书被撤销时，TLS 握手失败。OpenSSL 对服务器叶证书执行 CRL 检查；配置中的 CRL 应由签发该服务器证书的 CA 发布。旧客户端拒绝 schema v2，因此回滚时需保留未修改的 v1 profile。完整示例见 [`configs/client-v2.example.json`](../../configs/client-v2.example.json)。

```json
"schema_version": 2,
"tls": {
  "ca_file": "credentials/ca.crt",
  "certificate_chain_file": "credentials/client.crt",
  "private_key_file": "credentials/client.key",
  "server_crl_file": "credentials/server.crl"
}
```

## Windows 存储策略

生产配置文件和密钥应存放在当前用户的应用数据目录下，ACL 仅授予该用户、Administrators 和 SYSTEM。Windows 客户端会拒绝私钥重解析点、不受限制的 DACL、未知授权 ACE，以及向白名单外主体授予的读取权限。

不得将密钥放在可执行文件旁、仓库内或共享的 Downloads/Desktop 目录中。将密钥导入 Windows 受保护密钥存储仍是未来工作。

重解析点和 ACL 检查针对已打开的文件句柄执行。客户端在持有该句柄期间禁止其他进程共享写入/删除，并从同一句柄读取和解析密钥，从而关闭权限校验与 OpenSSL 使用之间的路径替换窗口。PEM 输入限制为 1 MiB，临时内存缓冲区会在解析后清零。
