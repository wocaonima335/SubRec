// M0 桩：subrec-asr 运行时（ProtocolIo / SpoolTailReader / SegmentStateMachine / ParentWatch / runHost /
// 引擎与 VAD 工厂 / 命令行解析）。真实实现归工作包 C（ZCode 终端），实现时按类拆文件并删除本文件。
// 桩行为：parseHostOptions 已可用；runHost 不读 stdin，直接以 NotSupported 退出码返回；不加载任何模型。

#include "asr_engine.h"
#include "engine_fake.h"
#include "host_options.h"
#include "host_runtime.h"

#include <cstdlib>
#include <cstring>

namespace asrhost {

namespace {
subtitle::Status m0Stub(const char *what)
{
    return subtitle::Status::error(subtitle::ErrorCode::NotSupported, std::string("M0 stub: ") + what);
}
} // namespace

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

std::unique_ptr<IAsrEngine> createEngine(const std::string &engineId, const std::string &fakeOptions,
                                         subtitle::Status *err)
{
    if (engineId == "fake" && fakeEngineAllowed()) {
        FakeEngineOptions o;
        if (!parseFakeOptions(fakeOptions, o, err))
            return nullptr;
        return std::make_unique<FakeAsrEngine>(o);
    }
    if (err)
        *err = subtitle::Status::error(subtitle::ErrorCode::EngineUnknown, "M0 stub: engine " + engineId);
    return nullptr;
}

std::unique_ptr<IVad> createVad(const subtitle::EngineRequest &, subtitle::Status *err)
{
    if (err)
        *err = m0Stub("createVad");
    return nullptr;
}

subtitle::Status ProtocolIo::install() { return m0Stub("ProtocolIo::install"); }
void ProtocolIo::write(const subtitle::proto::Message &) {}
std::optional<subtitle::proto::Message> ProtocolIo::readCommand() { return std::nullopt; }

subtitle::Status SpoolTailReader::open(const std::string &) { return m0Stub("SpoolTailReader::open"); }
void SpoolTailReader::setCommitted(int64_t) {}
int64_t SpoolTailReader::committed() const { return 0; }
subtitle::Status SpoolTailReader::read(int64_t, int64_t, std::vector<float> &) { return m0Stub("SpoolTailReader::read"); }
void SpoolTailReader::close() {}

void SegmentStateMachine::configure(const subtitle::LiveConfig &, int64_t, subtitle::SegId) {}
void SegmentStateMachine::onSpeechStart(int64_t) {}
void SegmentStateMachine::onSpeechEnd(int64_t) {}
void SegmentStateMachine::onSplice(int64_t) {}
void SegmentStateMachine::onFinish(int64_t) {}
void SegmentStateMachine::setPartialIntervalMs(int) {}
SegmentStateMachine::Action SegmentStateMachine::next(int64_t, int64_t, bool) { return {}; }

subtitle::Status ParentWatch::start(int64_t, std::function<void()>) { return m0Stub("ParentWatch::start"); }

int runHost(const HostOptions &)
{
    return static_cast<int>(subtitle::ErrorCode::NotSupported);
}

} // namespace asrhost
