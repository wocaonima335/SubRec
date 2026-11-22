// subrec-asr 入口（M0 桩）。真实实现归工作包 C：Windows 上 SetErrorMode(SEM_FAILCRITICALERRORS |
// SEM_NOGPFAULTERRORBOX)、wmain 转 UTF-8、捕获 bad_alloc → exit(kExitOutOfMemory)、ProtocolIo::install、
// ParentWatch::start、runHost。
// 桩行为：解析参数；--version 打印版本；否则向 stderr 说明是桩并以 NotSupported(104) 退出。不读模型。

#include "host_options.h"
#include "host_runtime.h"

#include <cstdio>

#ifndef SUBREC_ASR_VERSION
#define SUBREC_ASR_VERSION "0.0.0-m0"
#endif

int main(int argc, char **argv)
{
    asrhost::HostOptions options;
    subtitle::Status err;
    if (!asrhost::parseHostOptions(argc, argv, options, &err)) {
        std::fprintf(stderr, "subrec-asr: %s\n", err.message.c_str());
        return static_cast<int>(subtitle::ErrorCode::InvalidArgument);
    }
    if (options.showVersion) {
        std::printf("subrec-asr %s protocol=%d\n", SUBREC_ASR_VERSION, subtitle::proto::kProtocolVersion);
        return subtitle::proto::kExitOk;
    }
    std::fprintf(stderr, "subrec-asr: M0 stub, recognition not implemented (role=%s)\n", options.role.c_str());
    return asrhost::runHost(options);
}
