<!-- 发布前请替换 <OWNER> 占位符（GitHub 用户名或组织名）。 / Replace the <OWNER> placeholder (GitHub user or organization) before publishing. -->

# 字录 · SubRec

**录屏即出片 · Record once, get a finished video.**

[中文](#中文) | [English](#english)

> Status / 状态：早期原型（early prototype）。仅 Windows。构建说明基于对源码的阅读，**尚未在 Windows 上完整构建并录制验证**。
> Windows only. Build notes are derived from reading the sources and have **not yet been verified by a full build and recording on Windows**.

---

## 中文

### 这是什么

字录（SubRec）是一款基于 **Qt 6 Quick（QML）+ FFmpeg** 的 Windows 录屏工具。点击红色按钮开始、再点击结束，程序在停止时直接写出可播放的 **MP4 成片**（H.264 视频 + AAC 音频，MP4 文件头已前置 `+faststart`），无需额外的导出/转码步骤。

### 已实现的功能（以代码为准）

- 同时采集 **屏幕、摄像头、系统声音、麦克风**，在界面中切换视频源。
- 录制 **暂停 / 恢复**，并修正时间轴，避免暂停后时间戳跳变。
- **实时预览**：编码线程把 YUV 帧送入 OpenGL（GLSL）预览控件。
- 显式的媒体流水线：demux → decode → filter → encode → mux，带有界阻塞队列。
- 编码器按顺序尝试 **NVENC → AMD AMF → Intel QSV → libx264（软件）**，取第一个可用者（`encoder/ffvencoder.cpp`）。
- 录制过程中显示计时；可选择输出目录和文件名；“视频”页可浏览已录制文件。
- 控制链路：`QmlBridge → CaptureScheduler → CaptureSession → FFRecorder`。

### 已知限制（如实说明）

- 目前采集后端是 FFmpeg **DirectShow（dshow）**。屏幕与系统声音依赖用户**自行安装**的第三方 DirectShow 组件（`screen-capture-recorder`、`virtual-audio-capturer`），本仓库不包含它们。没有这些组件就无法录屏/录系统声音。
- 基于 WASAPI / DXGI / WGC 的**原生采集仍在开发中**（见 `tests/native_capture_probe/`），尚未作为正式功能提供。
- “设置”“滤镜”等部分界面尚未完成（见 Issues）。
- 目前没有视频剪辑功能；“视频”页只是已录制文件的列表。
- 代码注释以中文为主。

### 构建（Windows）

依赖：

- Qt **6.5+**（工程预设使用 6.8.0），模块：Core、Gui、Widgets、Qml、Quick、Multimedia（QML 还使用 Quick Controls / Layouts / Effects、`Qt.labs.platform`、`Qt.labs.folderlistmodel`）。
- CMake ≥ 3.21（使用 `CMakePresets.json` 需 ≥ 3.25），Ninja，C++17 编译器（CI 与预设使用 MinGW 13.1 x64）。
- **FFmpeg 共享库（shared）**：**本仓库不含 FFmpeg，需自行提供。** 需要 `include/`、`lib/`（`avcodec` `avformat` `avutil` `swscale` `swresample` `avfilter` `avdevice` 的 `.lib`/`.dll.a`）、`bin/`（DLL）。用 `-DFFMPEG_ROOT=<目录>` 或环境变量 `SUBREC_FFMPEG_ROOT` 指定；**未指定时 CMake 会报错并给出提示**。开发时使用的是 FFmpeg `N-119453-g37064b2d16-20250509`（2025-05-09 的 git master 快照，commit `37064b2d16`，**非正式发行版**；libavutil 60.2.100 / libavcodec 62.3；DLL：`avutil-60` `avcodec-62` `avformat-62` `avfilter-11` `avdevice-62` `swscale-9` `swresample-6`）的 GPLv3 共享构建；其它版本需自行验证 API 兼容性。源码：<https://git.ffmpeg.org/ffmpeg.git>、<https://github.com/FFmpeg/FFmpeg/commit/37064b2d16>。

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

可选：`-DSUBREC_BUILD_LIFECYCLE_TESTS=OFF` 关闭 Windows 下默认开启的生命周期测试目标。

> 关于 FFmpeg 的许可：开发所用的预编译 FFmpeg 是 **GPL v3** 构建（`--enable-gpl --enable-version3`，含 x264/x265）。本仓库不分发它；但发布包（zip 及将来的安装包）会包含它，因此发布包的 **`sources/` 目录附带对应源码**：`ffmpeg-37064b2d16.tar.gz`、`x264-<COMMIT>.tar.gz`、`x265-f8be4634.tar.gz`、`BUILD_CONFIG.txt`（configure 命令行）和 `SHA256SUMS`。其中 x265 为 `4.1+151-f8be4634`（commit `f8be4634`），x264 仅知 core 165（确切 revision 待确认）；详见 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。

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

SubRec (字录) is a Windows screen recorder built with **Qt 6 Quick (QML) and FFmpeg**. Press the red button to start and again to stop; on stop the app has already written a playable **MP4** (H.264 video + AAC audio, with `+faststart` set on the MP4 muxer), so there is no separate export or transcode step. The project's tagline is "录屏即出片" ("record, and the finished video is ready").

### Implemented (per the source code)

- Capture of **screen, camera, system audio and microphone**, switchable from the UI.
- **Pause / resume** with timeline correction so timestamps do not jump after a pause.
- **Live preview**: the encoder thread feeds YUV frames to an OpenGL (GLSL) preview item.
- Explicit media pipeline: demux → decode → filter → encode → mux, connected by bounded blocking queues.
- Video encoder selection in order **NVENC → AMD AMF → Intel QSV → libx264 (software)**; the first usable one wins (`encoder/ffvencoder.cpp`).
- Elapsed-time display, configurable output folder / file name, and a "video" page that lists recorded files.
- Control path: `QmlBridge → CaptureScheduler → CaptureSession → FFRecorder`.

### Known limitations (stated honestly)

- The only capture backend today is FFmpeg **DirectShow (dshow)**. Screen and system-audio capture rely on third-party DirectShow components that **you must install yourself** (`screen-capture-recorder`, `virtual-audio-capturer`); they are not part of this repository. Without them, screen and system-audio recording will not work.
- **Native WASAPI / DXGI / WGC capture is work in progress** (see `tests/native_capture_probe/`) and is not a shipped feature yet.
- Some UI pages (settings, filters) are incomplete (see the issue tracker).
- There is no video editing; the "video" page is only a list of recorded files.
- Code comments are mostly in Chinese.

### Building (Windows)

Requirements:

- Qt **6.5+** (presets use 6.8.0): Core, Gui, Widgets, Qml, Quick, Multimedia (QML additionally uses Quick Controls / Layouts / Effects, `Qt.labs.platform`, `Qt.labs.folderlistmodel`).
- CMake ≥ 3.21 (≥ 3.25 to use `CMakePresets.json`), Ninja, a C++17 compiler (CI and the preset use MinGW 13.1 x64).
- **FFmpeg shared libraries**: **this repository does not include FFmpeg; you must provide it.** You need `include/`, `lib/` (import libraries for `avcodec avformat avutil swscale swresample avfilter avdevice`) and `bin/` (DLLs). Point to it with `-DFFMPEG_ROOT=<dir>` or the `SUBREC_FFMPEG_ROOT` environment variable; **CMake stops with a clear error if it is not set**. Development used a GPLv3 shared build of FFmpeg `N-119453-g37064b2d16-20250509` (a 2025-05-09 git-master snapshot, commit `37064b2d16`, **not an official release**; libavutil 60.2.100 / libavcodec 62.3; DLLs: `avutil-60` `avcodec-62` `avformat-62` `avfilter-11` `avdevice-62` `swscale-9` `swresample-6`); other versions need their API compatibility checked. Source: <https://git.ffmpeg.org/ffmpeg.git>, <https://github.com/FFmpeg/FFmpeg/commit/37064b2d16>.

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

Optional: `-DSUBREC_BUILD_LIFECYCLE_TESTS=OFF` disables the lifecycle test target that is ON by default on Windows.

> FFmpeg licensing note: the prebuilt FFmpeg used in development is a **GPL v3** build (`--enable-gpl --enable-version3`, including x264/x265). This repository does not distribute it, but the release packages (zip, and any future installer) do include it, so every package carries the corresponding source in its **`sources/` directory**: `ffmpeg-37064b2d16.tar.gz`, `x264-<COMMIT>.tar.gz`, `x265-f8be4634.tar.gz`, `BUILD_CONFIG.txt` (configure command line) and `SHA256SUMS`. x265 is `4.1+151-f8be4634` (commit `f8be4634`); x264 is only known as core 165 (exact revision still to be confirmed). See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

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
