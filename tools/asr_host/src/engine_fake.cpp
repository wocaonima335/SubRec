// 假 ASR 引擎（工作包 C 实现，design-live-v2.md §11.1 第 1 层）。参数与输出约定见 engine_fake.h（M0 冻结）。
//
// 输出模型：
// - final 文本按 lang 生成（zh："第<id>段"；en："seg <id>"；mix："第<id>段 ..."，language="zh" 且 ASCII ≥ 30%）。
// - 每个码点一个 token，时间戳在 [begin16k, end16k) 换算的绝对毫秒区间内均匀分布。
// - partial 文本 = final 文本按「已识别时长」截取的前缀（每 250 ms 露出一个码点，确定性的）。
// - rtf / refineRtf 用可取消的 sleep 模拟解码耗时；decodeMs 按真实耗时（含 sleep）填写。
//
// 进程级注入：crashAtSeg（空指针写）、oomAtSeg（exit 137）、hangAtSeg（解码线程永久 sleep、hb 继续）、
// hangAll（解码线程永久 sleep 且置 fakeHangAllActive 让 hb 停）、illegalInstr（injectIllegalInstrExit，
// 由 runHost 在启动/加载时调用）、garbageStdout（向 fd 1 打印非 JSON —— ProtocolIo::install 已把 fd 1
// 重定向到 stderr，协议通道不受影响）。

#include "engine_fake.h"
#include "fake_hooks.h"

#include "subtitle/host/asr_protocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace asrhost {

namespace {

std::mt19937 &sharedRng()
{
    static std::mt19937 rng(1);
    return rng;
}

// 引擎取消标志（engine_fake.h 冻结、不能加私有成员，放文件级；宿主内一个进程只有一个引擎实例）。
// 每次 recognize 入口复位，requestCancel 置位打断正在模拟的 rtf sleep。
std::atomic<bool> g_fakeCancel{false};

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void sleepCancellable(double ms, std::atomic<bool> &cancel)
{
    const int64_t deadline = nowMs() + static_cast<int64_t>(ms);
    while (nowMs() < deadline) {
        if (cancel.load(std::memory_order_relaxed))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ---- UTF-8 码点工具 ----

size_t codepointCount(const std::string &s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        i += c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        ++n;
    }
    return n;
}

// 第 index 个码点（含）起的前缀；index 超出 → 整串。
std::string codepointPrefix(const std::string &s, size_t index)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        if (n == index)
            return s.substr(0, i);
        const unsigned char c = static_cast<unsigned char>(s[i]);
        i += c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        ++n;
    }
    return s;
}

// 替换第 index 个码点为 replacement（多字节 UTF-8 序列）。
std::string replaceCodepoint(const std::string &s, size_t index, const std::string &replacement)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        if (n == index)
            return s.substr(0, i) + replacement + s.substr(i + len);
        i += len;
        ++n;
    }
    return s;
}

size_t utf8Bytes(size_t cpIndexIn, const std::string &s, size_t &byteIndex)
{
    size_t n = 0;
    byteIndex = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        if (n == cpIndexIn) {
            byteIndex = i;
            return len;
        }
        i += len;
        ++n;
    }
    byteIndex = s.size();
    return 0;
}

std::string fullTextFor(int64_t segId, const std::string &lang)
{
    const std::string id = std::to_string(segId);
    if (lang == "en")
        return "seg " + id + " spoken";
    if (lang == "mix")
        return "第" + id + "段 with some english words mixed in " + id;
    return "第" + id + "段";
}

} // namespace

std::atomic<bool> &fakeHangAllActive() noexcept
{
    static std::atomic<bool> flag{false};
    return flag;
}

[[noreturn]] void injectIllegalInstrExit() noexcept
{
#if defined(_WIN32)
    RaiseException(subtitle::proto::kStatusIllegalInstruction, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::abort();   // RaiseException 不该返回
#else
    std::fprintf(stderr, "subrec-asr: fake illegalInstr exit %d\n", subtitle::proto::kExitCpuUnsupported);
    std::_Exit(subtitle::proto::kExitCpuUnsupported);
#endif
}

// ---- 参数解析（M0 已冻结的实现，保持原样）----

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

// ---- 引擎 ----

FakeAsrEngine::FakeAsrEngine(FakeEngineOptions options) : m_options(std::move(options)) {}

subtitle::Status FakeAsrEngine::load(const subtitle::EngineRequest &, std::string *backendUsed)
{
    if (backendUsed)
        *backendUsed = "cpu";
    if (m_options.loadFail)
        return subtitle::Status::error(static_cast<subtitle::ErrorCode>(m_options.loadFail), "fake loadFail");
    sharedRng().seed(m_options.seed);
    m_partialCount = 0;
    if (m_options.garbageStdout) {
        // fd 1 在 ProtocolIo::install() 之后指向 stderr；协议通道（dup 出的原 fd）不受影响。
        std::fprintf(stdout, "FAKE-ENGINE-GARBAGE {not-json}\n");
        std::fflush(stdout);
    }
    return {};
}

subtitle::Status FakeAsrEngine::recognize(const float *pcm16k, size_t n, RecognizeKind kind,
                                          subtitle::FinalSegment &io)
{
    const int64_t t0 = nowMs();
    const double durationMs = static_cast<double>(n) / subtitle::kAsrSamplesPerMs;
    (void)pcm16k;
    // 共享 RNG 在每次 recognize 入口按 seed 重置：同一参数的调用序列完全确定，
    // 与调用顺序 / 其他引擎实例无关（引擎不能加私有成员，见 engine_fake.h 冻结说明）。
    sharedRng().seed(m_options.seed);
    g_fakeCancel.store(false, std::memory_order_relaxed);

    if (m_options.garbageStdout) {
        std::fprintf(stdout, "FAKE-ENGINE-GARBAGE recognize seg=%lld kind=%d\n",
                     static_cast<long long>(io.id), static_cast<int>(kind));
        std::fflush(stdout);
    }

    // 进程级故障注入（段号 n 从 0 开始，与 SegId 一致）。
    if (m_options.crashAtSeg >= 0 && io.id == m_options.crashAtSeg) {
        std::fprintf(stderr, "subrec-asr: fake crashAtSeg=%lld\n", static_cast<long long>(io.id));
        std::fflush(stderr);
        *(volatile int *)0 = 0;
    }
    if (m_options.oomAtSeg >= 0 && io.id == m_options.oomAtSeg) {
        std::fprintf(stderr, "subrec-asr: fake oomAtSeg=%lld\n", static_cast<long long>(io.id));
        std::fflush(stderr);
        // 不能用 std::exit：它的 _IO_cleanup 要锁全部 stdio 流，而 IO 线程正阻塞在 fgets(stdin)
        // 上持有 stdin 锁（glibc 死锁）。_Exit 立即终止，退出码同样为 137。
        std::_Exit(subtitle::proto::kExitOutOfMemory);
    }
    if (m_options.hangAtSeg >= 0 && io.id == m_options.hangAtSeg) {
        std::fprintf(stderr, "subrec-asr: fake hangAtSeg=%lld\n", static_cast<long long>(io.id));
        std::fflush(stderr);
        for (;;)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (m_options.hangAll >= 0 && io.id >= m_options.hangAll) {
        std::fprintf(stderr, "subrec-asr: fake hangAll at seg=%lld\n", static_cast<long long>(io.id));
        std::fflush(stderr);
        fakeHangAllActive().store(true, std::memory_order_relaxed);
        for (;;)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    const double rtf = kind == RecognizeKind::Refine ? m_options.refineRtf : m_options.rtf;
    if (rtf > 0)
        sleepCancellable(durationMs * rtf, g_fakeCancel);

    std::string text = fullTextFor(io.id, m_options.lang);
    if (kind == RecognizeKind::Partial) {
        ++m_partialCount;
        // 已识别时长 → 露出的码点数（每 250 ms 一个，至少 1 个）。
        const size_t total = codepointCount(text);
        size_t visible = static_cast<size_t>(std::ceil(durationMs / 250.0));
        visible = std::min(total, std::max<size_t>(1, visible));
        text = codepointPrefix(text, visible);
        if (m_options.partialRegress >= 0 && m_partialCount == static_cast<uint32_t>(m_options.partialRegress)) {
            // 把已出现的前缀改掉：替换第 0 个码点（LocalAgreement 的「改口不回退」路径）。
            text = replaceCodepoint(text, 0, "改");
        } else if (m_options.partialNoise > 0) {
            std::uniform_real_distribution<double> prob(0.0, 1.0);
            if (prob(sharedRng()) < m_options.partialNoise) {
                static const char *kNoise[] = {"啊", "嗯", "哦", "呃", "呀"};
                std::uniform_int_distribution<int> pick(0, 4);
                std::uniform_int_distribution<int> tailCount(1, 3);
                const int count = std::min<int>(static_cast<int>(codepointCount(text)), tailCount(sharedRng()));
                for (int k = 0; k < count; ++k) {
                    const size_t idx = codepointCount(text) - 1 - static_cast<size_t>(k);
                    text = replaceCodepoint(text, idx, kNoise[pick(sharedRng())]);
                }
            }
        }
    } else if (kind == RecognizeKind::Final && m_options.finalDiffers > 0) {
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (prob(sharedRng()) < m_options.finalDiffers) {
            // 与最后一次 partial 不同：改写末尾码点。
            text = replaceCodepoint(text, codepointCount(text) - 1, "定");
        }
    }
    if (kind == RecognizeKind::Refine && m_options.refineEmpty > 0) {
        std::uniform_real_distribution<double> prob(0.0, 1.0);
        if (prob(sharedRng()) < m_options.refineEmpty)
            text.clear();
    }

    io.text = text;
    io.language = (m_options.lang == "en") ? "en" : "zh";
    io.emotion.clear();
    io.event.clear();
    io.modelId = "fake";

    // token 时间戳：每个码点一个 token，均匀铺满 [begin16k, end16k) 的绝对毫秒区间。
    io.words.clear();
    const size_t cps = codepointCount(text);
    if (cps > 0) {
        const int64_t baseMs = subtitle::asrSamplesToMs(io.begin16k);
        const double spanMs = std::max<double>(1.0, subtitle::asrSamplesToMs(io.end16k - io.begin16k));
        const double step = spanMs / static_cast<double>(cps);
        for (size_t k = 0; k < cps; ++k) {
            size_t byteIdx = 0;
            const size_t len = utf8Bytes(k, text, byteIdx);
            subtitle::Word w;
            w.text = text.substr(byteIdx, len);
            w.beginMs = baseMs + static_cast<int64_t>(std::llround(step * static_cast<double>(k)));
            w.endMs = baseMs + static_cast<int64_t>(std::llround(step * static_cast<double>(k + 1)));
            w.prob = -1.f;
            io.words.push_back(std::move(w));
        }
    }
    io.decodeMs = static_cast<float>(nowMs() - t0);
    return {};
}

void FakeAsrEngine::requestCancel() noexcept
{
    g_fakeCancel.store(true, std::memory_order_relaxed);
}

} // namespace asrhost
