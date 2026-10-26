<!-- DRAFT / 草稿 -->

# 贡献指南 / Contributing

感谢你愿意为字录（SubRec）做贡献。 / Thanks for your interest in contributing to SubRec (字录).

## 0. 必须先签署 CLA / A CLA is required for every contribution

**所有贡献（代码、文档、翻译、图标、示例等任何形式，包括很小的修改）都必须先签署 [CLA](CLA.md)，否则我们无法合并。**
未签署 CLA 的 Pull Request 会被标记为阻塞，直到签署完成。

**Every contribution — code, documentation, translations, icons, samples, of any size — requires a signed [CLA](CLA.md) before it can be merged.** Pull requests without a signed CLA are blocked until it is signed.

要点 / Key points:

- CLA 允许项目方以 GPL-3.0 发布你的贡献，也允许项目方在未来的商业版本中以其它许可证（含闭源商业许可）重新授权。你保留自己贡献的著作权。
  The CLA lets the maintainers release your contribution under GPL-3.0 **and** relicense it, including under proprietary commercial terms, in future versions. You keep the copyright in your contribution.
- 你必须是该贡献的著作权人，或已获得雇主/权利人的授权。**不要提交你无权授权的代码**（包括从其它项目复制的代码、未经许可的 AI 输出、受保密协议约束的代码）。
  You must own the contribution or have authority to license it. **Do not submit code you cannot license** (code copied from other projects, AI output whose terms you have not checked, code under NDA).
- 如果代码来自第三方（即使许可证宽松），请在 PR 中明确指出来源与许可证，由维护者决定能否收入；它不会被 CLA 覆盖，需记入 `THIRD_PARTY_LICENSES.md`。
  If you include third-party code (even permissively licensed), state its origin and license in the PR. It is *not* covered by the CLA and must be recorded in `THIRD_PARTY_LICENSES.md`.

签署方式（占位，待项目方确定）/ How to sign (placeholder, to be decided by the maintainers):
在第一个 PR 中按机器人提示回复，或发送签署邮件到 `<CLA-CONTACT>`。 / Reply to the CLA bot on your first PR, or email the signed form to `<CLA-CONTACT>`.

## 1. 提交前 / Before you start

- 较大改动请先开 Issue 讨论。 / Open an issue first for larger changes.
- 安全问题**不要**公开提 Issue，见 [SECURITY.md](SECURITY.md)。 / Do **not** report security issues publicly; see [SECURITY.md](SECURITY.md).
- 参与即表示同意遵守 [行为准则](CODE_OF_CONDUCT.md)。 / By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## 2. 开发约定 / Development conventions

- 语言 C++17，Qt 6.5+，CMake ≥ 3.21，目标平台 Windows。构建方法见 [README](README.md)（FFmpeg 需自行提供，通过 `FFMPEG_ROOT` 指定）。
- `reference/` 目录下是保留原版权头的第三方文件（LGPL-2.1+），不参与构建，不接受对它们的修改性贡献。
- 保持现有的分层：`capture / demuxer / decoder / filter / encoder / muxer / queue / thread / recorder / opengl / qml`。新的控制命令优先进入 `CaptureScheduler`。
- 提交前用 `clang-format`；新增注释只在原因不显然时才写。
- 每个 PR 说明：做了什么、为什么、如何验证（至少说明在哪个 Windows 版本/硬件上构建并录制过）。
- 一个 PR 解决一件事；提交信息写清“为什么”。
- **不要**提交构建产物、二进制库、IDE/AI 工具本地配置、`*.user` 文件、含个人路径或凭据的文件。
- **不要**新增或升级第三方依赖/预编译二进制，除非在 PR 中写明其许可证、来源、构建选项（例如 FFmpeg 是否 `--enable-gpl` / `--enable-nonfree`）。

## 3. 贡献的许可证 / License of contributions

在 CLA 之下，你的贡献以 GPL-3.0 提供，并按 CLA 约定可被项目方重新授权。详见 [CLA.md](CLA.md)。
Under the CLA your contribution is provided under GPL-3.0 and may be relicensed by the maintainers as described in [CLA.md](CLA.md).
