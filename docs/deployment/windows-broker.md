# Windows broker 服务部署

当前 Windows ZIP 包含桌面客户端、broker 服务、Wintun 和本安装脚本。脚本仅支持新安装；已有服务必须先卸载。真实 Wintun 隧道尚未在虚拟机验证，不能视为生产认证。

## 安装

1. 从可信发布页下载 ZIP 和校验文件。在管理员 PowerShell 中建立受保护暂存目录，再把 ZIP 和策略文件放入其中。安装器会拒绝 UNC 路径，并检查包、策略、每个父目录及文件的所有者、DACL 和 reparse 状态；用户可写源路径会被拒绝。

```powershell
$stage = Join-Path $env:ProgramFiles 'PersonalVPN-Staging'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
icacls $stage /inheritance:r
icacls $stage /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F'
icacls $stage /setowner '*S-1-5-32-544' /T /C
# 复制 ZIP 和策略文件到 $stage，再核验 ZIP 哈希并解压。
icacls $stage /setowner '*S-1-5-32-544' /T /C
```
2. 复制 ZIP 中 `share\doc\personal-vpn\configs\broker-policy.example.json`，按组织批准的目标路由修改。默认路由默认关闭。
3. 在管理员 PowerShell 中运行：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned
Unblock-File .\deploy\windows\install-broker.ps1
.\deploy\windows\install-broker.ps1 -Action Install `
  -PackageDirectory 'C:\Staging\personal-vpn' `
  -PolicyFile 'C:\Staging\broker-policy.json'
```

脚本把服务和客户端文件安装到 `%ProgramFiles%\PersonalVPN\bin`，把机器策略复制到 `%ProgramData%\PersonalVPN\broker-policy.json`。服务以 LocalSystem 运行，失败后每分钟重启，最多连续三次。普通用户对程序文件只有读取/执行权限，对机器策略无权限。

安装不覆盖既有服务。卸载后重装时，必须再次提供与保留策略完全相同的文件；更换策略前先由管理员备份并审查旧文件。执行前应通过 `-WhatIf` 查看目标。安装后检查 `PersonalVPNBroker` 服务状态和 Windows 系统事件日志。无效/缺失策略会使服务停止，安装器不会生成宽松默认策略。

## 卸载

在管理员 PowerShell 中运行：

```powershell
.\deploy\windows\install-broker.ps1 -Action Uninstall
```

卸载会停止并删除服务及 `%ProgramFiles%\PersonalVPN` 程序文件；保留 `%ProgramData%\PersonalVPN\broker-policy.json`，避免静默丢失管理员配置。确认不再需要后，管理员可另行删除该策略目录。

## 限制

- 服务当前只发现活动物理控制台会话，不支持 RDP 和多用户并行。
- UI 尚未接入 broker，当前客户端仍需管理员权限。
- 脚本未实现升级回滚，也未验证服务 ACL、重启恢复、路由清理或真实网络连通性。
