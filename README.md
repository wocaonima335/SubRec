<!-- 发布前请替换 <OWNER> 占位符（GitHub 用户名或组织名）。 / Replace the <OWNER> placeholder (GitHub user or organization) before publishing. -->

# 字录 · SubRec

**录屏即出片 · Record once, get a finished video.**

[中文](#中文) | [English](#english)

> Status / 状态：早期原型（early prototype）。仅 Windows。构建说明基于对源码的阅读，**尚未在 Windows 上完整构建并录制验证**。
> Windows only. Build notes are derived from reading the sources and have **not yet been verified by a full build and recording on Windows**.

---

## 中文

### 这是什么

字录（SubRec）是一款基于 **Qt 6 Quick（QML）+ FFmpeg** 的 Windows 录屏工具。点击红色按钮开始、再点击结束，程序在停止时直接写出可播放的 **MP4 成片**（H.264 视频，可选 AAC 音频，MP4 文件头已前置 `+faststart`），无需额外的导出/转码步骤。

### 已实现的功能（以代码为准）

- 同时采集 **屏幕、摄像头、系统声音、麦克风**，在界面中切换视频源。
- 录制 **暂停 / 恢复**，并修正时间轴，避免暂停后时间戳跳变。
- **实时预览**：CPU 模式由编码线程提交 YUV 帧；GPU 模式由独立工作线程转换预览帧，再送入 OpenGL（GLSL）预览控件。
- 显式的媒体流水线：demux → decode → filter → encode → mux，带有界阻塞队列。
- 编码器按顺序尝试 **NVENC → AMD AMF → Intel QSV → libx264（软件）**，取第一个可用者（`encoder/ffvencoder.cpp`）。
- 录制过程中显示计时；可选择输出目录和文件名；“视频”页可浏览已录制文件。
- 控制链路：`QmlBridge → CaptureScheduler → CaptureSession → FFRecorder`。

### 已知限制（如实说明）

- 屏幕采集有两个后端（设置页“Screen backend”或环境变量 `SUBREC_CAPTURE_BACKEND=auto|ddagrab|dshow`）：
  - **Desktop Duplication（FFmpeg `ddagrab` 滤镜，默认 auto 时优先）**：无需第三方组件；NVIDIA/AMD 上 D3D11 帧直接交给 `h264_nvenc`/`h264_amf`，其它情况下载到内存转 yuv420p。GPU 直通仍从独立的 hwdownload 预览支路生成约 360p、8 fps 的 yuv420p 画面。**尚未在真机上验证**（见 `tools/pc2_verify.ps1`）。只能采集默认显卡上的显示器；遇到 UAC/锁屏/分辨率变化时视频流结束（文件保留）。
  - **DirectShow（dshow）+ `screen-capture-recorder`**：需用户自行安装该第三方组件；ddagrab 打不开时自动回退到它（若已安装）。
- 原生 WASAPI loopback 已在 `capture/wasapi_*` 实现，是默认系统声音后端；`SUBREC_SYSTEM_AUDIO_BACKEND` 为 `auto`（默认）或 `wasapi` 时行为相同：优先 WASAPI；没有 WASAPI 设备或打开失败时，若已自行安装 `virtual-audio-capturer`（DirectShow，本仓库不包含）则回退到它（混音模式不回退）；`dshow` 直接使用 `virtual-audio-capturer`。麦克风与摄像头走 DirectShow；WGC 未实现。
- 系统音频和麦克风按钮可以独立打开；两者同时打开时使用 WASAPI 系统音频与 DirectShow 麦克风混音。DirectShow 系统音频后端不支持混音，界面会显示提示。两者都关闭时输出不含音轨的无声 MP4，视频时钟独立推进，暂停时不推进。
- “设置”“滤镜”等部分界面尚未完成（见 Issues）。
- 目前没有视频剪辑功能；“视频”页只是已录制文件的列表。
- 代码注释以中文为主。

### 构建（Windows）

依赖：

- Qt **6.5+**（工程预设使用 6.8.0），模块：Core、Gui、Widgets、Qml、Quick、Multimedia（QML 还使用 Quick Controls / Layouts / Effects、`Qt.labs.platform`、`Qt.labs.folderlistmodel`）。
- CMake ≥ 3.21（使用 `CMakePresets.json` 需 ≥ 3.25），Ninja，C++17 编译器（CI 与预设使用 MinGW 13.1 x64）。
- **FFmpeg 共享库（shared）**：**本仓库不含 FFmpeg，需自行提供。** 需要 `include/`、`lib/`（`avcodec` `avformat` `avutil` `swscale` `swresample` `avfilter` `avdevice` 的 `.lib`/`.dll.a`）、`bin/`（DLL）。用 `-DFFMPEG_ROOT=<目录>` 或环境变量 `SUBREC_FFMPEG_ROOT` 指定；**未指定时 CMake 会报错并给出提示**。推荐使用本仓库的脚本 [`packaging/ffmpeg/build-ffmpeg.sh`](packaging/ffmpeg/README.md) 从固定的上游源码交叉编译最小 FFmpeg（Linux 主机 + MinGW-w64；FFmpeg **n8.0.3**，commit `8ae0b349`；x264 stable `b35605ac`；libvpl v2.16.0；nv-codec-headers n12.2.72.0；AMF v1.5.3；`--enable-gpl --enable-libx264`，**无** `--enable-version3`、**无** `--enable-nonfree`；DLL：`avutil-60` `avcodec-62` `avformat-62` `avfilter-11` `avdevice-62` `swscale-9` `swresample-6`，合计约 11 MB），产物目录即可作为 `FFMPEG_ROOT`；CI 的 `ffmpeg` job 做同样的事并把产物交给 Windows job。其它 FFmpeg 构建需自行验证 API 兼容性。源码：<https://git.ffmpeg.org/ffmpeg.git>。

```powershell
# 1. 配置（MinGW g++ 与 Ninja 需在 PATH 中），显式传参；或设置 QT_ROOT 与 SUBREC_FFMPEG_ROOT 环境变量后改用 `cmake --preset windows-mingw-release`：
cmake -S . -B build/windows-mingw-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="<你的 Qt>/6.8.0/mingw_64" `
  -DFFMPEG_ROOT="<你的 FFmpeg 目录>"

# 2. 编译（目标名为 appSubRec）
cmake --build build/windows-mingw-release --target appSubRec

# 3. 运行：构建时 CMake 会把 FFmpeg DLL 复制到输出目录
build/windows-mingw-release/appSubRec.exe
```

可选：`-DSUBREC_BUILD_LIFECYCLE_TESTS=OFF` 关闭 Windows 下默认开启的全部 CTest 测试。按 CI 的方式在本地运行测试：构建 `subrec_tests` 目标后执行 `ctest --test-dir <build> -L "^ci$" -LE "^(device|needs_ffmpeg_test_components|needs_powershell)$" --output-on-failure`。

> 关于 FFmpeg 的许可：发布包中的 FFmpeg 是 **GPL-2.0-or-later** 构建（`--enable-gpl --enable-libx264`；无 version3、无 nonfree；与 SubRec 的 GPL-3.0 兼容）。本仓库不分发它；但发布包（zip 及将来的安装包）会包含它，因此发布包的 **`sources/` 目录附带对应源码**：`ffmpeg-n8.0.3.tar.xz`、`x264-b35605ace3dd.tar.xz`、`libvpl-v2.16.0.tar.xz`、`nv-codec-headers-n12.2.72.0.tar.xz`、`amf-headers-v1.5.3.tar.xz`、`subrec-ffmpeg-build-scripts.tar.xz`（构建脚本）、`BUILD_CONFIG.txt`（configure 参数、工具链版本、DLL 哈希）和 `SHA256SUMS`（合计约 18 MB）。发布包不含 x265，也**不含 `ffmpeg.exe`**（SubRec 从不启动它）。构建时的警告分类见 [`packaging/ffmpeg/BUILD_WARNINGS.md`](packaging/ffmpeg/BUILD_WARNINGS.md)；详见 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。

### 下载与安装：Windows SmartScreen 警告

本项目是个人开源项目，**没有购买代码签名证书**，因此发布的 `.exe` / 安装包**没有数字签名**。首次运行时 Windows SmartScreen 可能弹出“Windows 已保护你的电脑 / 无法识别的应用”（发布者显示为“未知发布者”）。这是未签名程序的正常现象，**不代表**文件已被判定为恶意；但也意味着系统无法替你确认来源，请自行核对：

1. **只从本项目官方 Release 页面下载**（`https://github.com/<OWNER>/SubRec/releases`，发布前替换 `<OWNER>`）。不要运行来自网盘转载、第三方下载站或他人转发的同名文件。
2. **校验 SHA256**（强烈建议）：每个发布文件旁会附带 SHA256 校验值（Release 说明或随附的 `.sha256` 文件）。在 PowerShell 中执行：

   ```powershell
   Get-FileHash .\<下载的文件名> -Algorithm SHA256
   # 或：certutil -hashfile .\<下载的文件名> SHA256
   ```

   输出的哈希必须与 Release 页面公布的**完全一致**（不区分大小写）；不一致请不要运行，并[报告问题](SECURITY.md)。注意：校验和与文件同处一个页面，只能发现下载损坏或中途被替换，不能替代代码签名。
3. 校验通过后，在 SmartScreen 弹窗上点击 **「更多信息」**，再点击 **「仍要运行」**。
4. 如果文件属性里有“解除锁定”复选框（右键 → 属性 → 常规 → 安全），勾选后再运行；解压 zip 后若仍被拦截，对 zip 文件先做此操作再解压。
5. 杀毒软件对未签名、且会采集屏幕/音频的程序可能误报；你也可以自己用源码构建（见上文“构建”），避免信任预编译文件。

### 许可证与贡献

- 许可证：**GPL-3.0**（见 [LICENSE](LICENSE)）。项目方计划在未来提供商业授权版本（双重授权）。
- **所有贡献都需要先签署 [CLA](CLA.md)**，详见 [CONTRIBUTING.md](CONTRIBUTING.md)。
- 行为准则：[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)；安全问题请见 [SECURITY.md](SECURITY.md)。
- 第三方组件清单：[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。
- 界面图标（`icons/*.png`）由项目维护者本人制作，与本项目其余代码一样按 **GPL-3.0** 发布。
- `reference/ffmpeg-fftools/ffmpeg_mux.c` 是保留原版权头的 FFmpeg 参考文件（LGPL-2.1+），仅供参考，不参与构建，不适用本项目的 CLA；详见 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。

---

## English

### What is it

SubRec (字录) is a Windows screen recorder built with **Qt 6 Quick (QML) and FFmpeg**. Press the red button to start and again to stop; on stop the app has already written a playable **MP4** (H.264 video with optional AAC audio, with `+faststart` set on the MP4 muxer), so there is no separate export or transcode step. The project's tagline is "录屏即出片" ("record, and the finished video is ready").

### Implemented (per the source code)

- Capture of **screen, camera, system audio and microphone**, switchable from the UI.
- **Pause / resume** with timeline correction so timestamps do not jump after a pause.
- **Live preview**: CPU frames come from the encoder thread; GPU frames use an independent conversion worker. Both feed YUV frames to an OpenGL (GLSL) preview item.
- Explicit media pipeline: demux → decode → filter → encode → mux, connected by bounded blocking queues.
- Video encoder selection in order **NVENC → AMD AMF → Intel QSV → libx264 (software)**; the first usable one wins (`encoder/ffvencoder.cpp`).
- Elapsed-time display, configurable output folder / file name, and a "video" page that lists recorded files.
- Control path: `QmlBridge → CaptureScheduler → CaptureSession → FFRecorder`.

### Known limitations (stated honestly)

- Screen capture has two backends (settings page "Screen backend", or `SUBREC_CAPTURE_BACKEND=auto|ddagrab|dshow`):
  - **Desktop Duplication (FFmpeg's `ddagrab` filter, preferred by `auto`)**: no third-party component; on NVIDIA/AMD the D3D11 frames go straight to `h264_nvenc`/`h264_amf`, otherwise they are downloaded and converted to yuv420p. GPU passthrough uses a separate hwdownload preview branch at about 360p and 8 fps. **Not yet verified on real hardware** (see `tools/pc2_verify.ps1`). Only displays on the default GPU; a UAC prompt, lock screen or resolution change ends the video stream (the file is kept).
  - **DirectShow (dshow) + `screen-capture-recorder`**: the third-party filter must be installed by you; used automatically if ddagrab cannot open (when installed).
- Native WASAPI loopback is implemented in `capture/wasapi_*` and is the default system audio backend. `SUBREC_SYSTEM_AUDIO_BACKEND=auto` (default) and `wasapi` behave the same: WASAPI is preferred, and if no WASAPI device is available or it fails to open, capture falls back to the separately installed `virtual-audio-capturer` (DirectShow, not bundled) when present (no fallback in mixed mode). `dshow` uses `virtual-audio-capturer` directly. Microphone and camera use DirectShow; WGC is not implemented.
- System audio and microphone buttons are independent. Mixed mode uses WASAPI system audio plus a DirectShow microphone; the UI explains why a DirectShow system backend cannot be mixed. With both buttons off, SubRec writes a video-only MP4 without an audio track. Its video clock advances independently and excludes paused frames. See [audio modes](docs/AUDIO_MIXING.md).
- Some UI pages (settings, filters) are incomplete (see the issue tracker).
- There is no video editing; the "video" page is only a list of recorded files.
- Code comments are mostly in Chinese.

### Building (Windows)

Requirements:

- Qt **6.5+** (presets use 6.8.0): Core, Gui, Widgets, Qml, Quick, Multimedia (QML additionally uses Quick Controls / Layouts / Effects, `Qt.labs.platform`, `Qt.labs.folderlistmodel`).
- CMake ≥ 3.21 (≥ 3.25 to use `CMakePresets.json`), Ninja, a C++17 compiler (CI and the preset use MinGW 13.1 x64).
- **FFmpeg shared libraries**: **this repository does not include FFmpeg; you must provide it.** You need `include/`, `lib/` (import libraries for `avcodec avformat avutil swscale swresample avfilter avdevice`) and `bin/` (DLLs). Point to it with `-DFFMPEG_ROOT=<dir>` or the `SUBREC_FFMPEG_ROOT` environment variable; **CMake stops with a clear error if it is not set**. The recommended way is the repository's script [`packaging/ffmpeg/build-ffmpeg.sh`](packaging/ffmpeg/README.md), which cross-compiles a minimal FFmpeg from pinned upstream sources (Linux host + MinGW-w64; FFmpeg **n8.0.3**, commit `8ae0b349`; x264 stable `b35605ac`; libvpl v2.16.0; nv-codec-headers n12.2.72.0; AMF v1.5.3; `--enable-gpl --enable-libx264`, **no** `--enable-version3`, **no** `--enable-nonfree`; DLLs: `avutil-60` `avcodec-62` `avformat-62` `avfilter-11` `avdevice-62` `swscale-9` `swresample-6`, about 11 MB together). The output directory can be used directly as `FFMPEG_ROOT`; the CI `ffmpeg` job does the same and hands the result to the Windows job. Other FFmpeg builds need their API compatibility checked. Source: <https://git.ffmpeg.org/ffmpeg.git>.

```powershell
# 1. Configure (MinGW g++ and Ninja on PATH) with explicit paths; or set QT_ROOT and SUBREC_FFMPEG_ROOT and use `cmake --preset windows-mingw-release` instead:
cmake -S . -B build/windows-mingw-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="<your Qt>/6.8.0/mingw_64" `
  -DFFMPEG_ROOT="<your FFmpeg dir>"

# 2. Build (the executable target is named appSubRec)
cmake --build build/windows-mingw-release --target appSubRec

# 3. Run. CMake copies the FFmpeg DLLs next to the executable post-build.
build/windows-mingw-release/appSubRec.exe
```

Optional: `-DSUBREC_BUILD_LIFECYCLE_TESTS=OFF` disables all CTest tests, which are ON by default on Windows. To run the tests locally the way CI does, build the `subrec_tests` target and run `ctest --test-dir <build> -L "^ci$" -LE "^(device|needs_ffmpeg_test_components|needs_powershell)$" --output-on-failure`.

> FFmpeg licensing note: the FFmpeg in the release packages is a **GPL-2.0-or-later** build (`--enable-gpl --enable-libx264`; no version3, no nonfree; compatible with SubRec's GPL-3.0). This repository does not distribute it, but the release packages (zip, and any future installer) do include it, so every package carries the corresponding source in its **`sources/` directory**: `ffmpeg-n8.0.3.tar.xz`, `x264-b35605ace3dd.tar.xz`, `libvpl-v2.16.0.tar.xz`, `nv-codec-headers-n12.2.72.0.tar.xz`, `amf-headers-v1.5.3.tar.xz`, `subrec-ffmpeg-build-scripts.tar.xz` (the build scripts), `BUILD_CONFIG.txt` (configure arguments, toolchain versions, DLL hashes) and `SHA256SUMS` (about 18 MB in total). The packages contain no x265 and **no `ffmpeg.exe`** (SubRec never starts it). Build warnings are classified in [`packaging/ffmpeg/BUILD_WARNINGS.md`](packaging/ffmpeg/BUILD_WARNINGS.md). See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

### Downloading and installing: Windows SmartScreen warning

This is a personal open-source project and **does not use a code-signing certificate**, so the released `.exe` / installer is **not digitally signed**. On first launch Windows SmartScreen may show "Windows protected your PC" / "unrecognized app" with the publisher listed as "Unknown publisher". This is expected for unsigned software and does **not** mean the file has been judged malicious — but it also means Windows cannot vouch for where it came from, so please verify it yourself:

1. **Download only from this project's official Releases page** (`https://github.com/<OWNER>/SubRec/releases`; replace `<OWNER>` before publishing). Do not run same-named files from mirrors, file-sharing links or third-party download sites.
2. **Verify the SHA256** (strongly recommended). The SHA256 of every release file is published next to it (in the release notes or the attached `.sha256` file). In PowerShell:

   ```powershell
   Get-FileHash .\<downloaded-file> -Algorithm SHA256
   # or: certutil -hashfile .\<downloaded-file> SHA256
   ```

   The hash must match the published value **exactly** (case-insensitive). If it does not, do not run the file and [report it](SECURITY.md). Note: a checksum published on the same page only detects corruption or tampering in transit; it is not a substitute for code signing.
3. Once verified, on the SmartScreen dialog click **"More info"**, then **"Run anyway"**.
4. If the file's Properties dialog shows an "Unblock" checkbox (right-click → Properties → General → Security), tick it before running. For a zip, unblock the zip first and then extract it.
5. Antivirus tools may flag unsigned programs that capture the screen and audio. You can also build from source (see "Building" above) and avoid trusting a prebuilt binary.

### License and contributing

- License: **GPL-3.0** ([LICENSE](LICENSE)). The maintainers intend to offer a commercial license for future versions (dual licensing).
- **Every contribution requires a signed [CLA](CLA.md)**; see [CONTRIBUTING.md](CONTRIBUTING.md).
- Code of conduct: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Security reports: [SECURITY.md](SECURITY.md).
- Third-party components: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
- The UI icons (`icons/*.png`) were created by the project maintainer and are released under **GPL-3.0**, like the rest of the project.
- `reference/ffmpeg-fftools/ffmpeg_mux.c` is a reference file from FFmpeg that keeps its original copyright header (LGPL-2.1+). It is for reference only, is not part of the build, and is not covered by this project's CLA; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

### GPU preview implementation and verification

The ddagrab GPU graph splits unchanged D3D11 frames to the encoder and a second
hardware sink. Capture drains that sink with `AV_BUFFERSINK_FLAG_NO_REQUEST`,
then offers references through a single pending-frame slot without waiting for
the consumer. A separate worker graph runs `fps=8,hwdownload,format=bgra,scale,...,format=yuv420p`
to a software preview sink (even dimensions, height at most 360). This worker
boundary keeps GPU transfers and conversion off the recording thread. It also
works when the encoder downloads GPU frames internally; only the GPU branch
submits preview in that mode. CPU capture retains its existing preview path.

Rebuild the packaged FFmpeg with `packaging/ffmpeg/build-ffmpeg.sh`: GPU preview
requires the production `split` and `fps` filters as well as `hwdownload`,
`format`, and `scale`. Every five seconds while frames arrive,
`[Preview] gpu-branch frames=N fps=X dropped=Y` reports converted preview frames,
interval rate, and drops from the pending slot/conversion output/GUI handoff.
Normal 8 fps sampling is not a drop. GUI stalls replace pending frames instead
of growing queues. A conversion failure is logged and leaves recording running.
Verify NVENC passthrough and GPU-to-CPU encoder fallback on Windows, including
GUI stalls, sustained capture rate, and pause/stop/restart. D3D11 driver/device
contention and actual transfer latency still require real-hardware measurement.
