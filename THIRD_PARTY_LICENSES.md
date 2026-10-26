<!-- DRAFT / 草稿，需律师审核。This is an engineering inventory, not legal advice. -->

# 第三方许可证清单 / Third-Party Licenses (DRAFT)

字录（SubRec）本身以 **GPL-3.0** 发布（见 [LICENSE](LICENSE)）；项目方计划另行提供**商业（闭源）授权版本**（双重授权）。本文列出本项目链接、随包分发或在仓库中带有的第三方材料及其许可证，并说明与 GPL-3.0 及商业授权的兼容性。

SubRec (字录) is released under **GPL-3.0** ([LICENSE](LICENSE)); the maintainers plan to offer a separate commercial (closed-source) edition (dual licensing). This file lists third-party material the project links against, ships with, or carries in the repository, and how it interacts with GPL-3.0 and a commercial edition.

- **本仓库不含** FFmpeg 二进制/头文件、Qt、OpenCV 或任何预编译 DLL。FFmpeg 和 Qt 由构建者自行提供（见 README “构建”）。不使用 git submodule，也没有包管理清单（`vcpkg.json`、`conan` 等）。
  This repository does **not** contain FFmpeg binaries/headers, Qt, OpenCV or any prebuilt DLL. Builders provide FFmpeg and Qt themselves. There are no git submodules and no package manifests.
- 证据等级 / Evidence levels：**[文件]** = 在文件或 DLL 内嵌字符串中直接看到；**[知识]** = 来自对上游许可证的了解，未在本仓库内验证，**发布前必须逐项核对上游**。

> 结论先行 / TL;DR
> 1. 开发时使用的 FFmpeg 是 **GPL v3 构建**（`--enable-gpl --enable-version3`，含 libx264/libx265 等，未启用 nonfree）。与 **GPL-3.0 的开源版本兼容**，但**使该二进制不能进入闭源商业版本**。
> 2. 商业版本需要：换成 **LGPL 构建的 FFmpeg**（不启用 `--enable-gpl`，放弃 libx264/libx265 等），或为 GPL 组件取得商业授权；同时需处理 H.264/HEVC/AAC 的专利池问题（非版权问题）。
> 3. Qt 6 各模块均可在 GPLv3 项目中使用；对商业版本需**动态链接 + 遵守 LGPLv3**，或购买 Qt 商业授权。
> 4. CLA **不能**覆盖任何第三方材料，包括 `reference/ffmpeg-fftools/ffmpeg_mux.c`（见 §3）。
> 5. 本项目代码由维护者本人及 AI 工具编写；`icons/*.png` 由维护者本人制作，按 GPL-3.0 发布。仓库中唯一的整文件第三方拷贝是 `reference/ffmpeg-fftools/ffmpeg_mux.c`（FFmpeg，**LGPL-2.1-or-later**，保留原版权头，不参与编译）。拷贝检测是启发式的，结论有局限，见 §6。

---

## 1. FFmpeg（外部依赖，核心）

**如何提供 / How it is provided：** 不随仓库提供。构建时用 `-DFFMPEG_ROOT=<path>` 或环境变量 `SUBREC_FFMPEG_ROOT` 指定含 `include/`、`lib/`、`bin/` 的 FFmpeg **共享（shared）**构建；未提供时 CMake 报错（`cmake/SubRecFFmpeg.cmake`）。CMake 会把 `${FFMPEG_ROOT}/bin/*.dll` 复制到输出目录与安装目录（`CMakeLists.txt`）。

**开发所用版本 / Version used in development [文件]：** `FFmpeg version N-119453-g37064b2d16-20250509`（git master 快照，2025-05-09，**不是正式发布版**），commit `37064b2d16`；libavutil 60.2.100、libavcodec 62.3。DLL：`avutil-60 avcodec-62 avformat-62 avfilter-11 avdevice-62 swscale-9 swresample-6`。其它 FFmpeg 版本需自行验证 API 兼容性。

**对应源码获取位置 / Where to get the corresponding source：**

| 组件 / Component | 标识 / Identification | 源码 / Source |
| --- | --- | --- |
| FFmpeg | commit `37064b2d16` | <https://git.ffmpeg.org/ffmpeg.git>；GitHub 镜像 <https://github.com/FFmpeg/FFmpeg/commit/37064b2d16> |
| x265 | `4.1+151-f8be4634`（API build 215，取自 DLL 内嵌版本字符串），commit `f8be4634` | <https://bitbucket.org/multicoreware/x265_git>（CI 使用的归档 URL **未验证**） |
| x264 | 静态链接；版本信息仅有 `core 165`（SEI：`x264 - core 165 - H.264/MPEG-4 AVC codec - Copyleft 2003-2025`），无 `r` 版本号、无 commit | <https://code.videolan.org/videolan/x264> 的 `stable` 分支中 `x264.h` 为 `X264_BUILD 165` 的提交；**确切 revision 未知（TODO）** |
| 其它被启用的外部库 | 见 configure 命令行（`packaging/BUILD_CONFIG.txt`） | **TODO：版本与源码尚未收集** |

完整 `./configure` 命令行见 [`packaging/BUILD_CONFIG.txt`](packaging/BUILD_CONFIG.txt)，取自 `avcodec-62.dll` 内嵌的配置字符串（未带入任何本机路径）。该构建**看起来类似** BtbN `FFmpeg-Builds` 的 win64 GPL shared 构建——**仅为推断，未确认**。同样的信息见 [`reference/ffmpeg-fftools/NOTICE.md`](reference/ffmpeg-fftools/NOTICE.md)。

> ⚠ **源码完整性警示 / Source-completeness warnings**
> 1. **x264 可能不是逐字节对应的源码**：DLL 里只有 `core 165`，包内的 x264 是 `stable` 分支中 `X264_BUILD 165` 的“最接近”版本。若与实际编译进去的代码不完全一致，则不能完全满足 GPL 对“对应源码”的要求（合规风险）。理想做法是**用已知源码重新构建 FFmpeg**（记录 FFmpeg/x264/x265 的 commit 与构建脚本），并替换 DLL。
> 2. **其它静态链接库**：configure 命令行启用了大量其它外部库，很可能静态链接进 DLL；它们的源码尚未随包提供（TODO）。用最小配置重新构建可大幅缓解。
> 3. 以上未解决前，打 tag（`v*`）的发布构建会失败；其它构建会被标记为 `INCOMPLETE-DO-NOT-DISTRIBUTE.txt`，不得对外发布。
> *(English)* x264 is identified only as `core 165`, so the bundled x264 may not be byte-identical to what was compiled in (GPL risk; best fix: rebuild FFmpeg from known sources). The many other external libraries in the configure line, probably linked statically, are not yet covered by `sources/` (TODO). Tagged releases fail until resolved.

- **发布包内随附 / Shipped inside every release package：** 发布包（zip 及将来的安装包）在 `sources/` 目录附带对应源码：

  | 包内路径 / Path | 内容 / Content |
  | --- | --- |
  | `sources/ffmpeg-37064b2d16.tar.gz` | FFmpeg commit `37064b2d16` 的源码归档 |
  | `sources/x265-f8be4634.tar.gz` | x265 commit `f8be4634` 的源码归档 |
  | `sources/x264-<COMMIT>.tar.gz` | x264 `stable` 分支中 `X264_BUILD 165` 的提交（**TODO：commit 待确定**） |
  | `sources/BUILD_CONFIG.txt` | configure 命令行、来源说明与构建说明（模板：`packaging/BUILD_CONFIG.txt`） |
  | `sources/SHA256SUMS` | `sources/` 内其它文件的 SHA-256 |

  CI（`.github/workflows/windows-build.yml`）固定下载 FFmpeg `37064b2d16` 与 x265 `f8be4634`；x264 通过仓库变量 `X264_COMMIT` 指定，CI 会检查所下载归档的 `x264.h` 中 `X264_BUILD` 为 165。`X264_COMMIT` 未设置、x264 校验失败，或 `BUILD_CONFIG.txt` 里仍有 TODO 时，**打 tag 的发布构建会直接失败**；非 tag 构建继续，但包内会有 `sources/INCOMPLETE-DO-NOT-DISTRIBUTE.txt`，**不得对外发布**。
  The workflow pins FFmpeg `37064b2d16` and x265 `f8be4634`; x264 comes from the repository variable `X264_COMMIT` and is checked for `X264_BUILD 165`. Tagged builds fail while `X264_COMMIT` is unset, the x264 check fails, or `BUILD_CONFIG.txt` still has TODO items; other builds are marked `INCOMPLETE-DO-NOT-DISTRIBUTE.txt` and must not be published.

**该构建的配置 / Build configuration [文件，取自 DLL 内嵌的 configure 字符串]：**

- `--enable-gpl` ✅ **启用**
- `--enable-version3` ✅ **启用**
- `--enable-nonfree` ❌ **未出现**（没有启用 nonfree）
- `--disable-libfdk-aac`（明确关闭）
- DLL 自报：`libavcodec license: GPL version 3 or later`、`libavutil license: GPL version 3 or later`
- `--enable-shared --disable-static`（共享库，动态链接）
- 交叉编译 `x86_64-w64-mingw32-gcc`，`--extra-version=20250509`

启用的外部库（节选，与许可证类别相关者）/ Notable enabled external libraries：

| 类别 / Class | 库 / Libraries（`--enable-…`） | 说明 / Note [知识] |
| --- | --- | --- |
| **GPL-only（使构建为 GPL）** | `libx264`, `libx265`, `libxvid`, `libxavs2`, `libdavs2`, `libvidstab`, `frei0r`, `librubberband`, `libdvdread`, `libdvdnav`, `libzvbi`（部分） | 这些是 `--enable-gpl` 的原因。**`libx264` 被本项目软件编码路径直接使用**（`encoder/ffvencoder.cpp` 中的 `libx264`）。 |
| 需 GPLv3/LGPLv3 才兼容（`--enable-version3` 的原因） | `libopencore-amrnb/-amrwb`（Apache-2.0）、`gmp`（LGPLv3/GPLv2 双许可）、`libaribb24`（LGPL-3）、`libvmaf`（BSD-2+Patent）等 | 因此整体是 **GPL-3.0-or-later**，不是 GPL-2.0。 |
| LGPL | `libmp3lame`, `libsoxr`, `libtwolame`, `libssh`, `libplacebo`, `libbluray`, `libgme`, `chromaprint`, `openal`, `libfribidi`, `libzmq` 等 | LGPL 组件在 GPL 整体中合并分发没问题。 |
| 宽松 / Permissive | `libaom`, `libdav1d`, `libvpx`, `libopus`, `libvorbis`, `libtheora`, `libwebp`, `libass`, `libfreetype`, `libharfbuzz`, `libxml2`, `libzimg`, `libsvtav1`, `librav1e`, `libvvenc`, `libjxl`, `libopenjpeg`, `libopenh264`, `libkvazaar`, `libsrt`, `librist`, `sdl2`, `zlib`, `lzma` 等 | BSD/MIT/ISC/Apache/MPL 等，逐项以上游为准。 |
| 硬件接口 | `--enable-amf`（AMD AMF 头文件，MIT）、`--enable-ffnvcodec`（nv-codec-headers，MIT）、`--enable-libvpl`（Intel oneVPL，MIT）、`--enable-vulkan`、`--enable-opencl`、`--enable-cuda-llvm` | 编译期头文件为宽松许可；**运行期**依赖用户机器上的显卡驱动（AMD/NVIDIA/Intel），驱动不随本项目分发。未使用需要 NVIDIA CUDA SDK 的 nonfree 选项。 |
| 其它 | `schannel`（Windows 系统 TLS）、`avisynth`（运行期动态加载）、`libopenmpt`、`liboapv` 等 | — |

**与 GPLv3 的兼容性 / Compatibility with GPLv3：**

- 本项目（GPL-3.0）+ FFmpeg（GPL-3.0-or-later）动态链接：**兼容**，合并作品按 GPL-3.0 发布。该 FFmpeg 未启用 nonfree，因此**可合法再分发**（一旦 `--enable-nonfree`，二进制将不可再分发）。
- **分发义务 / Obligations when distributing binaries：** 若发布的压缩包/安装包**包含** FFmpeg DLL，必须随二进制提供（或书面承诺提供）**对应源码**：所用 FFmpeg 的精确版本（commit `37064b2d16`）及全部被启用的 GPL 组件（x265 已定位到 commit `f8be4634`；x264 仅知 `core 165`，确切 revision 未知，**TODO**；其余静态链接库的版本与源码也尚未收集，**TODO**）的源码、构建脚本与完整 configure 配置，以及 GPL-3.0 许可证文本。**仅发布源码、让用户自行提供 FFmpeg 时，本项目不分发 FFmpeg。** 本仓库的 CI（`.github/workflows/windows-build.yml`）会把下载的 FFmpeg DLL 打进发布包，维护者在发布前必须履行上述义务。
- 若项目选择 **GPL-3.0-only**：与 GPL-3.0-or-later 的 FFmpeg 组合没有问题；若想写 “GPL-2.0-or-later” 则**不可行**（被 version3 组件阻断）。

**与“GPLv3 + 商业授权”的兼容性 / Compatibility with dual-licensing：**

- ❌ **该 FFmpeg 构建不能随闭源商业版本分发。** 项目方对 FFmpeg/x264/x265 等不拥有版权，CLA 无法让其可重新授权。
- 商业版本的可行路径 / Paths for a commercial edition：
  1. 改用 **LGPL 构建**的 FFmpeg（`--disable-gpl --disable-nonfree`，仅动态链接，保留用户替换 DLL 的能力，附 LGPL 文本与源码获取方式）；**软件 H.264 编码需另寻来源**（代码中的 `libx264` 路径在 LGPL 构建里不可用；可依赖 NVENC/AMF/QSV、Windows Media Foundation `h264_mf`、或 OpenH264 的合规分发方式），并回归测试所有用到的滤镜/封装器是否仍可用。
  2. 向 x264（x264 LLC）、x265（MulticoreWare）等取得**商业授权**。
  3. 开源版与商业版分别维护不同的 FFmpeg 构建。
- 专利（非版权问题，需专业意见）：H.264 / HEVC / AAC 等编码格式涉及专利池（如 Via LA、Access Advance 等）。LGPL/GPL 授权**不**授予专利许可。

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
| **`reference/ffmpeg-fftools/ffmpeg_mux.c`** | 892 行；FFmpeg `fftools/ffmpeg_mux.c` 的整文件原样拷贝，**逐字节未改**；不参与编译（任何 CMake 目标都未引用）；说明见同目录 [`NOTICE.md`](reference/ffmpeg-fftools/NOTICE.md)。**此拷贝取自哪个 FFmpeg 版本未记录、也未能核对，不声称与开发所用的 `37064b2d16` 一致**（待维护者用 FFmpeg 源码树比对，TODO） | **LGPL-2.1-or-later**，版权人为 FFmpeg 项目贡献者；文件头 “This file is part of FFmpeg … GNU Lesser General Public License … version 2.1 or later” | **不是本项目作者的代码。** 原版权头保持不变；许可证文本见 [`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt)；仅作设计对照参考；商业版分发包不要包含它；[`CLA.md`](CLA.md) 不覆盖它，不接受对它的修改性贡献。LGPL-2.1-or-later 允许按 GPL 条款再分发，放在 GPLv3 仓库没有文件层面的冲突。它引用的 `ffmpeg.h`、`ffmpeg_mux.h`、`ffmpeg_utils.h`、`sync_queue.h` 是 FFmpeg 内部头，所以也无法单独编译。 |
| 图标 `icons/*.png`（12 个） | `res.qrc` 嵌入可执行文件 | **本项目自有作品**，GPL-3.0（由项目维护者本人制作） | 不是第三方材料；和代码一样适用 GPL-3.0 与 `CLA.md`。 |
| 字体 | QML 指定 `Segoe UI` | Microsoft 系统字体，未随包分发 | 不随包分发则无问题；不要把字体文件加入发行包。 |

## 4. 构建工具与 CI（不随成品分发）

CMake（BSD-3）、Ninja（Apache-2.0）、MinGW-w64 GCC（GPL，有 Runtime Exception）、`tools/*.ps1`（自有脚本）。CI（`.github/workflows/windows-build.yml`）使用的 GitHub Actions（`actions/checkout`、`actions/upload-artifact`、`jurplel/install-qt-action`、`softprops/action-gh-release`）为各自许可，仅在 CI 使用。

## 5. 发布前待办 / Pre-release checklist

1. 决定项目许可证写法：`GPL-3.0-only` 还是 `GPL-3.0-or-later`（“or-later” 需所有版权人同意）。
2. 为发行包中包含的 FFmpeg 二进制准备**对应源码**（精确 commit、所有 GPL 库的版本与补丁、构建脚本），已决定**随发布包附带**（`sources/`，见 §1），待补：x264 的确切 commit、其余静态链接库的版本与源码、构建脚本/工具链（`packaging/BUILD_CONFIG.txt` 中的 TODO；更干净的做法是用已知源码重新构建 FFmpeg），以及 `ffmpeg_mux.c` 来源版本的核对（`reference/ffmpeg-fftools/NOTICE.md`）。
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
