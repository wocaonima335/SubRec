# Windows 安装器实施回报（2026-11-12）

基础分支 `installer`，基础提交 `b23052b`。完整阅读外部 installer-plan.md 的 590 行，
按 D1–D16 推荐值实施；程序图标按续接决定改为 AI 生成的 cand1，签名完全排除。本次不提交。

## 模块 → 文件（供协调方拆分提交）

| 模块 | 文件 |
| --- | --- |
| W1 安装器与许可信息页 | `packaging/installer/SubRec.iss`、`NOTICE.txt`、`cleanup-directories.txt`、`.gitignore`、`README.md`、`THIRD_PARTY_SOURCES.md`、`ChineseSimplified.isl`、`ChineseSimplified.LICENSE.txt`（本行未写完整路径的文件均在 `packaging/installer/`） |
| W2 图标 | `packaging/installer/SubRec.ico`、`packaging/installer/generate-icon.py`、`packaging/installer/icon/subrec-icon-source.png`、`packaging/installer/icon/prompt_base.txt`、`packaging/installer/icon/README.md` |
| W3 打包与冒烟 | `packaging/installer/package.py`、`packaging/installer/smoke-test.ps1`、`packaging/installer/test_package.py`；`.github/workflows/windows-build.yml` 的 build 作业改动 |
| W4 独立草稿发布 | `.github/workflows/windows-build.yml` 的 release 作业、权限和相应说明 |
| W5 文档与来源登记 | `README.md`、`CONTRIBUTING.md`、`THIRD_PARTY_LICENSES.md`、`packaging/licenses/SOURCES.txt`、`docs/WINDOWS_INSTALLER_RELEASE.md`、`docs/verification/windows-installer.md` |
| W7 版本对齐、exe 资源 | `CMakeLists.txt`、`cmake/SubRecVersion.cmake`、`packaging/installer/SubRec.rc.in` |

跨模块：工作流文件同时覆盖 W3/W4，版本参数传递也依赖 W7；建议按 hunk 拆分或合为一次
CI 提交。W1 的说明与 NOTICE、W5 的第三方清单同时记录 W2 图标、W7 资源用途；W7 引用 W2
的 ICO。按 W1 → W2 → W7 → W3/W4 → W5 提交可保持依赖可读，翻译及许可已补齐，发布前仍需 Windows 编译与验收。

## 图标

程序图标由 AI 图像模型 gpt-image-2.5（经维护者自己的 CLIProxyAPI 调用）于 2026-11-12 生成。AI 生成图像的版权状态可能不明确。
提示词为：

> Flat minimal app icon for a Windows screen-recording and subtitle app called SubRec (字录). Rounded square tile filled solid red #DC2626, full bleed, no background outside the tile except transparent/white. Bold thick white simplified Chinese character "字" centered. No text other than the character, no gradients, no shadows, no fine details, high contrast, must stay legible at 16x16 pixels.

暂按推荐接入 cand1（待维护者确认）：红色圆角方块与白色粗体“字”，提示词指定红色 #DC2626，呼应 BrandLogo。
源 PNG 及原提示词已存入 `packaging/installer/icon/`；Pillow Lanczos 从原图独立生成四尺寸，
16/32 像素略加锐化并保留 alpha。exe 的资源名称为 `IDI_ICON1`，安装器、快捷方式及卸载
入口引用同一 ICO（卸载入口从 exe 读取）。没有修改托盘状态图标或窗口代码。

- 输入 SHA256：`379598a9fa591801d582cb9e85338ed20e902ccd31713feb97ff086ee2a3b550`
- ICO SHA256：`c8c2101f81550505b279b2fcb2689e3ed57bd134fc7a8ab157efbb78b1f30c5b`
- 新界面 `ui/icons/*.svg` 的 iconfont 说明与风险保持不变，仍限制预发布。

## 第三方核实

协调方于 2026-11-12 联网核实并提供材料，本沙箱离线校对原文件，不声称自行重新联网：

- Inno 许可允许商用与再分发（保留声明、遵守原文条件）；商业页购买请求非强制。
- 官网版本为 6.7.4 与 7.1.1；Chocolatey 为 6.7.1，按 D1 固定 6.7.1。
- 简体中文为 official，维护者 Zhenghan Yang，6.5.0+；issrc 7.x 随包、6.7.x 未随包。
  已补入固定提交 `6ef32198ef1f7b7b375cd4b6b90896c2a58eb4c2` 的原翻译，SHA256 为
  `e0b0b350e2245f3c5e65586dfe43d574f6e7f06f2261149aba284954b3fc9a8d`，许可归档附同提交
  license.txt 的完整原字节、来源、提交、hash 及维护者 MIT 仓库链接。
- Qt 6.8 官方最低 Windows 10 1809，与脚本一致。

官方链接和全部已核实/待核实状态见
[THIRD_PARTY_SOURCES.md](../../packaging/installer/THIRD_PARTY_SOURCES.md)。

## 已完成的本地验证

| 检查 | 结果 |
| --- | --- |
| Pillow 读取 ICO | 16、32、48、256 均存在、均能解码为对应尺寸 RGBA，透明度范围 0–255；生成脚本检查来源 hash 和各帧字节一致性；目视检查原尺寸及放大图，16×16 的“字”可辨认 |
| 上游材料 | PNG、提示词、翻译与提供的源文件逐字节相同；翻译 SHA256 匹配指定值；许可归档完整包含 issrc license.txt 原字节 |
| 实际翻译打包 | 用真实翻译、许可归档及隔离的测试 payload/编译器目录运行 prepare 成功，安装材料复制后字节不变；不代表实际 ISCC 编译通过 |
| YAML | PyYAML 解析成功（ffmpeg/build/release 三作业）；yamllint relaxed、禁用长行/truthy 规则，通过 |
| 打包逻辑测试 | `python packaging/installer/test_package.py -v`，11 项通过：版本/正式版拦截、缺失翻译/许可、历史目录清理、危险路径、INCOMPLETE 命名/拒发、压缩包布局、原许可字节保留、附件完整性和篡改 |
| CMake 版本模块 | 独立 `cmake -P` 验证 4 个合法版本、4 个非法版本，全部符合预期 |
| RC 模板 | `configure_file` 成功；`0.2.1-beta.2` 显示版本与 `0,2,1,0` 数值资源正确，`IDI_ICON1` 及 exe/SetupIconFile/快捷方式/卸载入口同源引用检查通过；**未用资源编译器编译** |
| Release shell | 从 YAML 提取两段 bash，`bash -n` 通过；**未调用 gh、未测试真实 Release API** |
| 权限与编码 | workflow 默认 contents:read，build 无写权限，release 才有 contents:write；ISS/NOTICE 均 UTF-8 BOM |
| 差异约束 | `git diff --check` 通过；`main.cpp`、`qml/`、`ui/`、`shell/` 无修改；已有代码注释与 FROZEN 块保留 |

## 未验证或未做

- 完整 CMake 配置：默认生成器先因缺 make 失败；显式使用已安装 Ninja 后停在
  `project()` 找不到 C++ 编译器。本机也没有 Qt、FFmpeg、MinGW，尚未走到其依赖检查。
  没有用假编译器/假 Qt 声称完整配置成功。
- ISS 语法和 `.rc` 编译：无 ISCC、Wine、MinGW，**未验证**；中文翻译已补齐，仍需实编译验证与 6.7.1 的兼容性。
  未尝试当前不可用的 apt 安装；缺少 PowerShell，PS1 也未执行/解析。
- Windows 静默安装/升级/卸载测试已编写但**未执行**；真实运行互斥、录制升级、UI 语言、
  SmartScreen、中文用户名、设置和录像保留由维护者按验收清单测试。
- 无 Windows 安装包构建产物；没有安装器体积或耗时实测。协调方提供的联网核实项已登记，本沙箱没有重新联网。
- 没有提交、推送、CI 触发、tag、Release 读取/写入、证书申请或签名服务接入。

## 对方案的明确调整

- 按维护者最新决定采用 gpt-image-2.5 经 CLIProxyAPI 生成的 cand1；完全不签名，签名以后再议。
- W7 同时加入 `.rc`，仅修改 CMake 并新增资源模板，未改任何窗口或 UI 产品代码。
- 使用精确的 `Local\SubRec.SingleInstance`，消除方案中隐式前缀假设；仍需实机验证。
- 清理列表保留历史目录并合并当前部署目录，新目录在 tag 构建时要求补入历史清单，避免
  仅按新版一级目录生成时遗漏已停用插件。未采用卸载旧版再安装的方案。
- 版本计算提前到 CMake 前；打包整理后才确定 INCOMPLETE 后缀；正式版 tag 在图标许可
  未解决前拒绝。原方案只描述风险，这里把预发布限制落实为失败检查。
- 使用认证的分页 Releases 列表 API 检查草稿，查询错误不当作“不存在”；精确验证四个附件，
  不用宽泛 glob 上传。未验证真实 API 行为，也未清理任何现有草稿。
- 中文翻译及许可已从协调方提供的固定上游文件补齐，缺失检查继续保留；Inno 固定 6.7.1。

推送前应由维护者批准/完成的具体事项见
[Windows 安装器发布与验收清单](../WINDOWS_INSTALLER_RELEASE.md)：审阅已补齐的第三方输入、
NOTICE/许可与源码材料及 AI 图标来源、永久 AppId/清理清单、CI 与草稿创建权限；随后安排 Windows
编译与验收。已接受的图标风险及不签名决策无需重复征询。


## 本次续接的改动范围

只替换程序图标和其生成/来源记录、将 RC 图标名称设为 `IDI_ICON1`、补齐翻译与许可、
将 CI 固定值和版本检查改为 6.7.1，并同步上述核实结果和许可文字。CMake、package.py、
smoke-test.ps1、test_package.py 与产品代码本轮未修改。旧 SVG 转换注释及旧版本候选注释
按约束保留并标记为已被新决定替代的历史说明，没有保留旧图标执行路径。
