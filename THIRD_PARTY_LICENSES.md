<!-- DRAFT / 草稿，需律师审核。This is an engineering inventory, not legal advice. -->

# 第三方许可证清单 / Third-Party Licenses (DRAFT)

字录（SubRec）本身以 **GPL-3.0** 发布（见 [LICENSE](LICENSE)）；项目方计划另行提供**商业（闭源）授权版本**（双重授权）。本文列出本项目链接、随包分发或在仓库中带有的第三方材料及其许可证，并说明与 GPL-3.0 及商业授权的兼容性。

SubRec (字录) is released under **GPL-3.0** ([LICENSE](LICENSE)); the maintainers plan to offer a separate commercial (closed-source) edition (dual licensing). This file lists third-party material the project links against, ships with, or carries in the repository, and how it interacts with GPL-3.0 and a commercial edition.

- **本仓库不含** FFmpeg 二进制/头文件、Qt、OpenCV 或任何预编译 DLL。FFmpeg 和 Qt 由构建者自行提供（见 README “构建”）。不使用 git submodule，也没有包管理清单（`vcpkg.json`、`conan` 等）。
  This repository does **not** contain FFmpeg binaries/headers, Qt, OpenCV or any prebuilt DLL. Builders provide FFmpeg and Qt themselves. There are no git submodules and no package manifests.
- 证据等级 / Evidence levels：**[文件]** = 在文件或 DLL 内嵌字符串中直接看到；**[知识]** = 来自对上游许可证的了解，未在本仓库内验证，**发布前必须逐项核对上游**。

> 结论先行 / TL;DR
> 1. 发布包随附的 FFmpeg 是 SubRec 用自己的脚本从**固定的、未修改的上游源码**构建的最小构建（FFmpeg **n8.0.3**、x264、libvpl、nv-codec-headers、AMF 头文件）：`--enable-gpl --enable-libx264`，**无** `--enable-version3`、**无** `--enable-nonfree`，整体为 **GPL-2.0-or-later**。与 **GPL-3.0 的开源版本兼容**，但**使该二进制不能进入闭源商业版本**。
> 2. 商业版本需要：换成**不含 x264 的 LGPL 构建**（`packaging/ffmpeg` 的 `lgpl` 变体，尚未运行；还要先在 `encoder/ffvencoder.cpp` 里加入 `h264_mf` 作软件编码回退并测试），或为 x264 取得商业授权；同时需处理 H.264/AAC 的专利池问题（非版权问题）。
> 3. Qt 6 各模块均可在 GPLv3 项目中使用；对商业版本需**动态链接 + 遵守 LGPLv3**，或购买 Qt 商业授权。
> 4. CLA **不能**覆盖任何第三方材料，包括 `reference/ffmpeg-fftools/ffmpeg_mux.c`（见 §3）。
> 5. 本项目代码由维护者本人及 AI 工具编写；`icons/*.png` 由维护者本人制作，按 GPL-3.0 发布。仓库中唯一的整文件第三方拷贝是 `reference/ffmpeg-fftools/ffmpeg_mux.c`（FFmpeg，**LGPL-2.1-or-later**，保留原版权头，不参与编译）。拷贝检测是启发式的，结论有局限，见 §6。

---

## 1. FFmpeg（外部依赖，核心）

**如何提供 / How it is provided：** 二进制不随仓库提供。构建时用 `-DFFMPEG_ROOT=<path>` 或环境变量 `SUBREC_FFMPEG_ROOT` 指定含 `include/`、`lib/`、`bin/` 的 FFmpeg **共享（shared）**构建；未提供时 CMake 报错（`cmake/SubRecFFmpeg.cmake`）。CMake 会把 `${FFMPEG_ROOT}/bin/*.dll` 复制到输出目录与安装目录（`CMakeLists.txt`）。官方包使用的 FFmpeg 由 [`packaging/ffmpeg/build-ffmpeg.sh`](packaging/ffmpeg/build-ffmpeg.sh) 从固定提交（[`versions.env`](packaging/ffmpeg/versions.env)）在 Linux 上交叉编译（MinGW-w64）得到，CI 中见 `.github/workflows/windows-build.yml` 的 `ffmpeg` 任务。
The FFmpeg of the official packages is built by `packaging/ffmpeg/build-ffmpeg.sh` from pinned commits (`versions.env`), cross-compiled with MinGW-w64; in CI it is the `ffmpeg` job of `windows-build.yml`. You can also supply any other shared FFmpeg build via `FFMPEG_ROOT` (then its own license applies).

**随包版本 / Version shipped [文件：`packaging/ffmpeg/versions.env`，首次真实构建已通过自检]：** FFmpeg **n8.0.3**，commit `8ae0b34901ba60a802f183ee75a250a9fc3e09a5`，**无补丁**。DLL（合计约 11 MB）：`avutil-60 avcodec-62 avformat-62 avfilter-11 avdevice-62 swscale-9 swresample-6`。其它 FFmpeg 版本需自行验证 API 兼容性。

**对应源码 / Corresponding source：**

| 组件 / Component | 版本 / Version（固定 commit） | 许可证 [知识，发布前核对上游] | 在二进制中的方式 | 源码 / Source |
| --- | --- | --- | --- | --- |
| FFmpeg | n8.0.3，`8ae0b349…`（<https://git.ffmpeg.org/ffmpeg.git>） | LGPL-2.1+；本构建因 `--enable-gpl` 为 **GPL-2.0-or-later** | 共享 DLL | `sources/ffmpeg-n8.0.3.tar.xz`（10.9 MB） |
| x264 | `stable` 分支 commit `b35605ace3ddf7c1a5d67a2eb553f034aef41d55`（`X264_BUILD 165`；<https://code.videolan.org/videolan/x264>） | **GPL-2.0-or-later**（另有 x264 LLC 商业授权） | 静态链接进 `avcodec-62.dll`，仅 8-bit 4:2:0 | `sources/x264-b35605ace3dd.tar.xz`（0.75 MB） |
| libvpl（Intel oneVPL 调度库） | v2.16.0，`778a66d6…` | MIT | 静态调度库；运行时由 Intel 显卡驱动提供 | `sources/libvpl-v2.16.0.tar.xz`（6.1 MB，整个仓库） |
| nv-codec-headers | n12.2.72.0，`c6927834…` | MIT | 仅头文件；NVENC 运行时由 NVIDIA 驱动提供 | `sources/nv-codec-headers-n12.2.72.0.tar.xz`（66 KB） |
| AMD AMF 头文件 | v1.5.3，`57ee0cfb…` | MIT | 仅头文件；AMF 运行时由 AMD 驱动提供 | `sources/amf-headers-v1.5.3.tar.xz`（62 KB，仅头文件与许可证） |
| MinGW-w64 运行库 | 取自构建工具链（版本记录在 `BUILD_CONFIG.txt`） | libgcc/libstdc++：GPL-3.0 + GCC Runtime Library Exception；winpthreads、mingw-w64 CRT：宽松许可 | 静态链接 | 工具链，未随包提供 |

不再包含 x265，也不再使用此前的预编译 FFmpeg 开发快照（约 70 个外部库）。 / x265 and the previous prebuilt FFmpeg snapshot are no longer part of the packages.

- **发布包内随附 / Shipped inside every release package：** `sources/` 目录由 [`packaging/ffmpeg/collect-sources.sh`](packaging/ffmpeg/collect-sources.sh) 从实际编译的检出生成（`git archive` + `xz -9`），共约 **18 MB**（实测）：

  | 包内路径 / Path | 内容 / Content |
  | --- | --- |
  | `sources/ffmpeg-n8.0.3.tar.xz` | FFmpeg commit `8ae0b349` 的源码（10,873,616 B） |
  | `sources/x264-b35605ace3dd.tar.xz` | x264 commit `b35605ace3dd…` 的源码（747,636 B） |
  | `sources/libvpl-v2.16.0.tar.xz`、`sources/nv-codec-headers-n12.2.72.0.tar.xz`、`sources/amf-headers-v1.5.3.tar.xz` | 其余组件源码（6,090,924 B / 65,888 B / 61,608 B） |
  | `sources/subrec-ffmpeg-build-scripts.tar.xz` | `versions.env`、`build-ffmpeg.sh`、`collect-sources.sh`、构建说明与警告说明（14,492 B） |
  | `sources/BUILD_CONFIG.txt` | 固定的 commit、完整 `./configure` 参数、构建主机与工具链版本、每个 DLL 的 SHA-256 |
  | `sources/SHA256SUMS` | `sources/` 内其它文件的 SHA-256（`sha256sum -c SHA256SUMS` 已在首次构建上通过） |

  CI 会复核 `SHA256SUMS`、必需文件、`BUILD_CONFIG.txt`（无 TODO、为 gpl 变体），并核对包内 DLL 的哈希与 `BUILD_CONFIG.txt` 一致；任一失败时，**打 tag（`v*`）的发布构建会直接失败**；非 tag 构建继续，但包内会有 `sources/INCOMPLETE-DO-NOT-DISTRIBUTE.txt`，**不得对外发布**。DLL 合计约 11 MB（avcodec 4.99 MB、avutil 2.48 MB、swscale 1.81 MB、avformat 0.70 MB，其余更小）。
  `sources/` is generated by `collect-sources.sh` from the exact checkouts that were compiled, about 18 MB (measured). CI re-checks the checksums, required files, `BUILD_CONFIG.txt` and the DLL hashes; tagged builds fail on any problem, other builds are marked `INCOMPLETE-DO-NOT-DISTRIBUTE.txt`.

> **`ffmpeg.exe` 不随包 / not shipped：** 构建也产出 `ffmpeg.exe`（0.46 MB）供验收与 CI 冒烟测试，但 SubRec 直接链接 FFmpeg 库，代码中没有任何启动 `ffmpeg.exe` 的调用，因此不放入发布包（少一个 GPL 可执行文件，也少一份攻击面）。若将来要调用，需放入包并在此登记。
> *(English)* The build also produces `ffmpeg.exe` for acceptance and CI smoke tests only; SubRec never starts it, so it is excluded from the package.

> **已知局限 / Known limits：**
> 1. 首次构建在 Debian 13（GCC 14）上完成；CI 使用 Ubuntu 24.04，工具链版本较旧，已记录在每个 `BUILD_CONFIG.txt` 中。重新构建得到功能相同的库，但不保证逐字节相同的 DLL。
> 2. MinGW-w64 运行库来自构建工具链，工具链本身不随包提供（运行库部分的许可证允许）。
> 3. 构建警告的逐条分类见 [`packaging/ffmpeg/BUILD_WARNINGS.md`](packaging/ffmpeg/BUILD_WARNINGS.md)。
> 4. libvpl 与 OpenH264 的固定值是否为提交 id（而非 tag 对象）尚未单独确认；libvpl 的固定值已在真实构建中通过严格校验（`HEAD` 必须等于该值），OpenH264 仅用于尚未运行的可选变体。

**该构建的配置 / Build configuration [文件，`sources/BUILD_CONFIG.txt` 与 `packaging/ffmpeg/README.md`]：**

- `--enable-gpl --enable-libx264` ✅（整体 GPL-2.0-or-later）
- `--enable-version3` ❌ **未使用**；`--enable-nonfree` ❌ **未使用**
- `--disable-everything --disable-autodetect --disable-network`，仅启用 SubRec 用到的编码器（`aac`、`h264_nvenc`、`h264_amf`、`h264_qsv`、`libx264`、测试用的 `pcm_s16le`/`rawvideo`）、解码器、`mp4` 封装器、`dshow`/`lavfi` 输入设备、`file` 协议及少数滤镜（含屏幕采集用的 `ddagrab`/`hwdownload`，均为 LGPL 代码；每项在 `packaging/ffmpeg/README.md` 中附代码出处）
- `--enable-shared --disable-static`（共享库，动态链接；其它依赖静态链接）
- 交叉编译 `x86_64-w64-mingw32`，`--extra-version=subrec-gpl`

**与 GPLv3 的兼容性 / Compatibility with GPLv3：**

- 本项目（GPL-3.0）+ FFmpeg（GPL-2.0-or-later，含 x264 GPL-2.0-or-later）动态链接：**兼容**，合并作品按 GPL-3.0 发布（“or later” 允许升级到 GPLv3）。该 FFmpeg 未启用 nonfree，因此**可合法再分发**。
- **分发义务 / Obligations when distributing binaries：** 若发布的压缩包/安装包**包含** FFmpeg DLL，必须随二进制提供（或书面承诺提供）**对应源码**：FFmpeg（commit `8ae0b349`）、x264 及其它被打入的组件的源码、构建脚本与完整 configure 配置，以及 GPL 许可证文本——这些已全部在 `sources/` 与包内 `licenses/ffmpeg/` 中。**仅发布源码、让用户自行提供 FFmpeg 时，本项目不分发 FFmpeg。** 维护者在发布前仍须确认 `sources/` 完整（CI 已自动检查）。
- 若项目选择 **GPL-3.0-only**：与 GPL-2.0-or-later 的 FFmpeg 组合没有问题。因本构建未使用 `--enable-version3`，FFmpeg 部分本身不要求 GPLv3。

**与“GPLv3 + 商业授权”的兼容性 / Compatibility with dual-licensing：**

- ❌ **该 FFmpeg 构建（含 x264）不能随闭源商业版本分发。** 项目方对 FFmpeg/x264 等不拥有版权，CLA 无法让其可重新授权。
- 商业版本的可行路径 / Paths for a commercial edition：
  1. 改用 **LGPL 构建**的 FFmpeg（`packaging/ffmpeg` 的 `lgpl` 变体：不启用 `--enable-gpl`，仅动态链接，保留用户替换 DLL 的能力，附 LGPL 文本与源码获取方式）；**软件 H.264 编码需另寻来源**（`libx264` 路径在 LGPL 构建里不可用）：依赖 NVENC/AMF/QSV，加上 Windows Media Foundation `h264_mf` 回退（需改 `encoder/ffvencoder.cpp:257-262` 并测试），或 OpenH264 变体（`lgpl-openh264`，另有专利问题）。这两个变体尚未运行过。
  2. 向 x264 LLC 取得 **x264 商业授权**。
  3. 开源版与商业版分别维护不同的 FFmpeg 构建。
- 专利（非版权问题，需专业意见）：H.264 / AAC 等编码格式涉及专利池（如 Via LA 等）。LGPL/GPL 授权**不**授予专利许可。

## 2. Qt 6

**证据 [文件]：**`CMakeLists.txt`：`find_package(Qt6 COMPONENTS Quick Qml Multimedia Widgets Gui)`，`qt_standard_project_setup(REQUIRES 6.5)`；各子模块 `target_link_libraries` 使用 `Qt6::Core/Gui/Widgets/Qml/Quick/Multimedia`；预设与 CI 使用 Qt **6.8.0** MinGW 64 位。QML 导入：`QtQuick`、`QtQuick.Controls`（含 `Material` 样式）、`QtQuick.Layouts`、`QtQuick.Window`、`QtQuick.Effects`、`Qt.labs.platform`、`Qt.labs.folderlistmodel`。源码另使用 `QOpenGLShaderProgram/QOpenGLFunctions`（Qt Gui）、`QQuickFramebufferObject`（Qt Quick）、`QAudioOutput/QAudioDevice`（Qt Multimedia）。

| Qt 模块 / Module | 用途 | 开源许可（Qt 6）[知识] |
| --- | --- | --- |
| Qt Core | 基础类、线程、文件 | LGPL-3.0 / GPL-2.0 / GPL-3.0 / 商业 |
| Qt Gui（含 QOpenGL*） | 窗口、OpenGL | 同上 |
| Qt Widgets | `QApplication`（`main.cpp` 引用） | 同上 |
| Qt Qml | QML 引擎 | 同上 |
| Qt Quick（含 Controls 2、Layouts、Window、Effects、Material 样式） | 界面、`QQuickFramebufferObject` 预览 | 同上 |
| Qt Multimedia | 音频输出/设备 | 同上（Qt 6.5+ 的 Windows 版本使用基于 FFmpeg 的后端，其自带的 FFmpeg 为 **LGPL 构建**，与本项目所用的 GPL FFmpeg 是**两套独立库**；需确认两套 DLL 名称/版本不冲突，并在发布物中分别注明） |
| `Qt.labs.platform` / `Qt.labs.folderlistmodel` | 系统对话框、文件列表 | 同上（实验性 labs 模块，API 可能变动） |
| Qt 内部捆绑的第三方代码（libpng、libjpeg、zlib、freetype、harfbuzz 等） | 随 Qt DLL 分发 | 各自许可，需在发布物中带上 Qt 的第三方声明 |

**未使用**（因此不触发）的 GPL-only/商业-only Qt 附加模块 [知识]：如 Qt Charts、Qt Data Visualization、Qt Virtual Keyboard、Qt Quick 3D Physics 等。如果将来引入，必须重新核对其许可证（这些模块可能仅提供 GPLv3 或商业授权，将直接阻断闭源商业版本）。

**与 GPLv3 的兼容性：** LGPL-3.0 / GPL-3.0 的 Qt 与 GPL-3.0 项目**兼容**。以动态库（官方 `mingw_64` 包为共享库）方式分发，附 GPL-3.0 与 LGPL-3.0 文本及 Qt 声明。**不要选 GPL-2.0-only 作为项目许可证**（Qt 的 GPL-3.0/LGPL-3.0 组合与之冲突）。

**与“GPLv3 + 商业授权”的兼容性：**

- 开源版本：没有问题。
- 商业（闭源）版本两条路：
  1. **使用 LGPL-3.0 的 Qt**：必须**动态链接**；随附 LGPL-3.0 文本与版权声明；允许用户替换/重新链接 Qt 库（LGPLv3 含 anti-tivoization 条款）；**不得**用商业条款禁止逆向工程以调试对 Qt 的修改；显著声明使用了 Qt。需由律师核对分发形式（安装包/便携 zip）。
  2. **购买 Qt 商业授权**：按开发者计费，条款与限制需另行核对。
- Qt 的商业授权**不会**自动解决 FFmpeg/x264 的 GPL 问题，两者是独立的。

## 3. 其它被构建链接、随包分发或在仓库中的组件

| 组件 | 位置 / 证据 | 许可证 | 状态与兼容性 |
| --- | --- | --- | --- |
| MinGW-w64 运行库（`libstdc++-6.dll`、`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`） | `CMakeLists.txt` 在构建后复制并安装（MinGW 构建时） | libstdc++/libgcc：GPL-3.0 **+ GCC Runtime Library Exception**；winpthreads：MIT/ZPL 类宽松许可 [知识] | 因有 Runtime Exception，可随闭源/开源程序分发；GPL 项目无冲突。需附许可证文本。DLL 本身不在仓库中，由构建机提供。 |
| **AMD AMF / NVIDIA NVENC / Intel QSV 运行时** | 通过 FFmpeg 编码器名 `h264_amf/h264_nvenc/h264_qsv` 调用（`encoder/ffvencoder.cpp`） | 驱动由用户机器的 GPU 厂商提供 | 不随本项目分发，无再分发问题。 |
| **screen-capture-recorder / virtual-audio-capturer**（第三方 DirectShow 组件） | `capture/dshow_capture_backend.cpp`、`recorder/ffrecorder_p.h` 以设备名 `video=screen-capture-recorder`、`audio=virtual-audio-capturer` 引用 | 未验证 [需核查上游许可证] | **不在仓库内**；用户自行安装。README 已说明。请勿在未核对许可证前把它们打进安装包。 |
| **`reference/ffmpeg-fftools/ffmpeg_mux.c`** | 892 行；FFmpeg `fftools/ffmpeg_mux.c` 的整文件原样拷贝，**逐字节未改**；不参与编译（任何 CMake 目标都未引用）；说明见同目录 [`NOTICE.md`](reference/ffmpeg-fftools/NOTICE.md)。**与 FFmpeg n8.0.3（commit `8ae0b349`）的 `fftools/ffmpeg_mux.c` 逐字节相同**（已用 `diff` 核对，0 差异）；**未**与其它版本（如最初开发所用的快照 `37064b2d16`）比对，所以不知道它最初拷自哪个版本——相同内容可能也存在于相邻版本 | **LGPL-2.1-or-later**，版权人为 FFmpeg 项目贡献者；文件头 “This file is part of FFmpeg … GNU Lesser General Public License … version 2.1 or later” | **不是本项目作者的代码。** 原版权头保持不变；许可证文本见 [`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt)；仅作设计对照参考；商业版分发包不要包含它；[`CLA.md`](CLA.md) 不覆盖它，不接受对它的修改性贡献。LGPL-2.1-or-later 允许按 GPL 条款再分发，放在 GPLv3 仓库没有文件层面的冲突。它引用的 `ffmpeg.h`、`ffmpeg_mux.h`、`ffmpeg_utils.h`、`sync_queue.h` 是 FFmpeg 内部头，所以也无法单独编译。 |
| 图标 `icons/*.png`（12 个） | `res.qrc` 嵌入可执行文件 | **本项目自有作品**，GPL-3.0（由项目维护者本人制作） | 不是第三方材料；和代码一样适用 GPL-3.0 与 `CLA.md`。 |
| 字体 | QML 指定 `Segoe UI` | Microsoft 系统字体，未随包分发 | 不随包分发则无问题；不要把字体文件加入发行包。 |

## 4. 构建工具与 CI（不随成品分发）

CMake（BSD-3）、Ninja（Apache-2.0）、MinGW-w64 GCC（GPL，有 Runtime Exception）、`tools/*.ps1`（自有脚本）。CI（`.github/workflows/windows-build.yml`）使用的 GitHub Actions（`actions/checkout`、`actions/upload-artifact`、`jurplel/install-qt-action`、`softprops/action-gh-release`）为各自许可，仅在 CI 使用。

## 5. 发布前待办 / Pre-release checklist

1. 决定项目许可证写法：`GPL-3.0-only` 还是 `GPL-3.0-or-later`（“or-later” 需所有版权人同意）。
2. 为发行包中包含的 FFmpeg 二进制准备**对应源码**：已决定**随发布包附带**（`sources/`，由 `packaging/ffmpeg/collect-sources.sh` 生成，见 §1），CI 自动检查完整性。发布前仍需：在 CI 上跑通一次完整构建并检查 `sources/BUILD_CONFIG.txt`、确认 libvpl/OpenH264 固定值为提交 id、人工抽查包内 `licenses/ffmpeg/` 与 `sources/`。
3. 为**商业版本**规划 LGPL 构建的 FFmpeg 与替代的软件编码方案，或购买相应商业授权；核对 H.264/HEVC/AAC 专利风险。
4. 为 Qt 在商业版本中的 LGPL 合规（动态链接、可替换、声明）或商业授权做决定。
5. 核查 `screen-capture-recorder` / `virtual-audio-capturer` 的许可证后再决定是否在文档中推荐或打包。
6. 所有 [知识] 标记项以上游当前许可证为准，逐项核对后再对外声明。
7. 在最终仓库目录上运行免费扫描器（见 §6 末尾），并由维护者确认“是否曾整体粘贴过开源项目的文件/函数”。

## 6. 整块拷贝检测 / Wholesale-copy check

**前提：** 项目代码仅由维护者本人及 AI 工具编写，没有他人代码。检测的是：从 FFmpeg（含 fftools、ffplay）、OpenCV 等项目**整文件或整段粘贴**进来的代码。

| 项目 | 结果 |
| --- | --- |
| `reference/ffmpeg-fftools/ffmpeg_mux.c`（892 行） | **整文件原样拷贝**自 FFmpeg `fftools/ffmpeg_mux.c`，保留原头，见 §3。不参与编译。 |
| 第一方会被编译的源码（`capture decoder demuxer encoder event filter muxer opengl queue recorder render resampler thread timer qml shaderSource tests tools main.cpp`） | 无外来版权/许可证头；与 `ffmpeg_mux.c`、FFmpeg 头文件（4.3 与 2025 版）及 OpenCV 头文件按规范化行（≥45 字符）比对 **0 行重叠**；12 词元滑窗只命中通用写法（`if (ret < 0) return ret;`、`av_packet_unref/av_frame_free` 调用序列）；无 fftools 内部类型/标识符；无超长行或大型数值表。**未发现整块拷贝。** |
| ffplay “参考”注释（`recorder/ffaudiosampler.{h,cpp}`、`qml/AudioVisualizer.qml`） | 实现是环形缓冲 + RMS + dB + QML Canvas，属算法思路参考，**未见 ffplay 代码粘贴**。风险低。ffplay 为 LGPL-2.1+。 |
| 着色器、图标 | `shaderSource/source.{frag,vert}` 是常见 YUV→RGB 写法，无外来头；`icons/*.png` 为维护者本人制作（§3）。 |

**局限：**

1. 启发式文本比对，**只与当时本机已有的文本**（FFmpeg/OpenCV 头文件和 `ffmpeg_mux.c`）对照。未联网；没有 Qt、FFmpeg `.c` 源码、ffplay.c 等上游源码，**无法证明与它们没有相似**。
2. AI 生成的代码可能“长得像”某开源项目（模型复现常见写法），文本比对不一定能发现，版权状态也可能不明；这是“仅本人 + AI”模式下的主要剩余风险，建议请律师意见并核对所用 AI 工具的服务条款。
3. 把某函数“改写成自己的”不一定摆脱版权义务；如维护者记得照搬过某文件的结构或大段逻辑，请按来源许可证处理或重写。

**建议的补充检查（免费）：** 对本仓库运行 [ScanCode Toolkit](https://github.com/aboutcode-org/scancode-toolkit)（许可证与版权检测）和/或 [FOSSology](https://www.fossology.org/)；对仍有疑虑的文件重写或删除。
