#pragma once
// 工作包 C 内部：假引擎故障注入与宿主运行时之间的极小共享状态/动作（仅进程内，不跨进程）。
// engine_fake.cpp 定义并写入；host_runtime.cpp 读取。不入任何公共接口。

#include <atomic>

namespace asrhost {

// hangAll 触发后置位：解码线程挂死的同时 hb 线程也停止发送（§11.1）。
std::atomic<bool> &fakeHangAllActive() noexcept;

// illegalInstr：启动即退出。Windows 上抛 STATUS_ILLEGAL_INSTRUCTION（未处理异常 → 退出码
// 0xC000001D）；非 Windows 以 kExitCpuUnsupported(305) 退出（engine_fake.h 的约定）。
[[noreturn]] void injectIllegalInstrExit() noexcept;

} // namespace asrhost
