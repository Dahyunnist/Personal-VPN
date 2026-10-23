# 发布工程

Personal-VPN 使用 CMake/CPack 生成 Linux 服务端归档和 Windows 桌面客户端归档。只有 CI 中的核心测试、Linux 集成测试、ASan/UBSan、协议 fuzz、benchmark 冒烟测试和 Windows 打包构建全部通过，才接受发布。单独的 CodeQL 工作流会分析 C++ 改动，并按周运行。

## 供应链控制

- Windows 构建通过完整提交 ID `f5befd2d29e66809cd1110a152e375a7f1981f06` 获取 Dear ImGui v1.91.9b。
- Wintun 固定为 0.14.1，归档 SHA-256 必须为 `07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51`。
- Windows Boost/OpenSSL 依赖使用带签名的 vcpkg 2026.07.29 registry baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`；CMake 将 manifest 安装根目录固定到各构建目录，并在 Windows 包旁发布生成的依赖 SPDX 记录。
- 发布工作流只接受与 CMake 项目版本一致的标签。
- Linux 和 Windows 归档都会附带 SHA-256 校验和文件。
- 工作流会在上传前为 Linux 归档/校验和及 Windows ZIP 制品生成 GitHub 无密钥制品证明。`id-token`、`attestations` 和 `artifact-metadata` 权限只授予打包任务，不保存长期签名密钥。公开 CI 的执行结果仍需仓库所有者核验。消费者按[经过认证的 GitHub 操作指南](../operations/authenticated-github-sop.md)使用 `gh attestation verify` 验证下载文件。
- 发布前，工作流会下载全部六个草稿制品，使用可移植 SHA-256 sidecar 验证每个归档，并验证证明对应的准确发布工作流、触发标签和 GitHub 托管运行器身份。任何文件缺失、多余、被改动或无证明，都会使发布保持草稿状态。
- CI 与发布任务共用 `cmake/verify-windows-package.cmake` 检查 Windows 可执行文件、Wintun 运行时、示例配置、许可证和随包文档，并在上传前拒绝路径类似凭据的归档内容。
- `scripts/package-vcpkg-sbom.ps1` 保留每个 SPDX 文件相对 vcpkg 安装树的路径，并验证归档中没有缺失或重复条目。
- 仓库策略禁止将私钥、证书包、本地配置文件、DLL、可执行文件、日志和构建目录提交到版本控制。

Wintun 下载默认关闭，必须显式设置 `PERSONAL_VPN_FETCH_WINTUN=ON`，避免离线开发构建意外访问网络。仍支持指定本地 `PERSONAL_VPN_WINTUN_DLL`。

## 版本管理

使用语义化版本。以下情况分别递增：

- 主版本：不兼容的线路协议或配置变更；
- 次版本：向后兼容的功能；
- 修订版本：兼容的修复、打包和文档变更。

线路协议版本与产品版本刻意分开。产品发布可以继续使用协议 v1。

## 发布检查清单

1. 确认工作区干净，且发布提交上的 CI 全部通过。
2. 更新 `project(PersonalVPN VERSION ...)`、变更日志/发布说明和兼容性文档。
3. 确认没有开发 PKI 或包含秘密/个人数据的数据包抓取。
4. 创建并推送匹配的签名或附注式 `vX.Y.Z` 标签。
5. 由 `.github/workflows/release.yml` 创建草稿、构建、校验并证明两个平台的发布包及 Windows 依赖 SBOM。只有所有打包任务通过，且下载制品的校验和与来源证明验证通过后，才可发布。失败运行会保留私有草稿以便修复；已发布的 release 不允许被重跑修改。
6. 在干净主机下载制品，验证校验和与证明、检查归档内容，并运行服务端 `--help` 及使用临时 PKI 的客户端冒烟连接。
7. 公告前记录发布测试结果和已知限制。

发布制品是工程产物，不代表该教育性原型已通过外部安全审计或适合生产使用。
