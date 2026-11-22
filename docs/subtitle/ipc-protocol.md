# AI 字幕：主进程 ⇄ subrec-asr IPC 协议（protocol = 2）

状态：M0 冻结（2026-11-22）。依据 `design-live-v2.md` §7.2–§7.6、§10.3 C、§11.1；上版 `design-verified.md` §11.3 C。
代码契约：`subtitle/host/asr_protocol.h`（结构体、常量、`encode`/`decode`），本文与之逐字段一致；任何一方修改都要同时改另一方，并经 Codex 终端与 ZCode 终端双方同意。

## 1. 通道与通则

- 通道：`QProcess` 的 stdin（主→子）/ stdout（子→主）匿名管道。子进程启动第一件事：`_dup(1)` 保存协议句柄，再 `_dup2(_fileno(stderr), 1)`，第三方库的任何 printf 都进 stderr；stderr 由主进程限速记日志。
- 编码：UTF-8 JSON，一行一个对象，以 `\n` 结尾。单行上限 `kMaxLineBytes` = 1 MiB（不含 `\n`），超限视为 `HostProtocolError`(703) → 主进程重启子进程。
- 每个对象有字符串字段 `type`。新增字段一律可选：接收方忽略不认识的字段；缺省字段取 `asr_protocol.h` 结构体默认值。
- 不认识的 `type`：`decode` 返回空并给 `NotSupported`(104)；接收方记日志后忽略（**不**视为协议错误，便于向前兼容）。必需字段缺失或类型错误：`HostProtocolError`(703)。
- 数字：JSON number。`jobId`、16k 样本号、毫秒都必须 < 2^53（主进程用 `QJsonDocument`，以 double 存数）。
- 时间：带 `16k` 后缀的字段是 spool 的 16 kHz 样本号，区间半开 `[b, e)`；`tokens` / `words` 里的 `b`、`e` 是**成片绝对毫秒**（= 16k 样本号 / 16）。
- 版本：`kProtocolVersion = 2`。`hello` 版本不一致 → `HostVersionMismatch`(704)，不重试。
- 错误码：`error.code` 与子进程退出码使用 `subtitle/subtitle_error.h` 的 `ErrorCode` 数值。

## 2. 命令行参数

| 参数 | 取值 | 说明 |
|---|---|---|
| `--role=` | `live`（默认）/ `refine` | 同一 exe 两种角色；`refine` 实例只接受 `hello`/`load`/`refine`/`cancel`/`quit`（`proto::acceptedByRole`） |
| `--parent-pid=` | 父进程 pid | 父进程看门线程（`OpenProcess(SYNCHRONIZE)` + `WaitForSingleObject`），父进程消失即自杀 |
| `--engine=` | `sherpa-onnx` / `whisper.cpp` / `fake` | 可被 `load.request.engineId` 覆盖 |
| `--fake=` | `k=v,...` | 假引擎参数，见 §8；仅测试钩子开启时生效 |
| `--log-level=` | `error`/`warn`/`info`/`debug` | 影响 `log` 消息与 stderr |
| `--version` | — | 打印 `subrec-asr <ver> protocol=2` 到 stdout 后退出（不走协议，供安装检查） |

## 3. 主进程 → 子进程

| type | 字段 | 说明 |
|---|---|---|
| `hello` | `protocol`=2, `role` | 握手，启动后立即发送 |
| `load` | `request`{EngineRequest}, `live`{LiveConfig}, `fakeOptions`? | 回 `loaded` 或 `error` |
| `open` | `jobId`, `spoolPath`, `resumeFrom16k`, `committed16k`, `live` | 开始尾随 spool；`resumeFrom16k` 用于断点续识别（§7.4），VAD 从静音初始化；`live=false` 为 after / L3 补识别 |
| `append` | `jobId`, `committed16k` | 节流 200 ms（`LiveConfig.appendThrottleMs`），后发覆盖先发 |
| `splice` | `jobId`, `at16k` | 暂停开始与恢复都发；子进程在 `at16k` 强制封口并立即出 final，丢弃该段未发出的 partial |
| `setLevel` | `jobId`, `level`(0..4), `threads`, `partialIntervalMs` | 降级：L1 `partialIntervalMs=0`；L2 `threads=1`；L3 主进程改发 `pause` |
| `pause` | `jobId` | L3 暂停解码（spool 照常增长，子进程只保留握手与 hb） |
| `resume` | `jobId` | 录完恢复（主进程同时 `SetPriorityClass(NORMAL)`） |
| `finish` | `jobId`, `total16k` | 录制结束；子进程封口最后一段，全部 final 发完后回 `done` |
| `cancel` | `jobId` | 回 `cancelled` |
| `quit` | — | 正常退出（退出码 0） |
| `refine` | `jobId`, `spoolPath`, `segments`[{`id`,`b16k`,`e16k`}], `prompt` | M2，仅 `role=refine`：按给定边界逐句重识别，不跑 VAD；逐句回 `final`(pass=2)，全部完成回 `done` |

`EngineRequest` 的 JSON：`{engineId, modelId, modelDir(UTF-8), language, threads, backend:"auto"|"cpu"|"vulkan"|"cuda", vad:{threshold, minSpeechMs, minSilenceMs, speechPadMs, maxSegmentMs}, initialPrompt}`。
`LiveConfig` 的 JSON：与 `subtitle_types.h` 的成员同名（`partialIntervalMs, maxSegmentMs, endpointSilenceMs, appendThrottleMs, partFlushIntervalMs, threadsWhileRecording, agreementN, confirmGuardMs, writeConfirmedToPart, provisionalMinSegmentMs, revisableCues`）；子进程只使用前 3 项与 `threadsWhileRecording`，其余原样忽略。

## 4. 子进程 → 主进程

| type | 字段 | 说明 |
|---|---|---|
| `hello` | `protocol`, `version`, `backends`[...], `gpus`[{`name`,`vramMB`,`api`}] | 握手回复；M1 `backends=["cpu"]`、`gpus=[]` |
| `loaded` | `backend`, `loadMs`, `fallbackReason`? | GPU 失败自动回退时 `backend:"cpu"` 并带原因 |
| `hb` | `seq`, `state`, `rssMB` | IO 线程每 1 s 一次（解码中也发）；`state` ∈ `idle`/`loading`/`decoding`/`paused` |
| `busy` | `jobId`, `kind`, `b16k`, `e16k` | 每次解码**开始前**发；`kind` ∈ `partial`/`final`/`refine`；看门狗按 `samples = e16k − b16k` 算段级超时 |
| `partial` | `jobId`, `id`, `b16k`, `upto16k`, `text`, `tokens`[{`t`,`b`,`e`}], `seq` | 只用于显示；`seq` 本句内从 0 递增；旧宿主可不带 `tokens`（LocalAgreement 退化为按码点比较、无护栏） |
| `final` | `jobId`, `id`, `b16k`, `e16k`, `text`, `lang`, `emotion`, `event`, `words`[{`t`,`b`,`e`,`p`?}], `forcedCut`, `decodeMs`, `pass`, `modelId` | `pass=1` 实时 / `pass=2` 精修；`(jobId,b16k,e16k,pass)` 唯一，主进程幂等处理；`p` 缺省 = 无置信度 |
| `progress` | `jobId`, `done16k`, `decodeMsTotal` | 已终结到的样本；`decodeMsTotal` 供 RTF EWMA |
| `done` | `jobId` | `finish` 或 `refine` 全部完成 |
| `cancelled` | `jobId` | 回应 `cancel` |
| `error` | `jobId`(0 = 宿主级), `code`, `message` | 例：`EngineLoadFailed`(303)、`GpuDeviceLost`(322)、`SpoolReadFailed`(224) |
| `log` | `level`, `text` | 诊断日志，主进程转 `[ASR]` 日志 |

字段名映射到结构体：`lang` ↔ `FinalSegment.language`；`tokens`/`words` 的 `t`/`b`/`e`/`p` ↔ `Word.text`/`beginMs`/`endMs`/`prob`；`b16k`/`e16k` ↔ `begin16k`/`end16k`；`jobId` ↔ `job`。

## 5. 典型时序

实时录制：

```
主→子 hello{protocol:2, role:"live"}            子→主 hello{protocol:2, version, backends:["cpu"]}
主→子 load{request, live}                       子→主 hb … loaded{backend:"cpu", loadMs}
主→子 open{jobId:7, spoolPath, resumeFrom16k:0, committed16k:3200, live:true}
主→子 append{jobId:7, committed16k:6400}（每 200 ms）
                                                子→主 busy{kind:"partial", b16k, e16k} → partial{…, seq:0}
                                                子→主 busy{kind:"final", …} → final{pass:1, …} → progress{…}
主→子 splice{jobId:7, at16k}（暂停开始）        子→主 final{forcedCut:true, …}
主→子 setLevel{jobId:7, level:1, threads:1, partialIntervalMs:0}（降级）
主→子 finish{jobId:7, total16k}                 子→主 final … done{jobId:7}
```

崩溃恢复（§7.4）：主进程按 `HostWatchdog::onExit` 的退避计划重启 → `hello → load → open{resumeFrom16k = 最后一个 final 的 end16k}`；同一 `busy` 区间连续两次崩溃/超时 → 毒段，`resumeFrom16k = e16k`。

精修（M2）：`--role=refine` 新实例 → `hello{role:"refine"} → load{Whisper} → refine{segments, prompt}` → 逐句 `final{pass:2}` → `done`；新录制开始时主进程直接 kill。

## 6. 看门狗与退出码（§7.3、§7.5）

| 检测 | 阈值（`WatchdogConfig`） | 主进程动作 / 错误码 |
|---|---|---|
| 无 hb | 5 s | kill → 重启，`HostHeartbeatLost`(706) |
| 段级超时（`busy` 后无结果） | `max(10 s, samples/16000 × 3)` | kill → 重启，`HostDecodeTimeout`(707)；同区间第 2 次 → 毒段 `JobPoisonSegment`(611) |
| 加载超时 | 60 s | kill，该模型本次不再重试 |
| 重启耗尽 | 10 min 内 5 次 | L3，`HostRestartExhausted`(708) |

| 子进程退出码 | 含义 | 主进程 |
|---|---|---|
| 0 | 正常（`quit` / stdin EOF） | — |
| 137（`kExitOutOfMemory`） | 捕获 `bad_alloc` 或 Job 内存上限 | `HostOutOfMemory`(709)：第 1 次 L1 重启，第 2 次 L3 |
| `0xC000001D`（崩溃码） / 305 | CPU 指令集不支持 | `HostCpuUnsupported`(710)：本机禁用字幕，不重试 |
| 301 / 303 | 运行时 DLL 缺失 / 模型加载失败 | `unavailable`，不重试 |
| 704 | 协议版本不一致 | 不重试 |
| 其他非 0 / CrashExit | 崩溃 | 退避重启（1/2/4/8/16 s，上限 30 s） |
| `FailedToStart` | 被拦截等 | `HostBlocked`(711) |

## 7. 资源约束（主进程施加）

- 录制中以 `BELOW_NORMAL_PRIORITY_CLASS` 启动，录完 `SetPriorityClass(NORMAL)`（`AsrHostClient::setPriority`）。
- 启动后立即 `AssignProcessToJobObject`（`JobLimits`）：KILL_ON_JOB_CLOSE、DIE_ON_UNHANDLED_EXCEPTION、进程内存 1.5 GB（精修 3 GB）、可选 CPU rate 硬上限。
- 子进程内 `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`。

## 8. 假 ASR（测试注入，§11.1）

两层，都不需要模型：

1. **进程内假引擎**：`subrec-asr --engine=fake --fake=<k=v,...>` 或 `load{request:{engineId:"fake"}, fakeOptions:"..."}`。只有环境变量 `SUBREC_ASR_TEST_HOOKS=1` 时可用（主进程只在自身 `hooksEnabled()` 时设置），否则 `EngineUnknown`(300)。参数（`asr/src/engine_fake.h` 为准）：
   `rtf`、`crashAtSeg`、`hangAtSeg`、`hangAll`、`oomAtSeg`、`illegalInstr`、`garbageStdout`、`loadFail`、`partialNoise`、`partialRegress`、`finalDiffers`、`lang`（zh/en/mix）、`refineRtf`、`refineEmpty`、`seed`。输出 final 文本 `"第<id>段"`，按段长均匀生成 token 时间戳。
2. **进程外假宿主**：`subrec-asr-fakehost --script=<path>`，按脚本回放协议，用于 `AsrHostClient`/Runner/看门狗测试。脚本指令（`asr/fake_host/fake_host_script.h` 为准）：`expect <type>`、`send <json>`、`reply-hello [n]`、`hb <ms>|off`、`sleep <ms>`、`garbage <text>`、`longline <bytes>`、`exit <code>`、`crash`、`hang`。示例：

```
reply-hello
hb 1000
expect load
send {"type":"loaded","backend":"cpu","loadMs":5}
expect open
send {"type":"busy","jobId":1,"kind":"final","b16k":0,"e16k":16000}
crash
```

主进程测试通过 `AsrHostClientFactory`（`subtitle/host/asr_host_client_factory.h`）把 exePath / 参数指向以上任一种。
