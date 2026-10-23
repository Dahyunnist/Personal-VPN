# GitHub 认证与发布操作指南

当开发环境不能直接访问 GitHub 时，由仓库所有者按本指南完成需要本人认证的步骤。请在可信电脑的仓库根目录运行命令。不要把令牌、私钥、证书或一次性验证码粘贴到 issue、提交、构建日志或聊天中。

## 1. 认证

任选一种方式。

### 通过 HTTPS 使用 GitHub CLI

```powershell
gh auth login --hostname github.com --git-protocol https --web
gh auth status
```

请在浏览器中使用对 `Dahyunnist/Personal-VPN` 有写入权限的账号完成认证。

### 使用已有 SSH 密钥

```powershell
ssh -T git@github.com
```

若没有可信密钥，请遵循 GitHub 当前 SSH 密钥文档创建并配置。私钥必须放在仓库之外，使用操作系统凭据存储保护，并设置口令。

## 2. 写入前核对目标

```powershell
git remote -v
git remote get-url personal-vpn
git branch --show-current
git status --short
```

目标必须是 `git@github.com:Dahyunnist/Personal-VPN.git`（或其 HTTPS 等价地址）。远程默认分支为 `master`。不要推送到无关的 `origin` 远程。

## 3. 核对本地改动

以下命令列出尚未包含在指定远程分支中的提交；以实际输出为准，不要依赖固定提交数。

```powershell
git fetch personal-vpn
git log --oneline personal-vpn/master..HEAD
git status --short
git diff --check
```

不要把生成的构建目录或安装包提交到 Git。

## 4. 推送并检查 CI

只有在目标分支和改动确认无误后才推送。新功能通常应通过 Pull Request 合并；若直接推送受分支规则保护的 `master`，GitHub 会拒绝写入。

```powershell
git push personal-vpn HEAD
gh run list --repo Dahyunnist/Personal-VPN --branch (git branch --show-current) --limit 10
$runId = gh run list --repo Dahyunnist/Personal-VPN `
  --branch (git branch --show-current) --limit 1 --json databaseId --jq '.[0].databaseId'
gh run watch $runId --repo Dahyunnist/Personal-VPN --exit-status
```

如果打包验证失败，先打开对应的 Actions 运行和失败任务，复制第一条实际错误及少量上下文进行排查。不要分享令牌、Cookie、私钥或无关的运行器环境信息。先修复确定性错误，再重跑工作流；不要反复重跑来掩盖稳定失败。

所有检查通过后，如需创建 Pull Request：

```powershell
gh pr create --repo Dahyunnist/Personal-VPN `
  --base master `
  --head <feature-branch> `
  --title "简明描述改动" `
  --body-file .github/pull_request_template.md
```

审查失败结果。不要将失败或未执行的任务标为成功。

## 5. 创建版本化发布

合并改动后更新本地默认分支：

```powershell
git switch master
git pull --ff-only personal-vpn master
git status --short
```

确认 `CMakeLists.txt` 中 `project(PersonalVPN VERSION ...)` 的版本号后，再创建并推送匹配标签：

```powershell
git tag -a v0.1.0 -m "Personal-VPN v0.1.0"
git push personal-vpn v0.1.0
$runId = gh run list --repo Dahyunnist/Personal-VPN `
  --workflow release.yml --limit 1 --json databaseId --jq '.[0].databaseId'
gh run watch $runId --repo Dahyunnist/Personal-VPN --exit-status
```

标签必须与 `CMakeLists.txt` 中的 `project(PersonalVPN VERSION 0.1.0 ...)` 完全一致。发布工作流会生成 `.tar.gz`、`.zip` 归档及 SHA-256 文件，也会为归档和校验和文件生成 GitHub 制品证明。创建证明属于发布门禁，不需要签名密钥或用户证书。发布前，工作流会下载六个草稿制品，并将每个摘要和证明与准确的发布工作流及触发标签核对。任何不匹配都会使 release 保持草稿状态。

## 6. 验证下载制品

```powershell
gh release download v0.1.0 --repo Dahyunnist/Personal-VPN --dir release-check
Get-ChildItem release-check
Get-FileHash release-check\personal-vpn-*.zip -Algorithm SHA256
```

将计算出的哈希与对应 `.sha256` 文件比较；解压到新目录，并确认 Windows 归档包含 `personal-vpn-client.exe`、`wintun.dll`、示例配置、Wintun 许可证和文档。Linux 归档应执行等效的 `sha256sum -c` 校验。

安装或分发前验证来源证明。请在有网络连接的可信电脑上安装/更新官方 GitHub CLI，然后执行：

```powershell
gh attestation verify .\release-check\personal-vpn-0.1.0-Windows-AMD64.zip `
  --repo Dahyunnist/Personal-VPN
```

对 Linux `.tar.gz` 和 Windows SBOM `.zip` 执行等效命令。验证结果必须显示证明来自 `Dahyunnist/Personal-VPN`；若证明缺失或无效，应停止分发。GitHub 将证明保存在仓库中，而不是作为普通 release 附件。

## 7. 仓库设置（网页界面）

在默认分支启用规则集：要求 Pull Request、禁止强推和删除、要求全部 CI 任务通过、要求分支保持最新，并在有新提交后撤销过期批准。若仓库方案支持，还应启用密钥扫描、推送保护、Dependabot 警报和私密漏洞报告。

不得为了发布而把失败或未执行任务标记为成功。

## 8. 历史凭据事故

任何曾出现在公开历史中的证书私钥都应视为已泄露。应在签发 CA 中吊销、签发不同的新凭据、发布更新后的 CRL，并确认没有已部署主机仍信任或使用旧密钥。即使当前分支已不包含该文件，这些操作仍然必要。

密钥轮换后可以选择重写历史，但必须另行协调：创建新的镜像克隆、保存可恢复的离线 bundle、列出准确的敏感路径、遵循 GitHub 敏感数据移除流程、本地核对所有 refs，然后只强制更新指定仓库，并要求所有协作者重新克隆。不要在当前工作目录试验历史重写命令；重写历史不能替代吊销凭据。
