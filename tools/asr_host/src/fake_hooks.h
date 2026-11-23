#pragma once
// 工作包 C 内部：假引擎故障注入与宿主运行时之间的极小共享状态/动作（仅进程内，不跨进程）。
// engine_fake.cpp 定义并写入；host_runtime.cpp 读取。不入任何公共接口。

#include <atomic>

namespace asrhost {

// hangAll 触发后置位：解码线程挂死的同时 hb 线程也停止发送（§11.1）。
std::atomic<bool> &fakeHangAllActive() noexcept;

// 故障注入的段号基点（C2 问题 1）：open 续号后 SegId 从 resumeFrom16k 起算，而注入参数
// crashAtSeg/hangAtSeg/oomAtSeg/hangAll 保持「每次 open 的本地段号从 0 计」（engine_fake.h 的
// 原语义；D 依赖：重启后注入本地第 0 段命中同一段音频）。host_runtime 在每次 open 时写入
// （refine 置 0：其段号来自请求、本就是调用方编号），engine_fake 读取。
std::atomic<int64_t> &fakeInjectionSegBase() noexcept;

// illegalInstr：启动即退出。Windows 上抛 STATUS_ILLEGAL_INSTRUCTION（未处理异常 → 退出码
// 0xC000001D）；非 Windows 以 kExitCpuUnsupported(305) 退出（engine_fake.h 的约定）。
[[noreturn]] void injectIllegalInstrExit() noexcept;

} // namespace asrhost
