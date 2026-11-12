# Windows 安装器维护说明

按 installer-plan §7.3 推荐值实现；程序图标按维护者最新决定采用 AI 生成的 cand1，签名以后再议。
本次未提交、推送、触发 CI、操作 tag/Release 或访问 Windows 主机。

## 当前输入与验证边界

`ChineseSimplified.isl` 及其许可归档已由协调方从固定上游提交提供并补入仓库，SHA256 已核对。
协调方于 2026-11-12 核实 Inno 许可、官网版本、Chocolatey 版本及 Qt 最低 Windows 要求；
CI 固定 **Inno Setup 6.7.1**（Chocolatey 当前版本，官网最新 6.x 为 6.7.4）。来源和结果见
[THIRD_PARTY_SOURCES.md](THIRD_PARTY_SOURCES.md)。Windows 编译/安装尚未验证。

依赖 Windows 上的 Inno Setup 6.7.1、Python 3.11+（CI 固定 3.13）、PowerShell 7；
准备好的 `dist/SubRec` 来自原工作流，必须含 appSubRec.exe、DLL、QML、许可证和完整源码。

## 模块与数据流

1. `package.py version` 从 tag `vX.Y.Z-prerelease` 生成显示版本、四段资源版本和产物名。
   非 tag 使用 `0.0.0-ci.<sha7>`；CMake 和 ISCC 使用同一值。本地 CMake 默认 `0.2.0-beta`。
   含引号、路径符号、超出 65535 的数字等无效版本均拒绝。D10 未解决前无后缀正式 tag 拒绝。
2. `package.py prepare` 检查必备文件，把 Inno 原始许可和翻译材料加入共同 payload，
   再生成 `[InstallDelete]`。`INCOMPLETE-DO-NOT-DISTRIBUTE.txt` 导致非 tag 产物名加
   `-INCOMPLETE`，tag 直接失败。安装器固定安装全部 `sources/`；不会安装 DirectShow 组件。
3. ISCC 递归安装同一个 `dist/SubRec`，额外附 `NOTICE.txt`、`SubRec.ico` 和卸载器。
   仅 per-user，无 UAC；默认目录 `%LOCALAPPDATA%\Programs\SubRec`，允许改路径。
   中英双语信息页不要求同意 GPL，桌面快捷方式、完成后启动默认勾选，查看说明默认不勾选。
4. `smoke-test.ps1` 在临时的中文、带空格路径中静默安装，逐文件核对 hash、注册表卸载入口、
   快捷方式；重复安装检查旧 DLL/QML 删除和日志保留；卸载后检查目录、入口和快捷方式消失。
   不启动录制程序。日志作为独立 artifact 保留 14 天，失败时也上传。
5. `package.py bundle` 输出安装器、portable zip、对应源码 zip 及统一 `SHA256SUMS.txt`。
   tag artifact 保留 90 天，非 tag 14 天。源码 zip 与安装/绿色包的 `sources/` 完全同源。
6. 独立 `release` job 才有 `contents: write`。校验精确四文件集合、hash、源码缺失标记；
   全分页检查已有草稿和正式 Release，已存在或 API 失败就停止；只创建新预发布草稿，
   不追加、不覆盖、不删除。相同 tag 的 CI Release 作业串行运行，维护者勿同时手工创建。

## 图标与 Windows 资源

程序图标由 AI 图像模型 gpt-image-2.5（经维护者自己的 CLIProxyAPI 调用）于 2026-11-12 生成。AI 生成图像的版权状态可能不明确。
提示词为：

> Flat minimal app icon for a Windows screen-recording and subtitle app called SubRec (字录). Rounded square tile filled solid red #DC2626, full bleed, no background outside the tile except transparent/white. Bold thick white simplified Chinese character "字" centered. No text other than the character, no gradients, no shadows, no fine details, high contrast, must stay legible at 16x16 pixels.

源图为暂时接入的 cand1（待维护者确认）：红色圆角方块与白色粗体“字”，与现有 BrandLogo 风格统一。
原 PNG、提示词文件及 SHA256 记录见 [icon/README.md](icon/README.md)。
`generate-icon.py` 仅依赖 Pillow，以 Lanczos 从 PNG 分别缩放到 256/48/32/16；
16/32 像素略加锐化，保留透明度。执行 `python packaging/installer/generate-icon.py`；
本次在装有 Pillow 的本地 Python 虚拟环境中运行。

`SubRec.rc.in` 经 CMake 生成 `.rc`，仅在 Windows 编入 appSubRec，使用 `IDI_ICON1` 名称
供 Qt 读取默认窗口图标，并包含 VERSIONINFO；实际 Windows 显示仍待验收。
关于页沿用 `SUBREC_VERSION` 编译定义。安装器、快捷方式和卸载入口使用同一 ICO
（卸载入口读取 exe 资源）；没有修改 main.cpp、qml/、ui/、shell/ 或托盘状态图标。
新界面 iconfont 图标本身的许可风险仍然存在，因此 D10 预发布限制保持不变。

## 升级与卸载边界

AppId `{04DE0717-B7D3-492D-84C6-73C1E49B54F8}` 永不更改。互斥量明确使用
`Local\SubRec.SingleInstance`；Restart Manager 提示退出、不自动重启，升级前必须停止录制。
beta 允许降级。设置 `HKCU\Software\SubRec\SubRec` 与绿色版共用，卸载不删除；录像不作为
安装器管理的文件。卸载额外删除程序目录的 `SubRec.log`，不递归删除整个安装目录。

清理列表合并当前 payload 一级目录与 `cleanup-directories.txt`，仅清除这些程序目录和
根目录 `*.dll`，升级保留日志。**以后每次出现新一级目录，必须加入历史清单并永久保留**；
tag 构建会检查这一点。不能把用户录像目录加入清单。按方案，程序目录专用于应用文件；
请勿将录像/个人文件保存在被清理的 `qml`、`sources`、插件等目录，也不要把 `/DIR` 指向
已有共享软件目录。安装失败中途清理旧插件不具备完整回滚保证，需重新运行安装器恢复。

## 本地打包示例（Windows 构建环境）

在仓库根目录的 PowerShell 7，先按原 CI 完成 `dist/SubRec`，再运行：

```powershell
# 与 appSubRec 构建时传入的 -DSUBREC_VERSION 保持一致。
$env:GITHUB_REF_TYPE = 'tag'
$env:GITHUB_REF_NAME = 'v0.2.1-beta'
$env:GITHUB_SHA = (git rev-parse HEAD)
python packaging/installer/package.py version
$compiler = "${env:ProgramFiles(x86)}\Inno Setup 6"
python packaging/installer/package.py prepare --compiler "$compiler" --stem SubRec-v0.2.1-beta
# RepoURL 必须使用项目实际地址，不能把占位符写进成品。
& "$compiler\ISCC.exe" /DAppVersion=0.2.1-beta /DAppVersionNumeric=0.2.1.0 `
  "/DSourceDir=$((Resolve-Path dist/SubRec).Path)" /DOutputName=SubRec-v0.2.1-beta-win64-setup `
  /DRepoURL=https://github.com/ACTUAL_OWNER/SubRec packaging/installer/SubRec.iss
./packaging/installer/smoke-test.ps1 -Setup out/SubRec-v0.2.1-beta-win64-setup.exe
python packaging/installer/package.py bundle --stem SubRec-v0.2.1-beta
python packaging/installer/package.py verify --stem SubRec-v0.2.1-beta
```

本示例不操作 Release。发布流程与 Windows 人工验收见
[WINDOWS_INSTALLER_RELEASE.md](../../docs/WINDOWS_INSTALLER_RELEASE.md)。
