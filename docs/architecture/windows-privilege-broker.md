# Windows 特权 broker

## 设计决策

桌面 UI 应以普通用户权限运行。当前路由后端调用 `CreateIpForwardEntry2`，而 [Microsoft 文档说明该 API 要求管理员令牌](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-createipforwardentry2)。小型 Windows 服务负责 TLS 客户端、凭据加载、Wintun 会话、接口配置和路由事务。

当前 SCM 宿主使用 `WTSGetActiveConsoleSessionId` 与 `WTSQueryUserToken` 获取活动物理控制台会话的登录 SID，并按该 SID 创建命名管道；服务需以 LocalSystem 运行。此设计不支持 RDP 会话和多用户并行。真实令牌权限、服务 SID 限制、安装 ACL 与恢复行为尚未在 VM 验证；不得据此宣称已达到最小权限或生产部署标准。

服务端必须先认证 VPN 服务端，并验证 `IP_ASSIGN`，之后才能修改网络状态。这样可以防止被攻破或篡改的 UI 要求特权进程安装任意路由。

在服务路径通过下述验收门槛前，保留现有 UI 自行提权路径。broker 集成完成前，不要删除旧路径，也不要发布会静默连接失败的 UI。

## 信任边界与 IPC 约定

使用带版本和长度前缀的本地命名管道协议。服务端拒绝远程管道客户端、格式错误或超大消息、未知版本、未知消息类型及重复请求 ID。每条连接最多接受 256 个唯一请求 ID；达到上限后必须关闭连接，避免重放跟踪内存无界增长。初始消息只允许：

Windows broker pipe helper 创建固定名称的首个实例，DACL 只授予 SYSTEM、Administrators 和服务提供的授权登录 SID，并启用 `PIPE_REJECT_REMOTE_CLIENTS`。连接后认证函数模拟客户端，要求至少 `SecurityImpersonation` 令牌级别，核对管道会话 ID、线程令牌会话 ID及令牌中的登录 SID，并保证恢复服务身份。SCM 宿主已接入该 helper、请求会话和运行时注册表；客户端 helper 会以 `SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION` 打开管道并发送受限请求。Windows 集成测试覆盖真实管道往返，但尚未验证安装服务身份下的 ACL、远程拒绝和异常恢复。

- `Connect(profile_id)`；
- `Disconnect(request_id)`；
- `GetStatus(request_id)`。

请求/响应都使用同一版本、长度前缀和 request ID。响应仅承载 `Accepted`、`Status` 或 `Rejected`，可带连接状态和最多 480 字节诊断；不返回凭据、路径或路由。UI 必须核对响应 request ID 后才更新对应操作状态。

协议不得接受调用方提供的 IP 地址、网关、路由前缀、接口名、适配器名、原始数据包、可执行文件路径或任意命令。broker 只能在请求用户获准的配置文件根目录中解析配置。读取配置和凭据文件时，应模拟该用户身份；针对已打开句柄检查现有私钥 ACL 和大小限制，再恢复 broker 身份后执行网络操作。密钥字节绝不通过 IPC 传输。

`load_client_config(path, ClientNetworkPolicy)` 已提供路由约束入口：profile 中的每条路由必须精确匹配受信任策略，默认路由也需策略单独授权。SCM 服务使用管理员保护的机器策略；无策略重载仍用于现有提权式客户端，不构成 broker 授权。

`parse_broker_network_policy` 解析版本化机器策略，严格限制字段、CIDR、重复项和路由数量。Windows `load_broker_network_policy` 要求策略文件及直接父目录由 SYSTEM/Administrators 拥有，拒绝非可信主体写权限、空 DACL、reparse 点和超过 64 KiB 的文件，并从已校验句柄读取。SCM 宿主从 `%ProgramData%\PersonalVPN\broker-policy.json` 加载此策略；管理员安装脚本创建目录、设置 DACL/所有者并部署策略。脚本范例为 [`deploy/windows/install-broker.ps1`](../../deploy/windows/install-broker.ps1)。

Windows 路径 helper `resolve_impersonated_profile_path(profile_id)` 只接受协议限定的 ASCII ID，并从当前线程模拟令牌读取用户 profile 目录，固定解析到 `%USERPROFILE%\AppData\Local\PersonalVPN\profiles\<id>.json`。缺少模拟令牌即失败；调用方不能传入路径。SCM 宿主已通过 `load_impersonated_profile` 在用户模拟身份下加载 profile，并应用服务启动时读取的受信网络策略。helper 不创建目录；安装路径 ACL 和真实用户 profile 生命周期仍待验证。

`BrokerRuntimeRegistry` 已将 `Connect`、`Disconnect`、`GetStatus` 映射到 `ClientRuntimeControl`，按已认证的 TokenUser SID 限制每用户单一运行时，状态响应只返回固定短消息。SCM 宿主与 `serve_broker_pipe_session` 已串起会话发现、认证、请求解码、调用者模拟、registry 派发、关联响应和断线清理；客户端断开、控制台会话注销或服务停止后会停止并 join 运行时。管理员安装脚本设置 LocalSystem 服务和失败重启策略，支持卸载但不覆盖现有安装。仍未实现 RDP/多会话支持、升级回滚和 UI 长连接客户端。

Windows `BrokerClientRuntimeControl` 已提供桌面端运行时适配器：保持单条认证管道、发送 Connect、轮询 Status、发送 Disconnect；传输失败或客户端关闭会关闭管道，让服务端执行运行时清理。Windows 管道集成测试覆盖 Connect/Status/Disconnect 生命周期。当前 `VPNClientCore` 仍直接创建本地 `ClientRuntime`，该适配器尚未接入 GUI。

`store_broker_profile` 已将经现有解析器验证的 profile 与三份凭据复制到当前用户固定的 `%USERPROFILE%\AppData\Local\PersonalVPN\profiles` 根目录。它生成不透明 ID、重写凭据相对路径，拒绝 reparse 目录和非普通/超大凭据，并为目录、私钥和配置设定仅当前用户、SYSTEM、Administrators 可访问的 DACL。Windows 凭据测试会重新加载复制后的配置并建立 TLS 上下文。当前 GUI 尚未调用此导入器；导入产生的 profile 尚无 GUI 清理/管理流程。

管道 DACL 只向明确授权的交互式用户 SID、Administrators 和 SYSTEM 授权。每个连接都要模拟调用方，检查令牌用户和交互会话，并将请求绑定到该身份。不得信任消息数据中提供的 SID、用户名、配置文件路径或会话 ID。每个用户最多建立一个活动隧道。断开连接、管道中断、注销、服务停止或传输故障，都必须使用现有幂等关闭和路由回滚路径。

当前安装脚本配置服务为 LocalSystem，因为会话发现使用 `WTSQueryUserToken`。准确令牌权限和最小权限替代方案尚未通过提升权限集成测试验证。保护 UI/服务二进制、服务配置和更新路径，禁止普通用户写入；安装脚本仅允许管理员执行。

## 集成顺序

1. 已从 `ClientRuntime` 抽取生命周期控制接口，UI 当前仍实例化本地运行时；TLS、重连、事务和 join 完整关闭逻辑未变。
2. 有界请求/响应编解码器已有可移植往返与负向测试，并已加入 libFuzzer 目标。Windows 集成测试覆盖当前用户登录 SID/会话认证、请求解码和响应关联；仍需以真实服务身份验证 ACL、远程拒绝及跨进程重放处理。
3. 已在 SCM 宿主承载现有客户端运行时和 `WindowsNetworkBackend`。配置和私钥在调用方身份下读取；服务从受保护配置加载 `ClientNetworkPolicy` 并使用受限重载；TLS 验证仍在服务进程内完成。仅完成编译和自动化测试，未做真实服务安装或网络验证。
4. 已增加 broker IPC 运行时适配器；待将 `VPNClientCore` 对运行时的直接所有权改为该客户端。UI 消息只公开状态和有界诊断，不携带凭据或路由。
5. 已增加仅支持新安装的管理员脚本、LocalSystem 服务配置、失败重启和保留策略的卸载。待实现升级/回滚流程及提权集成测试。之后才能移除 UI 自行提权，并验证 UI 令牌未提权时，真实隧道配置和清理仍然成功。

## 验收门槛

- UI 可在普通用户令牌下启动和运行；只有安装/升级需要提权。
- 普通用户无法修改或替换服务文件、服务配置或 broker 策略。
- 管道调用方不能冒充其他用户/会话、远程连接，或绕过 TLS 认证后的地址分配请求直接提交网络变更。
- 无效、过期、超大、重放或不支持的 IPC 消息必须失败并关闭。
- broker 停止、UI 崩溃、管道中断、重连和服务重启后，不留下过期路由、地址、适配器会话或工作线程。
- 在一次性 Windows 虚拟机中，以预期服务身份通过真实 Wintun 和路由集成测试，且 UI 不提权。

所有门槛通过前，不得声称特权拆分已经完成。SCM broker 宿主和初版管理员安装/卸载脚本已进入代码及 Windows 构建，但未验证真实服务 ACL、升级回滚或 Wintun 网络行为；UI 仍直接调用 Windows 网络后端。本文记录实现现状与未通过的验收项，不代表生产就绪证明。
