// 命令行解析（工作包 C）。参数名见 subtitle/host/asr_protocol.h（kArgRole 等）。

#include "host_options.h"

#include "subtitle/host/asr_protocol.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace asrhost {

bool parseHostOptions(int argc, const char *const *argv, HostOptions &out, subtitle::Status *err)
{
    HostOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto valueOf = [&](const char *name) -> const char * {
            const size_t n = std::strlen(name);
            return a.compare(0, n, name) == 0 && a.size() > n && a[n] == '=' ? a.c_str() + n + 1 : nullptr;
        };
        if (const char *v = valueOf(subtitle::proto::kArgRole)) o.role = v;
        else if (const char *v2 = valueOf(subtitle::proto::kArgParentPid)) o.parentPid = std::atoll(v2);
        else if (const char *v3 = valueOf(subtitle::proto::kArgEngine)) o.engine = v3;
        else if (const char *v4 = valueOf(subtitle::proto::kArgFake)) o.fakeOptions = v4;
        else if (const char *v5 = valueOf(subtitle::proto::kArgLogLevel)) o.logLevel = v5;
        else if (a == "--version") o.showVersion = true;
        else {
            if (err)
                *err = subtitle::Status::error(subtitle::ErrorCode::InvalidArgument, "unknown argument: " + a);
            return false;
        }
    }
    if (o.role != subtitle::proto::kRoleLive && o.role != subtitle::proto::kRoleRefine) {
        if (err)
            *err = subtitle::Status::error(subtitle::ErrorCode::InvalidArgument, "bad role: " + o.role);
        return false;
    }
    out = o;
    return true;
}

} // namespace asrhost
