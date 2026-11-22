// M0 桩：假 ASR 引擎。真实实现归工作包 C。
// 已实现：参数解析 parseFakeOptions / formatFakeOptions（它们是主进程测试与宿主之间的约定，必须先可用）。
// 桩：load 只处理 loadFail；recognize 只输出 "第<id>段"，不 sleep、不注入故障、不生成 token。

#include "engine_fake.h"

#include <cstdlib>
#include <string>

namespace asrhost {

namespace {

bool parseDouble(std::string_view v, double &out)
{
    std::string s(v);
    char *end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return !s.empty() && end && *end == '\0';
}

bool parseInt(std::string_view v, int64_t &out)
{
    std::string s(v);
    char *end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return !s.empty() && end && *end == '\0';
}

} // namespace

bool parseFakeOptions(std::string_view text, FakeEngineOptions &out, subtitle::Status *err)
{
    FakeEngineOptions o;
    auto fail = [&](std::string_view key) {
        if (err)
            *err = subtitle::Status::error(subtitle::ErrorCode::InvalidArgument,
                                           "fake option: " + std::string(key));
        return false;
    };
    while (!text.empty()) {
        const size_t comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        if (item.empty())
            continue;
        const size_t eq = item.find('=');
        const std::string_view key = item.substr(0, eq);
        const std::string_view val = eq == std::string_view::npos ? std::string_view() : item.substr(eq + 1);
        int64_t i = 0;
        double d = 0;
        if (key == "rtf" && parseDouble(val, d)) o.rtf = d;
        else if (key == "crashAtSeg" && parseInt(val, i)) o.crashAtSeg = i;
        else if (key == "hangAtSeg" && parseInt(val, i)) o.hangAtSeg = i;
        else if (key == "hangAll" && parseInt(val, i)) o.hangAll = i;
        else if (key == "oomAtSeg" && parseInt(val, i)) o.oomAtSeg = i;
        else if (key == "illegalInstr" && eq == std::string_view::npos) o.illegalInstr = true;
        else if (key == "garbageStdout" && eq == std::string_view::npos) o.garbageStdout = true;
        else if (key == "loadFail" && parseInt(val, i)) o.loadFail = static_cast<int>(i);
        else if (key == "partialNoise" && parseDouble(val, d)) o.partialNoise = d;
        else if (key == "partialRegress" && parseInt(val, i)) o.partialRegress = i;
        else if (key == "finalDiffers" && parseDouble(val, d)) o.finalDiffers = d;
        else if (key == "lang" && (val == "zh" || val == "en" || val == "mix")) o.lang = std::string(val);
        else if (key == "refineRtf" && parseDouble(val, d)) o.refineRtf = d;
        else if (key == "refineEmpty" && parseDouble(val, d)) o.refineEmpty = d;
        else if (key == "seed" && parseInt(val, i) && i >= 0) o.seed = static_cast<uint32_t>(i);
        else
            return fail(item);
    }
    out = o;
    return true;
}

std::string formatFakeOptions(const FakeEngineOptions &o)
{
    std::string s = "rtf=" + std::to_string(o.rtf);
    auto addInt = [&](const char *k, int64_t v) {
        if (v >= 0)
            s += std::string(",") + k + "=" + std::to_string(v);
    };
    addInt("crashAtSeg", o.crashAtSeg);
    addInt("hangAtSeg", o.hangAtSeg);
    addInt("hangAll", o.hangAll);
    addInt("oomAtSeg", o.oomAtSeg);
    if (o.illegalInstr) s += ",illegalInstr";
    if (o.garbageStdout) s += ",garbageStdout";
    if (o.loadFail) s += ",loadFail=" + std::to_string(o.loadFail);
    s += ",partialNoise=" + std::to_string(o.partialNoise);
    addInt("partialRegress", o.partialRegress);
    s += ",finalDiffers=" + std::to_string(o.finalDiffers);
    s += ",lang=" + o.lang;
    s += ",refineRtf=" + std::to_string(o.refineRtf);
    s += ",refineEmpty=" + std::to_string(o.refineEmpty);
    s += ",seed=" + std::to_string(o.seed);
    return s;
}

bool fakeEngineAllowed() noexcept
{
    const char *v = std::getenv("SUBREC_ASR_TEST_HOOKS");
    return v && v[0] == '1' && v[1] == '\0';
}

FakeAsrEngine::FakeAsrEngine(FakeEngineOptions options) : m_options(std::move(options)) {}

subtitle::Status FakeAsrEngine::load(const subtitle::EngineRequest &, std::string *backendUsed)
{
    if (backendUsed)
        *backendUsed = "cpu";
    if (m_options.loadFail)
        return subtitle::Status::error(static_cast<subtitle::ErrorCode>(m_options.loadFail), "fake loadFail");
    return {};
}

subtitle::Status FakeAsrEngine::recognize(const float *, size_t, RecognizeKind kind, subtitle::FinalSegment &io)
{
    if (kind == RecognizeKind::Partial)
        ++m_partialCount;
    io.text = "第" + std::to_string(io.id) + "段";
    io.language = m_options.lang == "en" ? "en" : "zh";
    io.modelId = "fake";
    io.decodeMs = 0.f;
    return {};
}

void FakeAsrEngine::requestCancel() noexcept {}

} // namespace asrhost
