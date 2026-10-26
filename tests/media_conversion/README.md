# C-02 独立 CPU 媒体转换基础子集

**不是完整 C-02 接入，也不是录制验收。** 本目录只独立构建 `capture/media` 的 C++17 库；不依赖 Qt，不创建 Decoder/AVStream，不修改主工程 CMake、filter、encoder、queue 或台账。依赖用户通过 `FFMPEG_ROOT` 提供的 FFmpeg 共享构建中的 avutil 60、swscale 9、swresample 6 DLL/import library。

## 在 Git Bash 构建与运行

与主工程预设使用相同的工具链（MinGW、Ninja、Release）和 FFmpeg 根目录，但不调用主工程 preset，避免构建 appSubRec。CMake 要求 3.21 或以上。

以下命令可从任意 Git Bash 工作目录执行；不清目录、不安装、不提交。测试所需的三个 FFmpeg DLL 会复制到独立 build 目录；MinGW runtime 从 PATH 加载。

```bash
# 需要 MinGW g++、Ninja、CMake 已在 PATH 中；<path-to-ffmpeg> 是含 include/、lib/、bin/ 的 FFmpeg 共享构建目录
set -o pipefail
build=build/media-conversion-tests
mkdir -p "$build"

cmake -S tests/media_conversion -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFFMPEG_ROOT="<path-to-ffmpeg>" \
  2>&1 | tee "$build/configure.log" &&
cmake --build "$build" \
  2>&1 | tee "$build/build.log" &&
ctest --test-dir "$build" -V --output-on-failure \
  2>&1 | tee "$build/test-results.log"
```

也可在设置上述 PATH 后直接运行测试二进制：

```bash
build/media-conversion-tests/media_conversion_tests.exe
```

## API 契约

头文件：[media_conversion.h](../../capture/media/media_conversion.h)。命名空间 `capture::media`，库目标 `capture_media_conversion`。

### 所有权与错误

| API/对象 | 契约 |
|---|---|
| `AudioFormat(rate, format, layout, timeBase)` | 正采样率、有效声道布局、正时间基。自有布局，通过 `av_channel_layout_copy/uninit` 深拷贝并释放 custom map；支持 copy/move，自赋值安全。moved-from 对象可以析构或重新赋值，访问布局/复制它抛 `logic_error`。 |
| `VideoFormat(width, height, format, timeBase)` | 纯值描述，无外部资源；尺寸和时间基必须为正，宽度不超过 `INT32_MAX/4`。只接受 CPU BGRA/YUV420P，后者要求偶数宽高。 |
| `FramePtr` / `makeFrame()` | AVFrame 的 unique ownership，析构调用 `av_frame_free`；每次输出有独立引用计数缓冲，可在输入或转换器销毁后保留。 |
| `convert(const AVFrame&)` | 同步借用输入，不修改、不接管、不缓存输入指针。转换器内部上下文持久化，非线程安全、不可复制，不支持运行中重新配置。 |
| 非法格式、尺寸、时间基、plane、stride、PTS 跳变 | 抛 `std::invalid_argument`，在消耗音频输入前校验；通常可修正输入后重试。 |
| 错误生命周期调用 | 抛 `std::logic_error`，例如 drain 后继续 convert、失败后复用。 |
| 时间戳/计数溢出 | 抛 `std::overflow_error`；内存分配可能抛 `std::bad_alloc`。 |
| FFmpeg 负返回码 | 抛 `FfmpegError`（继承 `runtime_error`），包含操作名、错误文本及 `code()`。`sws_getContext` 只返回空指针，无法区分配置/分配失败，统一报告 EINVAL 并说明歧义。 |

输入必须是符合 FFmpeg 内存契约的可读 CPU AVFrame。调用者保证 plane 指针、`extended_data` 数组长度、实际分配容量及 FFmpeg 所需 padding；优先用 `av_frame_get_buffer` 分配。这里只能验证元数据/非空指针/最小 linesize，不能证明任意裸指针有效或检测伪造的缓冲长度。`AVChannelLayout` 的 custom map 被深拷贝，但 FFmpeg 的 `opaque` 指针及 channel opaque 所指用户对象不被深拷贝或释放；调用者管理其生命周期，不要通过 const 布局视图的内部指针修改 map。

### 视频：BGRA → YUV420P

- `VideoConverter(inputFormat, outputFormat)` 在构造时创建一个 `SwsContext`，后续所有帧复用。输入/输出尺寸各自固定，可以不同，缩放采用 `SWS_BILINEAR`；输入尺寸改变时明确拒绝，需新建实例。
- 接受非紧密正 stride 和负 stride；负 stride 时 `data[0]` 指向逻辑首行。要求 `abs(linesize[0]) >= width*4`，拒绝 `INT32_MIN`。不处理 GPU surface/readback。
- 明确采用 full-range BGRA → limited-range BT.601 YUV，输出标记 SMPTE170M/MPEG range；不依据输入帧 color metadata 自动选矩阵，不处理 HDR、alpha 合成或色彩管理。
- 输入 `pts`、非负 `duration` 从描述的时间基转换为输出描述时间基，设置输出 `time_base`。未知 PTS 保持未知。不会复制无关 side data 或伪造采集时钟。
- `convert` 成功必有输出，转换行数不足或 swscale 错误后实例不可再用。其它预校验/输出分配错误不改变上下文。

### 音频：PCM → 48000 Hz stereo FLTP

- `AudioConverter(inputFormat)` 深拷贝输入描述并持有一个 `SwrContext`，通过 `swr_alloc_set_opts2/swr_init` 初始化。固定输出 `48000 / stereo / AV_SAMPLE_FMT_FLTP / time_base=1/48000`，使用 FFmpeg 默认重采样/混音参数。
- 支持 U8、S16、S32、S64、FLT、DBL 的 packed/planar 共 12 种格式。输入率、格式及声道布局必须匹配描述。有效描述可以携带 UNSPEC 布局，但转换器拒绝只有声道数、没有身份的 UNSPEC；其它 FFmpeg 不支持的布局/混音组合通过初始化异常返回。
- PCM 每次 `nb_samples > 0`，每声道连续样本；packed 只读第一 plane，planar 通过 `extended_data` 读取所有声道。检查 `linesize[0]` 至少覆盖每 plane 的样本字节，不接受短 plane 或缺失指针；不把空帧当 EOS。
- 用 `swr_get_out_samples` 查询容量，输出 `nb_samples/duration` 设置为实际返回样本数。`convert` 返回空指针仅表示重采样器暂时缓存了输入，**不是 EOF**。
- EOS 后重复调用 `flush()` 或同义的 `drain()`，直到返回空指针；内部用 `swr_convert(..., nullptr, 0)` 排尾样本，不能只看 `swr_get_delay` 是否为零。第一次 drain 起禁止再输入，耗尽后重复 drain 安全返回空。未输入就 drain 也是合法的。
- 析构仅释放、不隐式 drain：要保留尾样本必须显式消费 drain 返回值。转换阶段 FFmpeg 失败或 drain 阶段任何异常把实例标为 Failed；需要重建。convert 前置校验/分配失败尚未消耗输入。

### 时间轴边界

`rescaleTimestamp(pts, from, to)` 校验正时间基、最近整数舍入（半数远离零），保持 `AV_NOPTS_VALUE`，溢出显式异常。AVFrame 自带 `time_base` 不作为输入权威值，统一使用格式描述中的时间基。

音频视为一个连续 segment：首个输入 PTS 锚定首个输出样本，后续输出 PTS = 换算后的锚点 + 累计实际输出样本数，drain 延续同一时间轴，不逐块重舍入输出 PTS、不额外扣重采样延迟。后续输入 PTS 必须符合“首 PTS + 累计输入样本时长”（允许一输入 tick 的量化误差）；已知/未知不能混用。若首 PTS 未知，所有输出保持未知。时间轴跳变需调用者 drain 并新建 segment，不自动补静音、丢样本或回零。

这里没有 QPC 采集、设备时间映射、暂停扣除、漂移补偿、generation 或同步队列策略。调用者提供的 PTS 应已由上游统一时间轴处理，转换器只做单位转换与连续样本计数。

## 实测与未覆盖范围

独立 MinGW Release 实测：CTest **1/1**、内部 **8/8 测试组**通过；日志明确显示 `NDEBUG` 已定义，所有判断使用不会被 Release 禁用的 `CHECK`。

- 合成红/绿/蓝/白色块：Y/U/V 数值校验，tight/padded/negative stride 输出逐像素一致；固定尺寸、不缩放、下采样和上采样；输出独立所有权。
- custom channel layout 构造/copy/assignment/move 深拷贝，原始 map 释放后继续使用。
- 44100 stereo 样本 → 48000 FLTP：drain 前 47983，尾部 17，合计精确 48000；验证声道值、PTS、duration 和重复 drain。
- 12 种 PCM 格式、初始 buffering、未知/负 PTS、空 drain、非法格式/尺寸/采样率/layout/plane/stride、时间戳跳变及换算溢出。

这些是合成 CPU 单元测试，未做 OOM/底层 FFmpeg 失败注入、sanitizer 或长时资源压力测试。C-02 的现有 filter/线程替换、queue/encoder 对接、真实采集输入及 BGRA→编码端到端验收均未接入；主工程构建、暂停同步、真实设备与录制输出也未验收。台账状态保持不动。
