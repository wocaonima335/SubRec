# AI 字幕：工作包拆分（M0 之后）

依据：`design-live-v2.md` §10.4（工作包、依赖与并行）、§11（测试方案）；用户已批准 D1–D35 全部按推荐执行；D13 暂时只用 PC3。
负责方只有两种：**Codex 终端**、**ZCode 终端**。每个模块只归一方；不归自己的文件不改（需要改别人的头文件 → 提给对方，双方同意后由 M0 头文件的维护规则处理，见 §4）。

## 1. 模块 → 负责方（一模块一方）

| 包 | 模块 / 文件（独占路径） | 负责方 | 里程碑 |
|---|---|---|---|
| M0 | `subtitle/*.h`、`subtitle/*/*.h`（接口冻结）、`subtitle/**/CMakeLists.txt` 骨架、`subtitle/m0_check/`、`asr/` 骨架、本目录文档 | 已完成（本提交） | M0 |
| A | AudioTap `subtitle/tap/audio_tap.cpp`；MonoResampler16k、PcmSpool、IngestWorker `subtitle/audio/*.cpp`（替换 `audio_stubs.cpp`）；`tests/subtitle_tap`、`tests/subtitle_audio` | Codex 终端 | M1 |
| B | TextNormalizer、HallucinationFilter、CueBuilder、SubtitleWriters `subtitle/core/{text_normalizer,hallucination_filter,cue_builder,subtitle_writers}.cpp`（替换 `core_stubs.cpp`）；`tests/subtitle_core` | Codex 终端 | M1 |
| K | Glossary、EventTags、ItnExtra `subtitle/core/{glossary,event_tags,itn_extra}.cpp`（替换 `rules_extra_stubs.cpp`）；`tests/subtitle_rules` | ZCode 终端 | M2/M3 |
| H | LocalAgreement、SentenceFixer、SubtitleDocument、SubtitleMerger `subtitle/live/{local_agreement,sentence_fixer,subtitle_merger}.cpp`（替换 `merger_stubs.cpp`）；`tests/subtitle_live` 中 H 的用例 | Codex 终端 | M1 |
| W | DegradeController、HostWatchdog `subtitle/live/{degrade_controller,host_watchdog}.cpp`（替换 `degrade_watchdog_stubs.cpp`）；`tests/subtitle_live` 中 W 的用例 | Codex 终端 | M1 |
| C | subrec-asr 全部：`asr/**`（协议 IO、SpoolTailReader、段状态机、partial 调度、sherpa 引擎、假引擎、父进程看门、fake 宿主）；**协议编解码 `subtitle/host/asr_protocol.cpp`**；`packaging/asr/**`；`tests/asr_host` | ZCode 终端 | M1（Whisper 引擎 M2） |
| S0 | PC3 Spike：MSVC 宿主原型跑 SenseVoice + Silero VAD 尾随增长中的 WAV，测 RTF/partial 成本/RSS/启动耗时 | ZCode 终端 | M1 前 |
| D | AsrHostClient + 工厂 `subtitle/host/asr_host_client.cpp`；SubtitleService、SubtitleJobRunner `subtitle/service/{subtitle_service,subtitle_job_runner}.cpp`（替换 `service_stubs.cpp`）；`tests/subtitle_host`、`tests/subtitle_service` | Codex 终端 | M1 |
| R | RefineScheduler `subtitle/service/refine_scheduler.cpp`；`tests/subtitle_refine` | Codex 终端 | M2 |
| E | ModelManager `subtitle/models/**`、`models.json`；`tests/subtitle_models` | ZCode 终端 | M1 |
| F | UI：`ui/backend/subtitle_*` 转发（U8）、`currentLine`/降级提示、悬浮条当前句、设置项 `ui/qml/**` 新文件、`FloatingBar.qml` 局部 | ZCode 终端 | M1 |
| G | 集成挂接点 I1–I10（录制主链路热点文件，唯一允许改录制代码的包）、`tests/subtitle_pipeline`、AutoTest 场景、CI（I7） | Codex 终端 | M1 末 |

说明：
- K 在设计 §10.4 写「Codex 或 ZCode」，这里定为 ZCode，以便与 Codex 主线并行。
- 协议编解码 `asr_protocol.cpp` 两边共用，定为 C（ZCode）独占：宿主最先需要它；D 只使用，不改。
- 同一 `CMakeLists.txt` 里有两个包的文件时（`core/`：B、K；`live/`：H、W；`service/`：D、R），文件头注释写明维护方，另一方只改自己那几行。

## 2. 依赖顺序

```
M0（完成）
 ├─ Codex 主线：A ─▶ B ─▶ H ─▶ W ─▶ D ─▶ G ─▶ PC3 验收
 │                       （H 需要 B 的 normalize/cue 实现才能跑真实用例；H 的单测可先对桩写）
 │                                     （D 需要 H、W；需要 C 的 fake 宿主或 fake 引擎）
 │                                                          R（M2：需要 D、C 的 refine 命令与 Whisper 引擎）
 └─ ZCode 主线：S0 ─▶ C（先交付：协议编解码 + fake 引擎 + fake 宿主）─▶ E ─▶ F ─▶ K（M2）
交汇点：M0 头文件；docs/subtitle/ipc-protocol.md；fake 宿主脚本格式与 --fake 参数（asr/fake_host/fake_host_script.h、asr/src/engine_fake.h）。
```

关键时间约束：**C 的「协议编解码 + 假引擎 + fake 宿主」必须早于 D 开始联调**；ZCode 应在 S0 之后优先交付这三项（不依赖 sherpa），再做 sherpa 引擎。

## 3. 验收用例（每包合入前必须通过）

所有包共同要求：
- 本仓库默认配置（`SUBREC_BUILD_SUBTITLE=OFF`）下 `appSubRec` 构建与行为不变；G 之前任何包不得改录制主链路文件。
- `-DSUBREC_BUILD_SUBTITLE=ON` 时 `subtitle_header_check_pure` / `subtitle_header_check_qt`（每个公共头单独编译）与 `subtitle_m0_link_check` 通过；新增公共头自动纳入检查（`m0_check/CMakeLists.txt` 用 GLOB，Qt 头需登记到列表）。
- 新测试放 `tests/subtitle_*`，按 `cmake/SubRecTests.cmake` 规则：不带 `device` 标签即自动 `ci`；需要真实模型的加 `needs_asr_model`（只在 PC3 跑）。CI 测试不解码 MP4，音频一律合成或读 WAV。

| 包 | 验收用例（CI，除注明外不需要模型） |
|---|---|
| A | SPSC 压力（生产者 10⁶ 块、消费者随机延迟）无丢失/重复，环满丢最新并计数；`maxPushNs` 统计；splice 在暂停开始与恢复两个方向都带出；暂停后无新块时 `spliceRequested()` 被 IngestWorker 上报为 `committed16k`；FIR 冲激位置误差 ≤ 1 个 16k 样本；pts 空洞补零且 `gaps/gapSamples` 正确；`publish` 单调且 ≤ 已写；spool 往返（写→读 WAV 头与样本）；磁盘写失败 → `lastError` 且继续消费 tap |
| B | golden SRT/VTT（BOM、CRLF、空文本跳过、>99 小时）；码点/有效字符计数；规整去 `<\|...\|>` 标签与语气词；幻觉黑名单/重复/字速；CueBuilder 硬边界不跨越、标点/字数/时长切分；`resolveOverlaps` 单调；`writeFileAtomic` 中途失败时原文件完好 |
| K | 词表拼音近似替换的命中与不误替换样例；`glossaryMiss`；prompt 长度截断；事件标签映射（未知事件不加标签）；ITN 规则为空时原样返回 |
| H | **LocalAgreement**（表驱动）：n=2/n=3 确认时机；300 ms 护栏；标点随前 token 确认；改口不回退且 `regressions`+1；splice/新段/final 后重置；`freeze()` 后不增长；`mismatchAgainst` 编辑距离；无 tokens 兼容路径。**Merger/Document**：final 幂等、乱序/重复 final、resume 后编号连续、`shouldFlushPart` 节流、`resumeFromPart` 往返、`flushPart` 中途失败原 `.srt.part` 完好；临时 cue（开关开：≥ 3 s 且以句中标点结尾才写，final 替换，幻觉时删除；开关关：永不出现）。**SentenceFixer**：可修订窗口只改最近 2 条；跨句合并的间隔/字数边界；录完模式全篇可修订；`acceptRefined` 对 UserEdited 返回 false |
| W | **DegradeController**：注入 `LoadSample` 序列逐条验证 §6.3 每条迁移、持续时间、迟滞、10 s 最小间隔；录制健康信号立即触发 L3；L3 不自动回升；`forceLevel`。**HostWatchdog**（ManualClock）：hb 5 s 超时、按 RTF 3 的段超时、加载 60 s 超时、退避 1/2/4/8/16 s（上限 30 s）、10 分钟 5 次耗尽、同区间两次 → poison、GPU 后端崩溃 → forceCpu、不重试退出码 |
| C | 协议 `encode`/`decode` 对每种消息往返；超长行、非 JSON、未知 type（NotSupported）、缺必需字段（HostProtocolError）；`--fake` 参数解析与所有故障注入（crashAtSeg/hangAtSeg/hangAll/oomAtSeg/illegalInstr/garbageStdout/loadFail/partialNoise/partialRegress/finalDiffers/lang/refineEmpty）；fake 宿主脚本回放；合成 WAV + fake 引擎：splice 强制封口、maxSegment 强切、resumeFrom16k 续识别、`setLevel` 关 partial；stdout 重定向后协议不被污染。`needs_asr_model`（PC3）：真实 SenseVoice 尾随增长 WAV 的关键词与 RTF |
| S0 | PC3 手工报告：RTF（1 线程、BELOW_NORMAL）、partial 成本、RSS/PeakProcessMemoryUsed、启动与加载耗时、Silero v4/v6 A/B |
| D | fake 宿主回放：握手版本不匹配、行超长、非 JSON；`exited(crashed)` → 重启计划；Job Object 赋值成功且父进程退出后子进程被杀（Windows）；**Service**（fake 引擎 + 合成 tap）：rtf=0.1 时 final 按时到达、停止后定稿；rtf=0.8 → L1、rtf=1.5 → L2/L3 且停止后补完 SRT 完整；crashAtSeg=3 → 自动恢复且无重复无遗漏；hangAtSeg=3 → 段超时恢复；连续 crashAtSeg → poison 只缺该段；oomAtSeg → L1 再 L3；illegalInstr → unavailable 不重试；`endRecording(false)` 不写 `.srt`；`shutdown` 超时 → incomplete；属性 `currentLine` 节流 ≤ 4 Hz |
| R | 路由（zh 低 ASCII 跳过、en/混说送精修、`refineAll`、热词缺失）；逐句替换按 segmentId 不重复不漏；refineEmpty → 615；UserEdited → 613；整篇 `.srt` 被编辑不覆盖；`interrupt()`/`resume()` 断点续跑（614）；精修实例崩溃退避、耗尽 → 712 且初稿完好 |
| E | 本地 `QTcpServer` 模拟 Range 续传、断线、哈希错、镜像切换；许可确认；删除；`engineRequestFor`；属性/方法名与 `ui/backend/model_catalog.cpp` 反射约定一致 |
| F | `tests/ui`：SubtitleView 转发新属性、降级徽标文案、悬浮条当前句受设置开关控制、暂停时清空 |
| G | `tests/subtitle_pipeline`：真实 `FFAEncoderThread` + 假 muxer + 假宿主，cue 起点误差 ≤ 30 ms、不跨拼接点、暂停开始后 ≤ 1 s 出 final；宿主挂起时编码线程吞吐变化 < 5 %；AutoTest `record_seconds_subtitle_live`；PC3 上 §5.4 L-R1–L-R9、L-T1–L-T4、L-Q1/L-Q2 |

## 4. 接口变更规则

- M0 头文件（`subtitle/` 下全部 `.h`）冻结：公共签名、结构体字段语义、错误码数值、协议字段只追加不修改；追加也要双方同意，并同步 `design-live-v2.md` §10 与本目录文档。
- 头文件中标「私有成员归实现方，可自由调整」的 `private:` 部分不受冻结约束。
- `asr/src/` 下除 `engine_fake.h`（参数名）与 `asr/fake_host/fake_host_script.h`（脚本格式）外，均为 C 包内部接口，ZCode 可自行调整。
- 桩文件（`*_stubs.cpp`）由对应包实现时删除并拆分为同名 `.cpp`，同时更新本子目录 `CMakeLists.txt` 中自己的行。

## 5. 本地检查命令

```
# subtitle 独立构建（无 FFmpeg；有 Qt6 Core 时同时构建并检查 Qt 目标）
cmake -S subtitle -B build-subtitle -G Ninja && cmake --build build-subtitle && ctest --test-dir build-subtitle
# 仓库内（Windows，与主程序同配置）
cmake --preset windows-mingw-release -DSUBREC_BUILD_SUBTITLE=ON && cmake --build build/windows-mingw-release --target subtitle_m0_link_check subtitle_header_check_pure subtitle_header_check_qt
# asr 子进程（MSVC 或任意 C++17 编译器）
cmake -S asr -B build-asr && cmake --build build-asr
```
