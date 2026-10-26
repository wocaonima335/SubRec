# Capture lifecycle 控制面回归测试

**前十组是 fake opener 控制面测试，第十一组是真实 Session 的早期格式失败子集；不是完整 R-01 验收。** 这些测试需要在 Windows 上构建并运行验证。任何一组输出 PASS 都不能代替整个进程正常退出。

## 接入与运行

依赖现有项目模块、Qt 6 和 FFmpeg。保留生产代码已有的全局 `CaptureLifecycleTest` friend，以及 config/opener hooks；不要求新增测试接口。

根 `CMakeLists.txt` 在测试子目录之前启用测试。现有 `SUBREC_BUILD_LIFECYCLE_TESTS` / `WIN32` 条件及 `add_subdirectory(tests/capture_lifecycle)` 保留，不要重复添加子目录：

```cmake
enable_testing()
```

本目录注册 `capture_lifecycle_tests`，设置 **CTest TIMEOUT=45 秒**。Windows CTest 环境向 PATH 前置当前 Qt、FFmpeg 和编译器运行库目录，不依赖 appSubRec 的部署步骤。主工程配置完成后，在构建树所在的仓库根目录执行：

```bash
cmake --build build/windows-mingw-release --target capture_lifecycle_tests
ctest --test-dir build/windows-mingw-release -R '^capture_lifecycle_tests$' --output-on-failure
```

以上路径针对现有 windows-mingw-release 构建目录；如果使用其他构建树，应使用该构建树。不要并发构建共享主 build。

直接运行 exe **没有进程级超时保护**，不推荐用于死锁回归。测试内部的 5 秒等待只用于 worker 进度断言，无法中断控制线程上的 join/析构死锁。CTest 超时必须判失败，不能用 watchdog、detach、强制退出或跳过析构伪造成功。

## 前十组 fake 测试（范围不变）

| 组 | 验证内容 | 防止误判的关键安排 |
| --- | --- | --- |
| cancel_during_preparing | requestStop 只取消、不 join；取消后不回调 | worker 忽略取消直至主线程放行门闩；stop join 后才检查回调数为零 |
| stop_joins_outside_lock | stop 在状态锁外 join | worker 必须先观察取消，再调用 state；必须观察到 Stopping |
| queued_old_generation_across_restart | 旧代成功、失败都不能影响新代 Preparing | 使用真实 startup completion 投递，等待投递完成但不泵事件，stop/start 后才派发；事件过滤器确认旧 MetaCall 确实执行 |
| same_generation_success_failure_guards | Running/Failed 拒绝同代失败与成功重放 | 分别派发两个结果，每次检查状态、错误和 generation，避免最终状态掩盖中间错误 |
| restart_directly_from_failed | Failed 可直接 start 并释放旧 session | 不显式 stop；检查旧 worker 完成、新 generation 和 Running |
| double_stop | 重复 stop 幂等 | 确认 session 清空、worker 完成且未投递结果 |
| paused_rejects_start_and_configuration | Paused 拒绝 start、切换源、选择设备和刷新 | friend 预置有效设备，避免“无效 ID”造成假通过；检查 generation、session 和配置不变 |
| off_thread_control_rejected | 非 owner 线程控制不影响 Preparing | 工作线程调用 start/stop/pause/resume/change/select/refresh，join 后检查状态、generation、取消标志及返回值 |
| destructor_joins_blocked_opener | 析构取消并 join 被阻塞 opener | 不开放门闩，worker 只能靠析构取消退出 |
| destructor_discards_posted_callbacks | QObject 析构丢弃已投递 startup 回调 | 投递完成才析构；QPointer、MetaCall 计数及带 weak_ptr 的同接收者 sentinel 验证删除与销毁，再派发事件 |

非 owner 测试在 Preparing 状态执行：它检查组合操作的拒绝/无副作用，不单独证明 pause/resume 每个入口都有 owner-thread guard（状态 guard 也能拒绝它们）。本测试不把这一覆盖边界写成更强结论。

## 第十一组：真实 Session 格式失败子集

`real_session_input_format_failure_twice` 在同一 scheduler/recorder 上重复两次 start → Failed → stop → Idle。config hook 返回配置成功，video/audioInput 均指定不存在的 format，音频角色明确为 SystemAudio；opener hook 清空。运行前用 `av_find_input_format` 确认格式不存在，否则在启动前失败，不接触真实设备。

实际路径是 `FFRecorder::startRecord → FFOpenSourceEvent → Demuxer InputFormat 失败 → CaptureSession::abortPipeline → queued Failed`。当前 opener 先初始化音频，所以失败发生在音频格式查找阶段，早于设备 open、视频准备和 mux 输出。测试泵事件等待 Failed，检查准确 lastError、recorder.isRecording 为 false、session 未 started；stop 后检查 Idle、错误清空和 session 释放。输出使用唯一候选文件名，仅检查不存在，不创建或删除输出文件。

这组覆盖实际早期准备失败的 abort 返回及重复启动，不覆盖已打开输入、已初始化 decoder、运行中的 filter/encoder/mux 回滚。scheduler 只暴露错误文本，不暴露结构化 stage；InputFormat 路径由不存在格式的前置检查及现有 Demuxer 分支限定，不声称直接断言了 scheduler 的结构化错误载荷。新增用例若 CHECK 失败仍走 Fixture RAII；若 abort/stop 死锁，现有 CTest TIMEOUT=45 秒必须判失败。

## 失败与清理

- 每组独立拥有 scheduler、worker 观测数据和门闩，不共享上轮的静态 Fixture 或全局完成计数。
- CHECK 失败正常 return 并触发 RAII：先释放所有测试门闩，必要时恢复 Paused，再析构 scheduler、join worker；丢弃该对象的待投递回调，派发 recorder 通知并清空 UI error。
- 人工创建的 off-thread 工作线程在任何 CHECK 前 join；没有脱离线程引用局部变量。
- `main` 正常调用 `FFRecorder::destoryInstance()`，返回后才输出 SUMMARY；随后正常析构 QCoreApplication。清理若死锁，由 CTest TIMEOUT 判失败，不能宣称后续组已经隔离通过。
- 出现 TEARDOWN 而没有 SUMMARY，重点检查 recorder/底层组件析构；出现全部组 PASS 但 CTest 超时仍然是失败。

## 不证明什么

前十组 fake 分支绕过 `FFRecorder::startRecord()` 和真实 opener。它不设置真实运行所用的 `m_cleanupNeeded`，因此不能证明真实输入 open 的可中断性、设备/驱动阻塞退出、部分初始化失败的资源回滚，也不能证明 decoder、filter、encoder、mux 的停止/唤醒/join 顺序、trailer 写入、输出文件完整性或资源泄漏不存在。

即使底层对象初始化和最终析构实际发生，也不等于真实媒体流水线曾经运行。不得用本测试通过把包含真实 open/decoder/filter/mux rollback 的 R-01 或其他完整生命周期条目标为 done。需补充真实或可控媒体故障注入、流水线 drain/abort、文件结果及资源释放证据。本轮不修改验收台账。
