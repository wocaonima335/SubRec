// M0 桩：JSON-lines 编解码。真实实现归工作包 C（ZCode 终端），纯 C++17、不引入 Qt；
// 主进程（MinGW）与 subrec-asr（MSVC）编译同一份源码，实现时不得使用任一侧独有的库。
// 桩行为：typeName / acceptedByRole / backendName / parseBackend 已按协议文档写好（它们是协议的一部分）；
// encode 只输出 {"type":"<name>"}，decode 一律返回 nullopt + NotSupported。

#include "subtitle/host/asr_protocol.h"

#include <type_traits>

namespace subtitle::proto {

namespace {
template <class T> constexpr const char *nameOf();
template <> constexpr const char *nameOf<Hello>() { return "hello"; }
template <> constexpr const char *nameOf<Load>() { return "load"; }
template <> constexpr const char *nameOf<Open>() { return "open"; }
template <> constexpr const char *nameOf<Append>() { return "append"; }
template <> constexpr const char *nameOf<Splice>() { return "splice"; }
template <> constexpr const char *nameOf<SetLevel>() { return "setLevel"; }
template <> constexpr const char *nameOf<Pause>() { return "pause"; }
template <> constexpr const char *nameOf<Resume>() { return "resume"; }
template <> constexpr const char *nameOf<Finish>() { return "finish"; }
template <> constexpr const char *nameOf<Cancel>() { return "cancel"; }
template <> constexpr const char *nameOf<Quit>() { return "quit"; }
template <> constexpr const char *nameOf<Refine>() { return "refine"; }
template <> constexpr const char *nameOf<Loaded>() { return "loaded"; }
template <> constexpr const char *nameOf<Heartbeat>() { return "hb"; }
template <> constexpr const char *nameOf<Busy>() { return "busy"; }
template <> constexpr const char *nameOf<PartialMsg>() { return "partial"; }
template <> constexpr const char *nameOf<FinalMsg>() { return "final"; }
template <> constexpr const char *nameOf<Progress>() { return "progress"; }
template <> constexpr const char *nameOf<Done>() { return "done"; }
template <> constexpr const char *nameOf<Cancelled>() { return "cancelled"; }
template <> constexpr const char *nameOf<Error>() { return "error"; }
template <> constexpr const char *nameOf<Log>() { return "log"; }
} // namespace

const char *typeName(const Message &m) noexcept
{
    return std::visit([](const auto &v) { return nameOf<std::decay_t<decltype(v)>>(); }, m);
}

std::string encode(const Message &m)
{
    return std::string("{\"type\":\"") + typeName(m) + "\"}";
}

std::optional<Message> decode(std::string_view, Status *err)
{
    if (err)
        *err = Status::error(ErrorCode::NotSupported, "M0 stub: proto::decode");
    return std::nullopt;
}

bool acceptedByRole(const Message &m, std::string_view role) noexcept
{
    if (role != kRoleRefine)
        return !std::holds_alternative<Refine>(m);
    return std::holds_alternative<Hello>(m) || std::holds_alternative<Load>(m) || std::holds_alternative<Refine>(m)
        || std::holds_alternative<Cancel>(m) || std::holds_alternative<Quit>(m);
}

const char *backendName(Backend b) noexcept
{
    switch (b) {
    case Backend::Auto: return "auto";
    case Backend::Cpu: return "cpu";
    case Backend::Vulkan: return "vulkan";
    case Backend::Cuda: return "cuda";
    }
    return "auto";
}

std::optional<Backend> parseBackend(std::string_view s) noexcept
{
    if (s == "auto") return Backend::Auto;
    if (s == "cpu") return Backend::Cpu;
    if (s == "vulkan") return Backend::Vulkan;
    if (s == "cuda") return Backend::Cuda;
    return std::nullopt;
}

} // namespace subtitle::proto
