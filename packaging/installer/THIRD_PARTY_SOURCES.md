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
