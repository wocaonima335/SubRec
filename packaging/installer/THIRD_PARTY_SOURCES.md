# 安装器第三方材料的来源与核实状态

以下已核实项由协调方于 **2026-11-12** 联网核实并提供原文件；本沙箱依据这些结果更新，
并离线校对文件 SHA256，不声称重新联网核实。此前沙箱 DNS 失败的记录已由这些结果补充。

| 材料 | 来源 | 当前状态（2026-11-12） |
| --- | --- | --- |
| Inno Setup 许可 | <https://jrsoftware.org/files/is/license.txt>；商业许可页 <https://jrsoftware.org/isorder.php> | **已核实**：宽松许可，允许商用及再分发，须保留版权声明及条款，不得歪曲来源，修改版须明确标记；具体以原文为准。商业页写明 “It is not strictly required”：请求商业用户购买但非强制，可在安装器用于生产后购买。 |
| Inno Setup 最新版本 | <https://jrsoftware.org/isdl.php> | **已核实**：官网列出 6.7.4 与 7.1.1（2026-10-28）。D1 选择 Inno Setup 6 固定版本，不迁移 7.x。 |
| Chocolatey `innosetup` | <https://community.chocolatey.org/packages/innosetup>；<https://community.chocolatey.org/packages/innosetup/6.7.1> | **已核实**：Chocolatey 最新为 6.7.1（2026-02-17），CI 固定 6.7.1 并检查 ISCC ProductVersion。该版本与官网最新 6.x 不同；本机尚未运行 choco/ISCC，也未独立审计包安装脚本及二进制下载哈希。 |
| `ChineseSimplified.isl` | <https://jrsoftware.org/files/istrans/>；下文固定提交 URL | **已核实并补齐**：官网列为 official，维护者 Zhenghan Yang，适用 6.5.0+。文件在 issrc 的 is-7_1_1 标签随包，is-6_7_1 / is-6_7_4 的 Files/Languages 无此文件，因此 Inno 6 在仓库附带。原始字节及作者头保持不变；与 6.7.1 的实际编译仍待验证。 |
| `ChineseSimplified.LICENSE.txt` | 同一 issrc 提交的 license.txt；<https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation> | **已补齐**来源归档及 issrc 许可原文；协调方核实翻译维护者另有 MIT 许可仓库。本文件取自 issrc，不据另一仓库链接擅自改写本拷贝的许可文本。 |
| 程序图标 `SubRec.ico` | `icon/subrec-icon-source.png`；`icon/prompt_base.txt` | 程序图标由 AI 图像模型 gpt-image-2.5（经维护者自己的 CLIProxyAPI 调用）于 2026-11-12 生成。AI 生成图像的版权状态可能不明确。 来源和完整提示词见下文。 |

## 翻译与许可的固定来源

- 获取及核实日期：2026-11-12（协调方）。维护者：Zhenghan Yang (Kira)。
- 提交：`6ef32198ef1f7b7b375cd4b6b90896c2a58eb4c2`。
- 翻译：<https://raw.githubusercontent.com/jrsoftware/issrc/6ef32198ef1f7b7b375cd4b6b90896c2a58eb4c2/Files/Languages/ChineseSimplified.isl>。
- 翻译 SHA256：`e0b0b350e2245f3c5e65586dfe43d574f6e7f06f2261149aba284954b3fc9a8d`。
- 许可原文：<https://raw.githubusercontent.com/jrsoftware/issrc/6ef32198ef1f7b7b375cd4b6b90896c2a58eb4c2/license.txt>。
- 许可原文 SHA256：`2e5346868c2a18434489824e11d65c3031620f792fefc415d05f19cd441abf5c`。
- `ChineseSimplified.LICENSE.txt` 加入来源说明，随后完整保留上述 license.txt 原始字节。

Inno 安装器与卸载器含运行代码，属于随包分发材料。`package.py prepare` 仍要求实际编译器的
`License.txt` 存在并原样复制到 `licenses/inno-setup/LICENSE.txt`；不以其它版本的文本替代。
翻译、其许可归档和本来源记录同样放入该目录，供安装包与绿色版共同携带。

## 程序图标

程序图标由 AI 图像模型 gpt-image-2.5（经维护者自己的 CLIProxyAPI 调用）于 2026-11-12 生成。AI 生成图像的版权状态可能不明确。
提示词为：

> Flat minimal app icon for a Windows screen-recording and subtitle app called SubRec (字录). Rounded square tile filled solid red #DC2626, full bleed, no background outside the tile except transparent/white. Bold thick white simplified Chinese character "字" centered. No text other than the character, no gradients, no shadows, no fine details, high contrast, must stay legible at 16x16 pixels.

暂按推荐接入 cand1（待维护者确认）；PNG 原文件、提示词、SHA256 及缩放方式见 [icon/README.md](icon/README.md)。
新界面 `ui/icons/*.svg` 的 iconfont 来源及许可风险未改变，详见仓库第三方清单。

## 方案中其它【需核实】项

| 项目 | 结果 / 后续核查入口 |
| --- | --- |
| Qt 6.8 最低 Windows 版本 | **已核实（协调方，2026-11-12）**：Qt 6.8 官方支持 Windows 10 (1809 or later)，与脚本 `10.0.17763` 一致。<https://doc.qt.io/qt-6.8/supported-platforms.html> |
| `AppMutex` / Restart Manager、托盘退出和录制中升级 | **未验证**。直接使用程序已有的 `Local\SubRec.SingleInstance`，避免依赖隐式前缀；仍须 Windows 实测安装和卸载。<https://jrsoftware.org/ishelp/topic_setup_appmutex.htm> |
| 安装体积、CI 增加 2–4 分钟 | **未实测**，不作体积或耗时承诺。 |
| `gh release view` 是否可见草稿 | **未核实**，实现改用认证的 Releases 列表 API 全分页检查，并在查询失败时终止。<https://docs.github.com/en/rest/releases/releases#list-releases>；CLI <https://cli.github.com/manual/gh_release_create>。尚未在测试仓库运行。 |
| softprops v2 草稿查询、重跑、body/draft 修改行为 | **未核实**；已替换该 action，不依赖其行为。<https://github.com/softprops/action-gh-release> |
| SmartScreen、MOTW、Smart App Control 行为 | **未核实当前政策、未实测**，不承诺安装只弹一次或一定有“仍要运行”。<https://learn.microsoft.com/en-us/windows/security/application-security/application-control/smart-app-control/overview> |
| WiX v6 Open Source Maintenance Fee | **未核实**，未采用 WiX。<https://wixtoolset.org/> |
| 方案 §5.2–§5.3 签名硬件/有效期、EV 信誉、服务价格/资格/地区/第三方二进制条款 | **全部未核实**，不据此作任何实施或采购决定。本次不签名、不申请证书、不接入服务；签名以后再议。 |


## 可选 ASR 组件：固定来源与许可（第一阶段）

以下记录来自已下载资产及原许可的离线核验。CI 接入不在第一阶段；当前发行 payload 可无 `asr/`。

| 组件 | 版本 / 许可 | 分发状态 |
| --- | --- | --- |
| sherpa-onnx | v2.0.2 / Apache-2.0 | 可选 MSVC `/MT` C API 宿主运行库 |
| onnxruntime | 1.28.2 / MIT | 官方 sherpa 完整包内 DLL |
| Silero VAD | v6.2.3 / MIT | 仅预留，S0 A/B 批准后再选实际随包文件 |
| SenseVoice-Small | FunASR Model License 1.1 | **不随包**；ModelManager 下载前要求接受许可 |

固定资产、许可原文及来源（SHA256 均为文件值，另标的 git blob SHA 除外）：

```text
ASR optional bundle / 可选 ASR 组件（M1 phase 1, 2026-11-25）
    SUBREC_ASR_WITH_SHERPA defaults OFF; no MSVC CI job is added in phase 1.
    Only an explicitly assembled asr/ payload ships the C API host and runtime.
    sherpa-onnx v2.0.2 (Apache-2.0), Windows x64 shared-MT Release no-tts FULL:
        Source: https://github.com/k2-fsa/sherpa-onnx/tree/v2.0.2
        Package: https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.2/sherpa-onnx-v2.0.2-win-x64-shared-MT-Release-no-tts.tar.bz2
        Size: 26,896,305 B; SHA256: 6a71d5ef016e544026d6505486d9fa42177ad989238adc6e4bd2eebfb6947416
        Verified file digest matches GitHub API; official checksum.txt omits Windows tar.bz2.
    onnxruntime 1.28.2 (MIT; Copyright (c) Microsoft Corporation):
        Source: https://github.com/microsoft/onnxruntime/tree/v1.28.2
        DLL ProductVersion/FileVersion: 1.28.2. Distributed inside the package above;
        the archive SHA256 covers it; no separate DLL SHA256 is claimed.
    Runtime allowlist: asr/subrec-asr.exe, sherpa-onnx-c-api.dll, onnxruntime.dll,
        onnxruntime_providers_shared.dll. Include providers_shared defensively:
        present in the official package, small, not an onnxruntime load-time import.
        Exclude C++ API DLL, all sherpa CLI tools, headers and import libraries.
    Silero VAD v6.2.3 (MIT; Copyright (c) 2020-present Silero Team):
        Source: https://github.com/snakers4/silero-vad/tree/v6.2.3
        Model: https://raw.githubusercontent.com/snakers4/silero-vad/v6.2.3/src/silero_vad/data/silero_vad.onnx
        Size: 2,327,524 B; file SHA256: 1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3
        Git blob SHA: 80c5592ef1f4c9ede3e357bbd02eb863358a6a9d (NOT file SHA256).
        RESERVED ONLY: the file that actually ships is not selected until S0 A/B
        is approved (decision 10). Default assembly copies no VAD. Explicit
        --vad v6.2.3 verifies SHA256 before copying as asr/silero_vad.onnx.
        Do not track v6.3/master. This phase does not finalize the installer VAD.
    sherpa v4-series Silero (MIT) is S0 A/B ONLY, NOT the installer default:
        https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
        Size: 643,854 B; SHA256: 9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6
    SenseVoice-Small int8 is NOT shipped. ModelManager downloads it only after
        FunASR Model License 1.1 acceptance:
        https://github.com/modelscope/FunASR/blob/main/MODEL_LICENSE
    Original license texts (licenses/asr/; unchanged, plus a SubRec bundle NOTICE):
        sherpa-onnx.LICENSE (Apache-2.0, 2.0.2): https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v2.0.2/LICENSE
        11,358 B; SHA256: cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30
        onnxruntime.LICENSE (MIT, 1.28.2): https://cdn.jsdelivr.net/gh/microsoft/onnxruntime@v1.28.2/LICENSE
        1,073 B; SHA256: 2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c
        silero-vad.LICENSE (MIT, 6.2.3): https://raw.githubusercontent.com/snakers4/silero-vad/v6.2.3/LICENSE
        1,075 B; SHA256: 2e63e9a38b6e8fc0c7bc37ce174caca1862870856c6daf5697cfb785e925520b
    Provenance: previously fetched originals in pkgC-verify-win/licenses-src/;
        copied byte-for-byte into packaging/licenses/asr/ after offline verification.
        For onnxruntime, github.com/microsoft/onnxruntime and raw.githubusercontent.com
        returned 404 from the fetching network; jsDelivr served the v1.28.2 text.
        This phase does not claim new online verification. The assemble script copies
        all three LICENSE files and NOTICE.txt, including Silero's license without VAD.
    清单与实现：packaging/asr/manifest.json；asr/ 缺席时现有打包流程保持兼容。
    VAD 仅预留 v6.2.3，实际随包文件等 S0 A/B 批准；SenseVoice 不随包。
```
