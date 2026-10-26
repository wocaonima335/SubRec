<!-- DRAFT / 草稿：发布前填写 <SECURITY-CONTACT>，并确认是否启用 GitHub 私有漏洞报告 -->

# 安全策略 / Security Policy

## 支持的版本 / Supported versions

项目处于早期原型阶段，尚无稳定发布。目前仅 `main` 分支的最新代码会获得安全修复。
The project is an early prototype with no stable releases. Only the latest code on `main` receives security fixes.

| 版本 / Version | 是否支持 / Supported |
| --- | --- |
| `main`（最新） / latest | 是 / yes |
| 其它 / others | 否 / no |

## 如何报告漏洞 / Reporting a vulnerability

**请不要在公开 Issue、PR 或讨论区披露漏洞细节。 / Please do not disclose vulnerability details in public issues, PRs or discussions.**

请使用以下任一私密渠道 / Use one of these private channels:

1. GitHub 仓库的 “Security → Report a vulnerability”（私有漏洞报告，需项目方启用）。 / GitHub private vulnerability reporting (the maintainers must enable it).
2. 邮件：`<SECURITY-CONTACT>`。 / Email: `<SECURITY-CONTACT>`.

报告中请尽量包含 / Please include:

- 受影响的版本或提交哈希、Windows 版本、Qt 与 FFmpeg 版本。
- 复现步骤或概念验证、预期与实际行为、影响评估。
- 请**不要**附带真实的个人录屏内容或凭据。 / Do not attach real personal recordings or credentials.

## 响应目标 / What to expect

以下是尽力而为的目标，不构成服务承诺 / Best-effort targets, not a service-level commitment:

- 3 个工作日内确认收到。 / Acknowledge within 3 business days.
- 10 个工作日内给出初步评估。 / Initial assessment within 10 business days.
- 修复就绪后与报告者协商披露时间，默认不超过 90 天。 / Coordinated disclosure, 90 days by default.

## 范围 / Scope

范围内 / In scope:本仓库自有代码，例如：文件路径与输出目录处理、日志文件、FFmpeg 输入 URL/设备字符串的构造与解析（`demuxer/`、`capture/`）、QML 与 C++ 的桥接、录制文件写入。
In scope: first-party code in this repository, e.g. output path handling, log files, construction/parsing of FFmpeg input URLs and device strings, QML↔C++ bridging, recording file output.

范围外 / Out of scope:
- FFmpeg、Qt、OpenCV 等第三方库自身的漏洞——请报告给上游；但如果是我们**捆绑的版本过旧**导致的已知 CVE，欢迎报告。
  Vulnerabilities in third-party libraries themselves — report upstream. A known CVE caused by an outdated **bundled version** is welcome, though.
- 需要本地管理员权限或已完全控制本机才能利用的问题。
- 用户自行安装的第三方 DirectShow 组件（`screen-capture-recorder`、`virtual-audio-capturer`）的问题。

## 隐私提示 / Privacy note

本应用采集屏幕、摄像头与音频。如果你发现录制内容可能被意外写到非预期位置、被网络传输，或日志中包含敏感信息，也请按安全问题报告。
This application captures screen, camera and audio. Please report as a security issue anything that may write recordings to unexpected locations, send them over the network, or leak sensitive data into logs.
