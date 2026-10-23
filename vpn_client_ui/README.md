# Personal-VPN Windows 客户端

桌面程序使用 Win32、DirectX 11 和 Dear ImGui。普通 UI 通过经身份验证的 Windows 命名管道调用 SCM broker；UI 不直接创建 Wintun，也不修改 IP 路由。

## 运行方式

- 导入配置前会校验 profile，并把配置及三份凭据复制到当前用户保护目录：`%USERPROFILE%\AppData\Local\PersonalVPN\profiles`。
- 导入会生成不透明 profile ID；UI 只向 broker 发送该 ID，不通过 IPC 传凭据、路由或任意文件路径。
- broker 在调用者身份下读取 profile，使用管理员保护的机器策略限制路由，再启动客户端运行时。
- 服务端通过双向 TLS 验证客户端身份，并分配客户端地址、网关、前缀和 MTU。
- 可在隧道断开后删除当前导入的 profile；这会清除应用目录内配置及凭据副本，不会删除原始导入文件。
- UI 状态来自 broker 的运行时状态，不会把“请求已受理”误报成连接成功。
- 正常停止会请求 broker 断开；UI 崩溃或管道中断时，服务端关闭运行时并清理其创建的网络状态。
- 关闭持续性断线保护会单独请求 UAC 提权。

UI 当前只管理本次运行中选中的 profile，不提供已保存 profile 列表，也不删除其他 profile。删除操作会二次确认；若文件锁定或部分删除失败，可再次执行清理。不要修改服务策略来绕过授权。

## 构建

从仓库根目录构建。CMake 工具链（例如 vcpkg）需为所选 Windows 编译器提供 Boost 和 OpenSSL。

```powershell
cmake -S . -B out/windows-client -A x64 `
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=ON `
  -DPERSONAL_VPN_BUILD_WINDOWS_UI=ON `
  -DPERSONAL_VPN_WINTUN_DLL=C:/deps/wintun/bin/amd64/wintun.dll
cmake --build out/windows-client --config Release --parallel
ctest --test-dir out/windows-client -C Release --output-on-failure
```

Dear ImGui 使用固定完整提交 ID。离线构建可用 `PERSONAL_VPN_IMGUI_DIR` 指向现有源码。离线 Wintun 构建需提供经独立校验的 `PERSONAL_VPN_WINTUN_DLL`；也可显式启用 `PERSONAL_VPN_FETCH_WINTUN=ON`，下载 Wintun 0.14.1 并校验固定 SHA-256。

## 启动与限制

普通客户端 UI 不需要管理员权限。首次使用前，管理员须按 [`docs/deployment/windows-broker.md`](../docs/deployment/windows-broker.md) 安装 broker 并配置机器路由策略。导入 schema v1 profile，例如 [`configs/client.example.json`](../configs/client.example.json)，再点击“连接 VPN”。客户端地址由服务端分配，不可在 UI 中编辑。

broker 目前只支持活动物理控制台会话。真实服务身份、服务 ACL、升级/回滚及 Wintun 隧道尚未通过 Windows 虚拟机验证；当前实现不能视为生产认证。密钥历史与 ACL 要求见 [`docs/security/client-configuration.md`](../docs/security/client-configuration.md)。
