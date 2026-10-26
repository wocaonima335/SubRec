# WASAPI loopback 系统声音源：接口设计

状态：**只有设计，没有实现**。接口头文件 [`capture/wasapi_loopback_capture.h`](../capture/wasapi_loopback_capture.h)
不在任何 `CMakeLists.txt` 里，也没有被 include。实现交给后续任务（Codex 终端）。原始 API
调用的参考实现是 [`tests/native_capture_probe/main.cpp`](../tests/native_capture_probe/main.cpp)
的 `wasapi` 模式（`runWasapi`）。

目标：用 Windows 自带的 WASAPI 共享模式 loopback 取代第三方 DirectShow 组件
`virtual-audio-capturer`，录系统声音不再需要用户另装组件。麦克风和摄像头继续走 DirectShow。

## 1. 放在现有音频链的哪里

现在（`event/ffopensourceevent.cpp` `initAudio` / `startAudio`）：

```
dshow "audio=virtual-audio-capturer"
  -> Demuxer (aDemuxer[A_AUDIO]) -> aPktQueue[A_AUDIO]
  -> FFADecoder + FFAResampler (PCM -> 48 kHz stereo FLTP) -> aFrmQueue[A_AUDIO]
  -> FFAFilterThread (system 分支, processSingleAudioFrame) -> aFilterEncoderFrmQueue
  -> FFAEncoderThread -> RecorderSyncQueue -> aac -> FFMuxer
```

之后：

```
wasapi::LoopbackCapture (自带线程)  ----------------------->  aFrmQueue[A_AUDIO]
  -> FFAFilterThread / FFAFilter / FFAEncoderThread / RecorderSyncQueue（都不改）
```

和这次的 ddagrab 屏幕源（`thread/ffscreengrabthread.cpp`）同一个思路：新源直接产出解码器
输出格式的帧，写进解码器的帧队列，跳过 demuxer + decoder；后面的链路完全不动。

## 2. 输出格式（硬约束）

| 项 | 值 | 原因 |
| --- | --- | --- |
| 采样格式 | `AV_SAMPLE_FMT_FLTP` | `filter/ffafilter.h` `AUDIO_SAMPLE_FORMAT`；FFADecoder 现在也输出它 |
| 采样率 | 48000 Hz | `AUDIO_SAMPLE_RATE`；`FFAEncoderThread::initEncoder` 用第一帧的参数打开 AAC |
| 声道 | 立体声，`AV_CHANNEL_LAYOUT_STEREO` | 与 `FFAudioSampler` / FFAFilter 一致；设备是 5.1/7.1 时由 swresample 下混 |
| 每帧样本数 | 建议 480（10 ms），任意正数都可以 | `FFAEncoder::encode` 自己重新切成 AAC 帧大小 |
| `pts` | 1/48000 时间基，第一帧 0，之后严格等于已输出样本数 | 见第 4 节 |
| 所有权 | `AudioFrameSink::push` 拿走引用（`av_frame_move_ref` 语义） | 与 `FFAFrameQueue::enqueue` 相同 |

转换用现成的 `capture::media::AudioConverter`（`capture/media/media_conversion.h`）：它接受
U8/S16/S32/S64/FLT/DBL 的 packed/planar 输入，固定输出 48000/stereo/FLTP，并保证 pts 连续
（“首 PTS + 累计样本数”）。共享模式的 mix format 通常是 32 位 float（`WAVE_FORMAT_EXTENSIBLE`
+ `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`），映射为 `AV_SAMPLE_FMT_FLT`；声道布局取
`WAVEFORMATEXTENSIBLE::dwChannelMask`（mask 为 0 时用 `av_channel_layout_default`）。
`AudioConverter` 的输入描述必须带真实布局，不接受只有声道数的 UNSPEC。

## 3. 线程模型

- 一个录制会话一个 `LoopbackCapture`，内部一个采集线程。所有 COM 对象
  （`IMMDeviceEnumerator`、`IMMDevice`、`IAudioClient`、`IAudioCaptureClient`、事件句柄）都在
  这个线程上创建、使用、释放；线程开头 `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`，结束时
  `CoUninitialize`（可复用 `thread/wincomscope.h` 的 `ScopedWinComInitializer`）。这与探针的规则
  相同，也和 FFScreenGrabThread “在同一线程上打开和读取”一致。
- 读取方式：优先事件驱动（`AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK`，
  `WaitForSingleObject(event, 100 ms)`）。Windows 10 之前 loopback 不支持事件回调
  （*待核实*：最低版本以 Microsoft 文档为准）；`Initialize` 失败时退回探针的 5 ms 轮询
  （`GetNextPacketSize` + `Sleep`）。
- 每次 `GetBuffer` 成功后必须 `ReleaseBuffer`（RAII，见探针的 `AudioPacket`）；
  `AUDCLNT_S_BUFFER_EMPTY` 不调用 `ReleaseBuffer`；`AUDCLNT_BUFFERFLAGS_SILENT` 时**不读**数据指针，
  按零样本处理。
- 推入 sink 可能阻塞（队列满 30 帧）。阻塞期间 WASAPI 缓冲继续填充，缓冲 100 ms；
  长时间阻塞会溢出（下一包带 `DATA_DISCONTINUITY`），计入 `stats.discontinuities`，不中断录制。
- `stop`/`abort` 只设置标志并 `SetEvent`，不在调用者线程上碰 COM 对象。

## 4. 时钟与时间戳

现状：`RecorderSyncQueue` 以**音频为主时钟**：它按累计样本数给音频打 pts
（`queue/recordersyncqueue.cpp` `m_nextAudioPts`），视频帧按音频时钟丢/补。输入帧自带的 pts
不被采用。由此得到两条约束：

1. **样本流不能有空洞。** loopback 在没有任何声音播放时**完全不出包**（探针 README 也写了：
   默认端点空闲时可能没有 loopback packet）。如果不补，音频时钟就停住，视频会被压住或丢弃。
   `fillIdleWithSilence = true`（默认）：采集线程用 QPC 计算“从 start() 到现在应有的样本数”，
   和已输出样本数比较，差值超过 20 ms 就补静音帧（每次最多补到当前时刻，不提前）。
   `virtual-audio-capturer` 据其上游说明也是没有声音时输出静音（*未在本仓库核实*）；补静音后
   替换前后对同步队列的行为一致。
2. **pts = 已输出样本数（1/48000）。** 设备包的 `qpcPosition`（`GetBuffer` 的第 5 个参数，
   100 ns 单位的 QPC 时间）只用于：
   - 计算第一包相对 `start()` 的延迟，写日志；
   - 检测漂移：`|(qpcPosition - startQpc) * 48000 / 10^7 - 已输出样本数|` 超过 100 ms 时记日志并
     计数（实现可选：补静音或丢样本对齐，第一版只记日志）。
   `AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR` 的包不参与漂移计算。
- 与视频的对齐：开始录制时 `FFOpenSourceEvent::openAndStartResult` 设
  `aEncoderThread->setStartTimeUs(t0)`、`vEncoderThread->setStartTimeUs(t0)`（`av_gettime_relative`）。
  `startQpcUs()` 返回第一样本对应的时间，实现中先测一次 `QueryPerformanceCounter` 与
  `av_gettime_relative()` 的差值并保存，所以两个时钟是否同源（MinGW 下 `av_gettime_relative` 走
  winpthreads 的 `clock_gettime(CLOCK_MONOTONIC)`，*未核实*是否就是 QPC）不影响结果。
- 暂停：不在源里处理。`FFAFilterThread` 暂停时丢帧、`RecorderSyncQueue::onPauseChanged` 处理
  时间轴，和 dshow 源一样。源在暂停期间照常输出（包括补静音）。

## 5. 生命周期、错误处理

```
LoopbackCapture cap(config);
open(sink, 5 s)      -> 线程: COM 初始化, 选端点, Activate, GetMixFormat, Initialize, GetService,
                        建 AudioConverter; 成功 = Prepared, 失败 = Failed + CaptureStartResult
start()              -> IAudioClient::Start, Running; 记录 startQpc
requestDrainStop()   -> 停读, AudioConverter::drain() 直到 nullptr, push 尾部, sink->finish()
requestAbort()       -> 停读, 不 finish（调用者关闭队列）
join()               -> IAudioClient::Stop, 释放 COM, CoUninitialize; Idle
```

| 情况 | 处理 |
| --- | --- |
| 没有活动的输出设备（`E_NOTFOUND` / `GetDefaultAudioEndpoint` 失败） | `open` 失败，`CaptureErrorStage::InputOpen`，提示“没有播放设备；可改用麦克风或安装 virtual-audio-capturer” |
| mix format 无法转换（`AudioConverter` 构造抛 `invalid_argument`/`FfmpegError`） | `open` 失败，`InputFormat`，日志里带 mix format 全部字段 |
| `AUDCLNT_E_DEVICE_INVALIDATED`（拔掉耳机、切换默认设备、驱动重启） | 释放旧 client，按配置重开（默认端点或同一 id），最多重试 10 次、间隔 200 ms；期间按 QPC 补静音，时间轴不断；`onRuntimeError(recovered=true)`。失败则 `Failed`、`sink->finish()`，`onRuntimeError(recovered=false)` → `FFRecorder::setCaptureError`（与 ddagrab 丢失时相同的 UI 路径） |
| 默认设备变化（`IMMNotificationClient::OnDefaultDeviceChanged`，`followDefaultDevice`） | 回调里只设标志（回调线程不能做 COM 重操作），采集线程按上一行的流程换设备 |
| `AUDCLNT_E_SERVICE_NOT_RUNNING`（Audiosrv 停止） | 同“设备失效”，重试耗尽后失败 |
| 打开超时（5 s） | 返回失败；之后 `requestAbort()` + `join()`，与 `FFScreenGrabThread::prepareOpenAndWait` 相同 |
| 用户取消（`shouldStart` 返回 false） | 与其它源一样由 `FFOpenSourceEvent::checkContinue` 处理，`requestAbort()` |

错误对象：`CaptureError::backend` 需要新值 `CaptureBackend::Wasapi`（加到
`capture/capture_types.h`，`captureBackendName` 返回 `"WASAPI"`），HRESULT 放进
`CaptureError::ffmpegError`（负值），`ffmpegMessage` 用 `FormatMessageW` 文本。

## 6. 接入步骤（给实现者）

1. 实现 `capture/wasapi_loopback_capture.cpp`（`Impl` 内部用探针的 RAII 类型：`ComPtr`、
   `TaskMemory`、`AudioStream`、`AudioPacket`），加入 `capture_module`，`WIN32` 时链接
   `ole32 uuid`（探针还用了 `propsys`，取友好名称需要）。
2. `capture/capture_types.h`：`CaptureBackend::Wasapi`。新建 `WasapiCaptureBackend : ICaptureBackend`，
   `enumerate()` 列出一个 `SystemAudio` 设备（id `wasapi:default`，以后可加每个端点
   `wasapi:<endpoint id>`），`makeInputRequest` 填 `backend = Wasapi`。
3. `CaptureScheduler`：仿照屏幕的 `ScreenBackendPreference` 加系统声音后端选择
   （环境变量建议 `SUBREC_SYSTEM_AUDIO_BACKEND=auto|wasapi|dshow`，auto = wasapi）。
   `buildCurrentConfig` 按所选设备的 backend 生成请求；若 dshow 的 `virtual-audio-capturer`
   存在，放进一个 `audioFallbackInput`（与 `videoFallbackInput` 同样的回退）。
4. `FFRecorderPrivate`：加 `systemAudioCapture`（或一个包装线程类，和 `screenGrabThread` 一样
   放在 `thread/`），`FFEvent` 里取到指针。
5. `FFOpenSourceEvent::initAudio`：`audioSourceType == AUDIO && audioInput.backend == Wasapi` 时
   不初始化 `aDemuxer/aDecoder`，改为 `open(sink(aFrmQueue[A_AUDIO]))`；失败且有回退时切回
   dshow 路径。`aFilter->initalize(...)` 仍传 decoder 指针（单源路径不使用它们）。
   `startAudio()`：调用 `start()` 而不是启动 demuxer/decoder 线程。
6. `FFCloseSourceEvent::stopAudioInputPath`：`sourceType == AUDIO` 且 WASAPI 活动时先
   `requestDrainStop()` + `join()`（产生 EOF），再走现有 demuxer/decoder 的空操作。
   `CaptureSession::abortPipeline`：`requestAbort()` + `join()`。
7. 测试：
   - `tests/` 新增单元测试，用假的 `AudioFrameSink` 和注入的 PCM 包（把 WASAPI 读取抽成
     接口）验证：FLT 48k/44.1k/5.1 → 48k stereo FLTP、pts 连续、静音补齐、drain 尾部、
     abort 不调用 `finish()`。
   - PC2：`tools/pc2_verify.ps1` 增加一次“播放一段声音 + 录 5 s”的 autotest
     （`SUBREC_SYSTEM_AUDIO_BACKEND=wasapi`），检查 mp4 里有 aac 音轨且时长约 5 s；再测一次
     无声播放（验证静音补齐、视频时长不变短）。

## 7. 不在第一版范围

- 独占模式、进程级 loopback（`ActivateAudioInterfaceAsync` + `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`，
  Windows 10 2004+）：以后可按应用录音。
- 系统声音 + 麦克风混音：要走 `FFAFilter::processDualtAudioFrames`（双路图已在
  `tests/audio_filter_dual` 中测试），需要 `FFAFilterThread` 同时读两个队列并按时间对齐，单独设计。
- 设备采样率改变（用户在声音设置里改格式）：按“设备失效”重开，新 mix format 需要新的
  `AudioConverter`；时间轴连续由“补静音 + 样本计数”保证。
