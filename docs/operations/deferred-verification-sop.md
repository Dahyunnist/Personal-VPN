# 待完成验证与发布证据操作指南

本清单记录已完成的本地门禁，并说明剩余验证步骤。不需要生产凭据；仅使用生成的开发 PKI。构建目录应放在仓库内，以便被 Git 忽略。WSL 下的测试 PKI 会放在 Linux 文件系统 `/tmp`，确保密钥权限检查有效。

## 已完成的本地门禁（2026-10-20）

- Linux Release，GCC 13.3.0、Boost 1.88.0、OpenSSL 3.0.13：18/18 项 CTest 通过；覆盖初始 TCP 拒绝、TLS 握手超时、拒绝过期回调、重连、路由回滚、服务端恢复及退避期间停止。扩展重连集成用例连续直接运行三次均通过。`personal-vpn-server --version` 输出 `personal-vpn-server 0.1.0`。
- 服务端 TLS 热重载变更后，Linux Release 与 Clang ASan/UBSan Debug 各通过 18/18 项 CTest。服务端集成测试覆盖有效上下文替换、拒绝无效候选上下文但不中断监听，以及替换后已建立会话继续传输。
- 指标 schema/导出变更后，Linux Release 与 Clang ASan/UBSan Debug 各通过 19/19 项测试。导出器测试校验 JSON schema v1、Prometheus counter/gauge 输出、字节/队列计数，以及不包含客户端专属标签。CLI 帮助列出两种指标格式。
- 本地 MinGW Windows ZIP 通过共用打包验证器。Linux 归档因缺少 Windows 可执行文件而被拒绝，证明验证器的负向路径有效。
- SBOM 归档回归测试使用两个位于不同包路径下的 `vcpkg.spdx.json`；输出保留两个路径且归档条目数检查通过。
- 关闭客户端传输并启用 warnings-as-errors 后，Linux Release 通过 15/15 项 CTest。
- Clang ASan/UBSan Debug 测试套件在启用泄漏检测和遇错停止后通过 18/18 项 CTest，其中包括重连生命周期用例。
- 协议 libFuzzer 冒烟测试运行 26 秒，共 928,913 次执行，无发现。这是限时冒烟测试，不代表解码器不存在漏洞。
- MSVC core Release 和 MinGW client-core 配置各通过 8/8 项测试。
- 重连变更后，MinGW GCC 14.2.0 构建 Windows 桌面 Release 包，客户端编译成功且 8/8 项 CTest 通过。Wintun 0.14.1 下载通过固定 SHA-256 校验；ZIP 含客户端、Wintun DLL 和许可证、ImGui 许可证、示例配置和 README。ZIP SHA-256：`917A629638730F7044B97A97B04B2D8D4BBA18FB8A6C19B78520590D279BBB03`。

## 主机追加验证（2026-10-21）

- 增加 Windows 特权 broker 请求编解码器：帧上限 128 字节、请求 ID 非零、仅允许三种请求类型，并校验配置文件 ID 路径安全。Release 和 Debug 桌面测试各通过 10/10 项，其中包括畸形帧拒绝。这只验证可移植编解码器；命名管道 ACL、调用方令牌绑定、服务生命周期和提权后的 Wintun 集成仍未实现。
- CI 基准增强提交（`bf0da34`）之后，使用 MinGW GCC 14.2.0 构建 Release 协议基准并完成 200,000 次迭代。其 JSON 通过与 CI 相同的必需字段、类型、范围和有限速率检查。这项 Windows 冒烟结果不代表隧道吞吐量。
- 重新运行当时的 Windows Release 和 Debug 桌面 CTest，各通过 9/9 项。MinGW 测试必须将 `C:\msys64\mingw64\bin` 加入 `PATH`；否则运行时 DLL 无法加载，测试结果无效。
- 使用缓存中的固定 ImGui/Wintun 依赖，以 warnings-as-errors 重新构建 MinGW Debug 完整桌面配置，UI 可执行文件编译成功，9/9 项 CTest 通过。Release 测试也以相同回归结果通过 9/9；覆盖 Windows 配置文件解析、未知字段拒绝、真实 TLS 私钥加载、宽泛 ACL 拒绝和 1 MiB 大小上限。提权交互、真实 Wintun 流量和 Debug 打包仍未验证。
- Windows 私钥 ACL/重解析点检查绑定到 OpenSSL 实际解析的同一文件句柄后，重新构建客户端并通过 9/9 项 CTest。凭据测试生成临时 RSA 身份，验证 TLS 上下文真实 PEM 加载，在入口处拒绝 11 种宽泛主体授权，从已验证句柄读取，并确认验证期间并发写入者不能打开密钥。PEM 缓冲区限制为 1 MiB，解析后清除；超过上限的文件会被拒绝。Windows 打包验证器也通过。
- Windows UI 改为将经过验证的 `ClientConfig` 快照传给运行时启动后，重新构建桌面目标，重跑 CTest 和打包验证。UI 不再于验证后重新打开配置文件路径；凭据仍使用外部路径，继续需要文档要求的 ACL/重解析点检查。
- Direct3D 调整大小失败处理改动后，显式重建 MinGW 桌面可执行文件、重跑 9/9 项 CTest、重新构建 Windows ZIP，并通过打包验证器。当前主机仍未验证提权驱动交互和真实隧道。
- 服务端私钥描述符加固提交（`a169098`）后，重建 Windows Release 并重跑全部 9/9 项 CTest。该密钥描述符实现仅适用于 Linux，仍需运行下文 Linux Release 和 ASan/UBSan 门禁；Windows 测试不覆盖它。
- 再次使用 `cmake/verify-windows-package.cmake` 验证 Windows Release ZIP，必需文件齐全，且未打包任何类似凭据的路径。发布校验和验证器回归测试通过 6/6。
- 使用 GCC 14.2.0 重建当前 MinGW client-core Release；在 `PATH` 包含 `C:\msys64\mingw64\bin` 时，8/8 项 CTest 通过。缺少该运行时路径会导致 MinGW DLL 加载失败，这是主机调用环境问题，不代表代码失败。
- 使用 MSVC 14.51.36231 重建当前 MSVC core Debug；8/8 项 CTest 通过。
- 私钥 ACL 加固后，使用 MinGW GCC 14.2.0 重建 Windows Release 桌面包；9/9 项 CTest 通过，覆盖仅当前用户及 11 种宽泛主体 DACL。ZIP 通过 `cmake/verify-windows-package.cmake`。
- 发布工作流 YAML 解析成功；增加发布期校验和与来源验证后，`git diff --check` 通过。发布校验和验证器 6/6 测试通过，覆盖 Windows CRLF 输入、Linux/Windows 归档、校验和被篡改/缺失、路径遍历和意外制品。工作流 YAML 具备仓库范围的 attestation 读取权限和源码检出步骤；发布验证会绑定签名者工作流与标签，并拒绝自托管运行器。
- 2026-10-21 在 WSL2 Ubuntu 中重新配置并运行当前 Linux Release 套件，20/20 项 CTest 全部通过。DrvFS 上生成的私钥会显示为 `777`；现已把 WSL 测试 PKI 移到 Linux `/tmp`，密钥权限为 `600`。该修复只调整测试证书存放位置，不放宽生产密钥检查。
- 截至 2026-10-21，Windows Release 和 Debug 桌面套件各通过 10/10 项测试；该本地 Release ZIP 因 Wintun 许可证缺失，不适合分发。此项为历史记录，不代表当前代码或制品状态。

## Broker GUI 接入阶段（2026-10-23）

- `VPNClientCore` 已切换到 `BrokerClientRuntimeControl`；普通 UI 不再要求管理员令牌，也不再直接创建 `ClientRuntime` 或调用 Windows 网络后端。连接通过认证命名管道传输不透明 profile ID，状态由 broker 轮询返回；管道丢失后由服务端清理运行时。
- GUI 导入配置时会将配置和凭据复制到当前用户保护目录。导入文件选择已改用 Unicode Windows API；profile store 使用文件流读取路径，避免非 ASCII 路径被窄字符编码破坏。
- 持续性断线保护关闭仍单独启动 UAC 确认。失败时 UI 不回退到本地网络特权路径。
- 当前 MinGW Debug 桌面套件 13 项通过；WFP 动态会话集成用例在本机 CTest 中因 opt-in 门禁而跳过。WFP 双栈策略测试覆盖策略生成、地址校验和边界；独立 CI 末尾步骤会创建/清除临时持久策略，并验证动态 Permit 会话生命周期。
- `0c44a0c` 远程 CI 与 CodeQL 全部通过。随后新增的动态 WFP 会话集成用例及 CI 步骤仍待远程验证。Windows runner 已验证安装器源路径接受/危险祖先 ACL 拒绝、真实 SCM 安装/服务启动/卸载、失败回滚及 ZIP/SBOM 校验；安装器 CI 不验证 Wintun 隧道数据面、UAC 交互、真实用户登录管道、升级回滚或多会话。

上述检查不等于真实 Windows VPN 隧道验证，也不能证明端到端性能。远程 Windows 打包门禁已通过；新增动态 WFP 会话集成步骤及受控双主机证据仍待完成。本地 ZIP 检查不能替代 vcpkg SBOM 门禁。另见[GitHub 认证操作指南](authenticated-github-sop.md)及[性能测试说明](../performance/benchmarking.md)。

## 1. 最终 Linux 与端到端测试套件

打开 WSL 后运行：

```bash
set -euo pipefail
cd /mnt/e/vpn
cmake -S . -B out/final-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/final-linux --parallel 2
ctest --test-dir out/final-linux --output-on-failure --timeout 45
out/final-linux/personal-vpn-server --version
```

预期：20 项测试全部通过，版本输出为 `personal-vpn-server 0.1.0`。

另外，验证仅启用服务端的配置组合：

```bash
cmake -S . -B out/final-server-only -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=OFF \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/final-server-only --parallel 2
ctest --test-dir out/final-server-only --output-on-failure --timeout 45
```

该配置会有意省略仅客户端 TLS 测试；CMake 不得引用已关闭的目标。

## 2. Sanitizer

```bash
set -euo pipefail
cd /mnt/e/vpn
CC=clang CXX=clang++ cmake -S . -B out/final-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPERSONAL_VPN_ENABLE_SANITIZERS=ON \
  -DPERSONAL_VPN_WARNINGS_AS_ERRORS=ON
cmake --build out/final-sanitized --parallel 2
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
ctest --test-dir out/final-sanitized --output-on-failure --timeout 60
```

出现任何 sanitizer 报告都视为本阶段失败，即使测试进程因其他原因返回零退出码。

## 3. 协议 fuzz 目标

```bash
set -euo pipefail
cd /mnt/e/vpn
CC=clang CXX=clang++ cmake -S . -B out/final-fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=OFF \
  -DPERSONAL_VPN_BUILD_TESTS=OFF \
  -DPERSONAL_VPN_BUILD_FUZZING=ON
cmake --build out/final-fuzz --parallel 2
mkdir -p out/final-fuzz/corpus
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
out/final-fuzz/frame-decoder-fuzz \
  -dict=fuzz/protocol.dict -max_total_time=60 -timeout=5 \
  out/final-fuzz/corpus
```

修改代码前先保留任何崩溃输入。限时冒烟测试是 CI 门禁，不代表解析器不存在漏洞。

## 4. 原生 MSVC 客户端与打包

打开 **Developer PowerShell for VS 2022 (x64)**，将 `VCPKG_ROOT` 设置为可信 vcpkg 检出目录，然后运行：

```powershell
Set-Location E:\vpn
cmake -S . -B out/final-msvc-client -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static `
  -DVCPKG_INSTALLED_DIR="$PWD/out/final-msvc-client/vcpkg_installed" `
  '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>' `
  -DPERSONAL_VPN_BUILD_LINUX_TRANSPORT=OFF `
  -DPERSONAL_VPN_BUILD_CLIENT_TRANSPORT=ON `
  -DPERSONAL_VPN_BUILD_WINDOWS_UI=ON `
  -DPERSONAL_VPN_FETCH_WINTUN=ON
cmake --build out/final-msvc-client --config Release --parallel 2
ctest --test-dir out/final-msvc-client -C Release --output-on-failure --timeout 45
cmake --build out/final-msvc-client --config Release --target package
Get-ChildItem out/final-msvc-client/vcpkg_installed -Recurse -Filter '*.spdx.json'
```

将 ZIP 解压到新目录。包内必须包含客户端可执行文件、Wintun DLL、示例配置、文档，以及 ImGui 和 Wintun 许可证。确认 Wintun 归档下载没有哈希不匹配。使用与 CI 和发布流程相同的包门禁：

```powershell
$package = Get-ChildItem out/final-msvc-client -Filter 'personal-vpn-*.zip' |
    Select-Object -First 1
cmake "-DPACKAGE_ARCHIVE=$($package.FullName)" -P cmake/verify-windows-package.cmake
```

## 5. 受控隧道证据

使用[性能测试说明](../performance/benchmarking.md)中的双主机设置和直连/VPN 对照方法。每种条件运行三次，并执行 1、6、24 小时长稳测试。保留原始 iperf3 JSON、RTT 样本、CPU/RSS、指标、构建 ID、路由快照和脱敏环境元数据。

这一步完成且结果可重复前，不得在简历中写吞吐量、延迟、并发、运行时长或优化百分比。

## 6. 记录结果

创建带日期、经过脱敏的 Markdown 摘要，记录命令、提交、环境、通过/失败数量、原始结果哈希和已知偏差。随后在一个聚焦提交中更新 README 证据及变更日志。GitHub 认证/推送/发布步骤见[认证操作指南](authenticated-github-sop.md)。
