#pragma once
// subrec-asr 命令行解析。参数名见 subtitle/host/asr_protocol.h（kArgRole 等），文档见 docs/subtitle/ipc-protocol.md §2。
// 归属：工作包 C（ZCode 终端）。

#include "subtitle/subtitle_error.h"

#include <cstdint>
#include <string>

namespace asrhost {

struct HostOptions {
    std::string role = "live";        // live | refine
    int64_t parentPid = 0;            // 0 = 不看门（仅手工调试）
    std::string engine;               // 空 = 等 load 指定
    std::string fakeOptions;          // --fake=...
    std::string logLevel = "info";
    bool showVersion = false;         // --version：打印版本到 stdout 后退出（不走协议）
};

// 未知参数 → InvalidArgument。argv 为 UTF-8（Windows 上由 main 用 wmain/CommandLineToArgvW 转换）。
bool parseHostOptions(int argc, const char *const *argv, HostOptions &out, subtitle::Status *err);

} // namespace asrhost
