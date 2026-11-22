// 协议编解码实现（工作包 C，ZCode 终端）。docs/subtitle/ipc-protocol.md 逐字段一致。
// 纯 C++17、无 Qt：内嵌一个最小 JSON 解析/序列化实现（对象/数组/字符串/数字/布尔/null，
// 含 \uXXXX 转义与代理对），主进程（MinGW）与 subrec-asr（MSVC）编译同一份源码。
//
// 必需字段的约定（ipc-protocol.md 只对个别字段标了 "?"）：凡没有合理默认值的「标识 / 路由」字段
// 为必需（jobId、坐标类字段、type 载荷的键、request.engineId、segments 等），缺失或类型不符 →
// HostProtocolError；其余载荷字段可选，缺省取 asr_protocol.h 结构体的默认值，未知字段一律忽略。
// 整数必须是 JSON number 且为整值、|值| < 2^53（§1 通则，超出 → HostProtocolError）。

#include "subtitle/host/asr_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

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

// ============================ 最小 JSON ============================

namespace {

constexpr int kMaxJsonDepth = 64;    // 防深度递归（1 MiB 的 '[[[...' 不应爆栈）
constexpr double kJsonIntLimit = 9007199254740992.0;   // 2^53（§1：以 double 存数）

struct Jv {
    enum Kind { Null, Bool, Num, Str, Arr, Obj };
    Kind kind = Null;
    bool boolean = false;
    double num = 0;
    std::string str;
    std::vector<Jv> arr;
    std::vector<std::pair<std::string, Jv>> members;

    const Jv *find(std::string_view key) const
    {
        for (const auto &kv : members)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : m_s(text) {}

    bool parse(Jv &out, std::string &err)
    {
        skipWs();
        if (!value(out, 0)) {
            err = "invalid json at offset " + std::to_string(m_i);
            return false;
        }
        skipWs();
        if (m_i != m_s.size()) {
            err = "trailing content at offset " + std::to_string(m_i);
            return false;
        }
        return true;
    }

private:
    std::string_view m_s;
    size_t m_i = 0;

    void skipWs()
    {
        while (m_i < m_s.size()) {
            const char c = m_s[m_i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                ++m_i;
            else
                break;
        }
    }

    bool value(Jv &out, int depth)
    {
        if (depth > kMaxJsonDepth || m_i >= m_s.size())
            return false;
        switch (m_s[m_i]) {
        case '{': return object(out, depth);
        case '[': return array(out, depth);
        case '"':
            out.kind = Jv::Str;
            return stringBody(out.str);
        case 't': return literal("true", out, Jv::Bool, true);
        case 'f': return literal("false", out, Jv::Bool, false);
        case 'n': return literal("null", out, Jv::Null, false);
        default: return numberBody(out);
        }
    }

    template <size_t N>
    bool literal(const char (&lit)[N], Jv &out, typename Jv::Kind kind, bool boolean)
    {
        if (m_s.compare(m_i, N - 1, lit, N - 1) != 0)
            return false;
        m_i += N - 1;
        out.kind = kind;
        out.boolean = boolean;
        return true;
    }

    bool object(Jv &out, int depth)
    {
        ++m_i; // '{'
        out.kind = Jv::Obj;
        skipWs();
        if (m_i < m_s.size() && m_s[m_i] == '}') {
            ++m_i;
            return true;
        }
        for (;;) {
            skipWs();
            std::string key;
            if (m_i >= m_s.size() || m_s[m_i] != '"' || !stringBody(key))
                return false;
            skipWs();
            if (m_i >= m_s.size() || m_s[m_i] != ':')
                return false;
            ++m_i;
            skipWs();
            Jv v;
            if (!value(v, depth + 1))
                return false;
            out.members.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (m_i >= m_s.size())
                return false;
            if (m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_s[m_i] == '}') {
                ++m_i;
                return true;
            }
            return false;
        }
    }

    bool array(Jv &out, int depth)
    {
        ++m_i; // '['
        out.kind = Jv::Arr;
        skipWs();
        if (m_i < m_s.size() && m_s[m_i] == ']') {
            ++m_i;
            return true;
        }
        for (;;) {
            skipWs();
            Jv v;
            if (!value(v, depth + 1))
                return false;
            out.arr.push_back(std::move(v));
            skipWs();
            if (m_i >= m_s.size())
                return false;
            if (m_s[m_i] == ',') {
                ++m_i;
                continue;
            }
            if (m_s[m_i] == ']') {
                ++m_i;
                return true;
            }
            return false;
        }
    }

    bool stringBody(std::string &out)
    {
        ++m_i; // '"'
        while (m_i < m_s.size()) {
            const unsigned char c = static_cast<unsigned char>(m_s[m_i]);
            if (c == '"') {
                ++m_i;
                return true;
            }
            if (c < 0x20)
                return false;
            if (c == '\\') {
                ++m_i;
                if (m_i >= m_s.size())
                    return false;
                const char e = m_s[m_i++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (m_i + 1 < m_s.size() && m_s[m_i] == '\\' && m_s[m_i + 1] == 'u') {
                            m_i += 2;
                            uint32_t lo;
                            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF)
                                return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            return false;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;   // 孤立低代理
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return false;
                }
            } else {
                out += static_cast<char>(c);
                ++m_i;
            }
        }
        return false;   // 未闭合
    }

    bool hex4(uint32_t &out)
    {
        if (m_i + 4 > m_s.size())
            return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = m_s[m_i + k];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out += static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                out += static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out += static_cast<uint32_t>(c - 'A' + 10);
            else
                return false;
        }
        m_i += 4;
        return true;
    }

    static void appendUtf8(std::string &out, uint32_t cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool numberBody(Jv &out)
    {
        const size_t start = m_i;
        while (m_i < m_s.size()) {
            const char c = m_s[m_i];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
                ++m_i;
            else
                break;
        }
        if (m_i == start)
            return false;
        const std::string tok(m_s.substr(start, m_i - start));
        char *end = nullptr;
        const double v = std::strtod(tok.c_str(), &end);
        if (!end || *end != '\0')
            return false;
        out.kind = Jv::Num;
        out.num = v;
        return true;
    }
};

// ---- 序列化 ----

void quoteInto(std::string &out, std::string_view s)
{
    out += '"';
    char buf[8];
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}

std::string quoted(std::string_view s)
{
    std::string out;
    quoteInto(out, s);
    return out;
}

// 逐键追加的极简对象构造器（保证键序稳定，便于日志比对）。
struct Obj {
    std::string s = "{";
    bool first = true;

    void raw(const char *k, const std::string &value)
    {
        if (!first)
            s += ',';
        first = false;
        quoteInto(s, k);
        s += ':';
        s += value;
    }
    void str(const char *k, std::string_view v) { raw(k, quoted(v)); }
    void i64(const char *k, int64_t v) { raw(k, std::to_string(v)); }
    void u64(const char *k, uint64_t v) { raw(k, std::to_string(v)); }
    void u32(const char *k, uint32_t v) { raw(k, std::to_string(v)); }
    void boolean(const char *k, bool v) { raw(k, v ? "true" : "false"); }
    void f32(const char *k, float v)
    {
        char buf[64];
        if (std::isfinite(v))
            std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
        else
            std::snprintf(buf, sizeof buf, "0");
        raw(k, buf);
    }
    void f64(const char *k, double v)
    {
        char buf[64];
        if (std::isfinite(v))
            std::snprintf(buf, sizeof buf, "%.17g", v);
        else
            std::snprintf(buf, sizeof buf, "0");
        raw(k, buf);
    }
    std::string done() const { return s + "}"; }
};

std::string strArray(const std::vector<std::string> &items)
{
    std::string s = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i)
            s += ',';
        quoteInto(s, items[i]);
    }
    s += ']';
    return s;
}

// ---- 反序列化取值辅助（必需 = required；类型不符 / 非整 / 超出 2^53 → HostProtocolError）----

Status hpe(const char *what)
{
    return Status::error(ErrorCode::HostProtocolError, what);
}

bool numToInt(const Jv &v, int64_t &out, const char *key)
{
    if (v.kind != Jv::Num)
        return false;
    const double d = v.num;
    if (!(d == std::floor(d)) || std::fabs(d) >= kJsonIntLimit)
        return false;
    out = static_cast<int64_t>(d);
    (void)key;
    return true;
}

// 返回 false 表示出错（err 已填 HostProtocolError）；字段缺失且非必需 → true，out 保留默认值。
bool getInt(const Jv &o, const char *key, int64_t &out, bool required, Status *err)
{
    const Jv *v = o.find(key);
    if (!v) {
        if (required && err)
            *err = hpe((std::string("missing field: ") + key).c_str());
        return !required;
    }
    int64_t i = 0;
    if (!numToInt(*v, i, key)) {
        if (err)
            *err = hpe((std::string("bad number field: ") + key).c_str());
        return false;
    }
    out = i;
    return true;
}

bool getDouble(const Jv &o, const char *key, double &out, bool required, Status *err)
{
    const Jv *v = o.find(key);
    if (!v) {
        if (required && err)
            *err = hpe((std::string("missing field: ") + key).c_str());
        return !required;
    }
    if (v->kind != Jv::Num) {
        if (err)
            *err = hpe((std::string("bad number field: ") + key).c_str());
        return false;
    }
    out = v->num;
    return true;
}

bool getString(const Jv &o, const char *key, std::string &out, bool required, Status *err)
{
    const Jv *v = o.find(key);
    if (!v) {
        if (required && err)
            *err = hpe((std::string("missing field: ") + key).c_str());
        return !required;
    }
    if (v->kind != Jv::Str) {
        if (err)
            *err = hpe((std::string("bad string field: ") + key).c_str());
        return false;
    }
    out = v->str;
    return true;
}

bool getBool(const Jv &o, const char *key, bool &out, bool required, Status *err)
{
    const Jv *v = o.find(key);
    if (!v) {
        if (required && err)
            *err = hpe((std::string("missing field: ") + key).c_str());
        return !required;
    }
    if (v->kind != Jv::Bool) {
        if (err)
            *err = hpe((std::string("bad bool field: ") + key).c_str());
        return false;
    }
    out = v->boolean;
    return true;
}

const Jv *getObj(const Jv &o, const char *key, bool required, Status *err)
{
    const Jv *v = o.find(key);
    if (!v) {
        if (required)
            if (err)
                *err = hpe((std::string("missing object field: ") + key).c_str());
        return nullptr;
    }
    if (v->kind != Jv::Obj) {
        if (err)
            *err = hpe((std::string("bad object field: ") + key).c_str());
        return nullptr;
    }
    return v;
}

const std::vector<Jv> *getArr(const Jv &o, const char *key, bool required, Status *err)
{
    const Jv *v = o.find(key);
    if (!v) {
        if (required)
            if (err)
                *err = hpe((std::string("missing array field: ") + key).c_str());
        return nullptr;
    }
    if (v->kind != Jv::Arr) {
        if (err)
            *err = hpe((std::string("bad array field: ") + key).c_str());
        return nullptr;
    }
    return &v->arr;
}

// ---- 子结构 ----

bool decodeVad(const Jv &o, VadConfig &vad, Status *err)
{
    double d = 0;
    int64_t i = 0;
    if (!getDouble(o, "threshold", d, false, err)) return false;
    if (o.find("threshold")) vad.threshold = static_cast<float>(d);
    if (!getInt(o, "minSpeechMs", i, false, err)) return false;
    if (o.find("minSpeechMs")) vad.minSpeechMs = static_cast<int>(i);
    if (!getInt(o, "minSilenceMs", i, false, err)) return false;
    if (o.find("minSilenceMs")) vad.minSilenceMs = static_cast<int>(i);
    if (!getInt(o, "speechPadMs", i, false, err)) return false;
    if (o.find("speechPadMs")) vad.speechPadMs = static_cast<int>(i);
    if (!getInt(o, "maxSegmentMs", i, false, err)) return false;
    if (o.find("maxSegmentMs")) vad.maxSegmentMs = static_cast<int>(i);
    return true;
}

bool decodeEngineRequest(const Jv &o, EngineRequest &r, Status *err)
{
    if (!getString(o, "engineId", r.engineId, true, err))
        return false;
    if (!getString(o, "modelId", r.modelId, false, err)) return false;
    std::string dir;
    if (!getString(o, "modelDir", dir, false, err)) return false;
    if (o.find("modelDir"))
        r.modelDir = std::filesystem::u8path(dir);
    if (!getString(o, "language", r.language, false, err)) return false;
    int64_t i = 0;
    if (!getInt(o, "threads", i, false, err)) return false;
    if (o.find("threads"))
        r.threads = static_cast<int>(i);
    std::string backend;
    if (!getString(o, "backend", backend, false, err)) return false;
    if (const Jv *b = o.find("backend")) {
        if (b->kind != Jv::Str) {
            if (err)
                *err = hpe("bad string field: backend");
            return false;
        }
        const auto parsed = parseBackend(backend);
        if (!parsed) {
            if (err)
                *err = hpe("unknown backend");
            return false;
        }
        r.backend = *parsed;
    }
    if (const Jv *v = getObj(o, "vad", false, err)) {
        if (!decodeVad(*v, r.vad, err))
            return false;
    } else if (err && !err->ok()) {
        return false;
    }
    if (!getString(o, "initialPrompt", r.initialPrompt, false, err)) return false;
    return true;
}

bool decodeLiveConfig(const Jv &o, LiveConfig &c, Status *err)
{
    int64_t i = 0;
    const auto intField = [&](const char *key, int &dst) -> bool {
        if (!getInt(o, key, i, false, err))
            return false;
        if (o.find(key))
            dst = static_cast<int>(i);
        return true;
    };
    return intField("partialIntervalMs", c.partialIntervalMs)
        && intField("maxSegmentMs", c.maxSegmentMs)
        && intField("endpointSilenceMs", c.endpointSilenceMs)
        && intField("appendThrottleMs", c.appendThrottleMs)
        && intField("partFlushIntervalMs", c.partFlushIntervalMs)
        && intField("threadsWhileRecording", c.threadsWhileRecording)
        && intField("agreementN", c.agreementN)
        && intField("confirmGuardMs", c.confirmGuardMs)
        && intField("provisionalMinSegmentMs", c.provisionalMinSegmentMs)
        && intField("revisableCues", c.revisableCues)
        && getBool(o, "writeConfirmedToPart", c.writeConfirmedToPart, false, err);
}

bool decodeWord(const Jv &o, Word &w, Status *err)
{
    if (!getString(o, "t", w.text, true, err)) return false;
    int64_t b = 0, e = 0;
    if (!getInt(o, "b", b, true, err)) return false;
    if (!getInt(o, "e", e, true, err)) return false;
    w.beginMs = b;
    w.endMs = e;
    double p = 0;
    if (!getDouble(o, "p", p, false, err)) return false;
    if (o.find("p"))
        w.prob = static_cast<float>(p);
    return true;
}

bool decodeWords(const std::vector<Jv> &arr, std::vector<Word> &out, Status *err)
{
    out.clear();
    out.reserve(arr.size());
    for (const Jv &item : arr) {
        if (item.kind != Jv::Obj) {
            if (err)
                *err = hpe("word entry is not an object");
            return false;
        }
        Word w;
        if (!decodeWord(item, w, err))
            return false;
        out.push_back(std::move(w));
    }
    return true;
}

std::string encodeWordCore(const Word &w)
{
    Obj o;
    o.str("t", w.text);
    o.i64("b", w.beginMs);
    o.i64("e", w.endMs);
    return o.done();
}

std::string encodeWordWithProb(const Word &w)
{
    Obj o;
    o.str("t", w.text);
    o.i64("b", w.beginMs);
    o.i64("e", w.endMs);
    if (w.prob >= 0)
        o.f32("p", w.prob);
    return o.done();
}

std::string encodeWords(const std::vector<Word> &words, bool withProb)
{
    std::string s = "[";
    for (size_t i = 0; i < words.size(); ++i) {
        if (i)
            s += ',';
        s += withProb ? encodeWordWithProb(words[i]) : encodeWordCore(words[i]);
    }
    s += ']';
    return s;
}

} // namespace

// ============================ encode ============================

std::string encode(const Message &m)
{
    const std::string type = typeName(m);
    // hello（主→子 / 子→主同构）
    if (const auto *h = std::get_if<Hello>(&m)) {
        Obj o;
        o.str("type", type);
        o.i64("protocol", h->protocol);
        if (!h->role.empty())
            o.str("role", h->role);
        if (!h->version.empty())
            o.str("version", h->version);
        if (!h->backends.empty())
            o.raw("backends", strArray(h->backends));
        if (!h->gpus.empty()) {
            std::string s = "[";
            for (size_t i = 0; i < h->gpus.size(); ++i) {
                if (i)
                    s += ',';
                Obj g;
                g.str("name", h->gpus[i].name);
                g.i64("vramMB", h->gpus[i].vramMB);
                g.str("api", h->gpus[i].api);
                s += g.done();
            }
            s += ']';
            o.raw("gpus", s);
        }
        return o.done();
    }
    if (const auto *d = std::get_if<Load>(&m)) {
        const EngineRequest &r = d->request;
        Obj vad;
        vad.f32("threshold", r.vad.threshold);
        vad.i64("minSpeechMs", r.vad.minSpeechMs);
        vad.i64("minSilenceMs", r.vad.minSilenceMs);
        vad.i64("speechPadMs", r.vad.speechPadMs);
        vad.i64("maxSegmentMs", r.vad.maxSegmentMs);
        Obj req;
        req.str("engineId", r.engineId);
        req.str("modelId", r.modelId);
        if (!r.modelDir.empty())
            req.str("modelDir", r.modelDir.u8string());
        req.str("language", r.language);
        req.i64("threads", r.threads);
        req.str("backend", backendName(r.backend));
        req.raw("vad", vad.done());
        req.str("initialPrompt", r.initialPrompt);
        const LiveConfig &c = d->live;
        Obj live;
        live.i64("partialIntervalMs", c.partialIntervalMs);
        live.i64("maxSegmentMs", c.maxSegmentMs);
        live.i64("endpointSilenceMs", c.endpointSilenceMs);
        live.i64("appendThrottleMs", c.appendThrottleMs);
        live.i64("partFlushIntervalMs", c.partFlushIntervalMs);
        live.i64("threadsWhileRecording", c.threadsWhileRecording);
        live.i64("agreementN", c.agreementN);
        live.i64("confirmGuardMs", c.confirmGuardMs);
        live.boolean("writeConfirmedToPart", c.writeConfirmedToPart);
        live.i64("provisionalMinSegmentMs", c.provisionalMinSegmentMs);
        live.i64("revisableCues", c.revisableCues);
        Obj o;
        o.str("type", type);
        o.raw("request", req.done());
        o.raw("live", live.done());
        if (!d->fakeOptions.empty())
            o.str("fakeOptions", d->fakeOptions);
        return o.done();
    }
    if (const auto *p = std::get_if<Open>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.str("spoolPath", p->spoolPath);
        o.i64("resumeFrom16k", p->resumeFrom16k);
        o.i64("committed16k", p->committed16k);
        o.boolean("live", p->live);
        return o.done();
    }
    if (const auto *p = std::get_if<Append>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.i64("committed16k", p->committed16k);
        return o.done();
    }
    if (const auto *p = std::get_if<Splice>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.i64("at16k", p->at16k);
        return o.done();
    }
    if (const auto *p = std::get_if<SetLevel>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.i64("level", p->level);
        o.i64("threads", p->threads);
        o.i64("partialIntervalMs", p->partialIntervalMs);
        return o.done();
    }
    if (const auto *p = std::get_if<Pause>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        return o.done();
    }
    if (const auto *p = std::get_if<Resume>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        return o.done();
    }
    if (const auto *p = std::get_if<Finish>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.i64("total16k", p->total16k);
        return o.done();
    }
    if (const auto *p = std::get_if<Cancel>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        return o.done();
    }
    if (std::holds_alternative<Quit>(m)) {
        Obj o;
        o.str("type", type);
        return o.done();
    }
    if (const auto *p = std::get_if<Refine>(&m)) {
        std::string segs = "[";
        for (size_t i = 0; i < p->segments.size(); ++i) {
            if (i)
                segs += ',';
            Obj s;
            s.i64("id", p->segments[i].id);
            s.i64("b16k", p->segments[i].b16k);
            s.i64("e16k", p->segments[i].e16k);
            segs += s.done();
        }
        segs += ']';
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.str("spoolPath", p->spoolPath);
        o.raw("segments", segs);
        o.str("prompt", p->prompt);
        return o.done();
    }
    if (const auto *p = std::get_if<Loaded>(&m)) {
        Obj o;
        o.str("type", type);
        o.str("backend", p->backend);
        o.i64("loadMs", p->loadMs);
        if (!p->fallbackReason.empty())
            o.str("fallbackReason", p->fallbackReason);
        return o.done();
    }
    if (const auto *p = std::get_if<Heartbeat>(&m)) {
        Obj o;
        o.str("type", type);
        o.i64("seq", p->seq);
        o.str("state", p->state);
        o.i64("rssMB", p->rssMB);
        return o.done();
    }
    if (const auto *p = std::get_if<Busy>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.str("kind", p->kind);
        o.i64("b16k", p->b16k);
        o.i64("e16k", p->e16k);
        return o.done();
    }
    if (const auto *p = std::get_if<PartialMsg>(&m)) {
        const PartialSegment &s = p->seg;
        Obj o;
        o.str("type", type);
        o.u64("jobId", s.job);
        o.i64("id", s.id);
        o.i64("b16k", s.begin16k);
        o.i64("upto16k", s.upto16k);
        o.str("text", s.text);
        o.raw("tokens", encodeWords(s.tokens, false));
        o.u32("seq", s.seq);
        return o.done();
    }
    if (const auto *p = std::get_if<FinalMsg>(&m)) {
        const FinalSegment &s = p->seg;
        Obj o;
        o.str("type", type);
        o.u64("jobId", s.job);
        o.i64("id", s.id);
        o.i64("b16k", s.begin16k);
        o.i64("e16k", s.end16k);
        o.str("text", s.text);
        o.str("lang", s.language);
        o.str("emotion", s.emotion);
        o.str("event", s.event);
        o.raw("words", encodeWords(s.words, true));
        o.boolean("forcedCut", s.forcedCut);
        o.f32("decodeMs", s.decodeMs);
        o.i64("pass", s.pass);
        o.str("modelId", s.modelId);
        return o.done();
    }
    if (const auto *p = std::get_if<Progress>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.i64("done16k", p->done16k);
        o.f64("decodeMsTotal", p->decodeMsTotal);
        return o.done();
    }
    if (const auto *p = std::get_if<Done>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        return o.done();
    }
    if (const auto *p = std::get_if<Cancelled>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        return o.done();
    }
    if (const auto *p = std::get_if<Error>(&m)) {
        Obj o;
        o.str("type", type);
        o.u64("jobId", p->jobId);
        o.i64("code", p->code);
        o.str("message", p->message);
        return o.done();
    }
    if (const auto *p = std::get_if<Log>(&m)) {
        Obj o;
        o.str("type", type);
        o.str("level", p->level);
        o.str("text", p->text);
        return o.done();
    }
    Obj o;
    o.str("type", type);
    return o.done();
}

// ============================ decode ============================

std::optional<Message> decode(std::string_view line, Status *err)
{
    if (line.size() > kMaxLineBytes) {
        if (err)
            *err = hpe("line exceeds kMaxLineBytes");
        return std::nullopt;
    }
    Jv root;
    std::string perr;
    Parser parser(line);
    if (!parser.parse(root, perr) || root.kind != Jv::Obj) {
        if (err)
            *err = hpe(perr.empty() ? "not a json object" : perr.c_str());
        return std::nullopt;
    }
    const Jv *typeV = root.find("type");
    if (!typeV || typeV->kind != Jv::Str) {
        if (err)
            *err = hpe("missing or non-string field: type");
        return std::nullopt;
    }
    const std::string &type = typeV->str;
    // 失败路径把错误写进 *err（调用方只在返回 nullopt 时读取）；成功路径不触碰。
    Status localStatus;
    Status &st = err ? *err : localStatus;
    auto out = std::optional<Message>();

    auto fail = [&](const char *what) -> std::optional<Message> {
        if (err)
            *err = hpe(what);
        return std::nullopt;
    };

    // 各消息的必需字段见文件头注释；以下 lambda 返回 false 时 err 已填好。
    if (type == "hello") {
        Hello h;
        int64_t i = 0;
        if (!getInt(root, "protocol", i, true, &st)) return std::nullopt;
        h.protocol = static_cast<int>(i);
        if (!getString(root, "role", h.role, false, &st)) return std::nullopt;
        if (!getString(root, "version", h.version, false, &st)) return std::nullopt;
        if (const auto *arr = getArr(root, "backends", false, &st)) {
            h.backends.reserve(arr->size());
            for (const Jv &b : *arr) {
                if (b.kind != Jv::Str)
                    return fail("backends entry is not a string");
                h.backends.push_back(b.str);
            }
        } else if (!st.ok()) {
            return std::nullopt;
        }
        if (const auto *arr = getArr(root, "gpus", false, &st)) {
            h.gpus.reserve(arr->size());
            for (const Jv &g : *arr) {
                if (g.kind != Jv::Obj)
                    return fail("gpus entry is not an object");
                Hello::Gpu gpu;
                if (!getString(g, "name", gpu.name, true, &st)) return std::nullopt;
                int64_t vram = 0;
                if (!getInt(g, "vramMB", vram, true, &st)) return std::nullopt;
                gpu.vramMB = static_cast<int>(vram);
                if (!getString(g, "api", gpu.api, false, &st)) return std::nullopt;
                h.gpus.push_back(std::move(gpu));
            }
        } else if (!st.ok()) {
            return std::nullopt;
        }
        out = std::move(h);
    } else if (type == "load") {
        Load d;
        if (const Jv *req = getObj(root, "request", true, &st)) {
            if (!decodeEngineRequest(*req, d.request, &st))
                return std::nullopt;
        } else {
            if (err)
                *err = st;
            return std::nullopt;
        }
        if (const Jv *live = getObj(root, "live", false, &st)) {
            if (!decodeLiveConfig(*live, d.live, &st))
                return std::nullopt;
        } else if (!st.ok()) {
            return std::nullopt;
        }
        if (!getString(root, "fakeOptions", d.fakeOptions, false, &st)) return std::nullopt;
        out = std::move(d);
    } else if (type == "open") {
        Open p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getString(root, "spoolPath", p.spoolPath, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "resumeFrom16k", p.resumeFrom16k, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "committed16k", p.committed16k, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getBool(root, "live", p.live, false, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "append") {
        Append p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getInt(root, "committed16k", p.committed16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "splice") {
        Splice p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getInt(root, "at16k", p.at16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "setLevel") {
        SetLevel p;
        int64_t job = 0, level = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getInt(root, "level", level, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.level = static_cast<int>(level);
        int64_t threads = 0, partialMs = 0;
        if (!getInt(root, "threads", threads, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("threads"))
            p.threads = static_cast<int>(threads);
        if (!getInt(root, "partialIntervalMs", partialMs, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("partialIntervalMs"))
            p.partialIntervalMs = static_cast<int>(partialMs);
        out = std::move(p);
    } else if (type == "pause") {
        Pause p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        out = std::move(p);
    } else if (type == "resume") {
        Resume p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        out = std::move(p);
    } else if (type == "finish") {
        Finish p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getInt(root, "total16k", p.total16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "cancel") {
        Cancel p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        out = std::move(p);
    } else if (type == "quit") {
        out = Quit{};
    } else if (type == "refine") {
        Refine p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getString(root, "spoolPath", p.spoolPath, true, &st)) { if (err) *err = st; return std::nullopt; }
        const auto *arr = getArr(root, "segments", true, &st);
        if (!arr) {
            if (err)
                *err = st;
            return std::nullopt;
        }
        p.segments.reserve(arr->size());
        for (const Jv &item : *arr) {
            if (item.kind != Jv::Obj)
                return fail("refine segment entry is not an object");
            RefineSegment s;
            if (!getInt(item, "id", s.id, true, &st)) { if (err) *err = st; return std::nullopt; }
            if (!getInt(item, "b16k", s.b16k, true, &st)) { if (err) *err = st; return std::nullopt; }
            if (!getInt(item, "e16k", s.e16k, true, &st)) { if (err) *err = st; return std::nullopt; }
            p.segments.push_back(s);
        }
        if (!getString(root, "prompt", p.prompt, false, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "loaded") {
        Loaded p;
        if (!getString(root, "backend", p.backend, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "loadMs", p.loadMs, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "fallbackReason", p.fallbackReason, false, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "hb") {
        Heartbeat p;
        if (!getInt(root, "seq", p.seq, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "state", p.state, true, &st)) { if (err) *err = st; return std::nullopt; }
        int64_t rss = 0;
        if (!getInt(root, "rssMB", rss, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("rssMB"))
            p.rssMB = static_cast<int>(rss);
        out = std::move(p);
    } else if (type == "busy") {
        Busy p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getString(root, "kind", p.kind, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "b16k", p.b16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "e16k", p.e16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "partial") {
        PartialMsg p;
        PartialSegment &s = p.seg;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        s.job = static_cast<JobId>(job);
        if (!getInt(root, "id", s.id, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "b16k", s.begin16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "upto16k", s.upto16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "text", s.text, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (const auto *arr = getArr(root, "tokens", false, &st)) {   // 旧宿主可不带 tokens
            if (!decodeWords(*arr, s.tokens, &st)) {
                if (err)
                    *err = st;
                return std::nullopt;
            }
        } else if (!st.ok()) {
            return std::nullopt;
        }
        int64_t seq = 0;
        if (!getInt(root, "seq", seq, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("seq"))
            s.seq = static_cast<uint32_t>(seq);
        out = std::move(p);
    } else if (type == "final") {
        FinalMsg p;
        FinalSegment &s = p.seg;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        s.job = static_cast<JobId>(job);
        if (!getInt(root, "id", s.id, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "b16k", s.begin16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getInt(root, "e16k", s.end16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "text", s.text, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "lang", s.language, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "emotion", s.emotion, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "event", s.event, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (const auto *arr = getArr(root, "words", false, &st)) {
            if (!decodeWords(*arr, s.words, &st)) {
                if (err)
                    *err = st;
                return std::nullopt;
            }
        } else if (!st.ok()) {
            return std::nullopt;
        }
        if (!getBool(root, "forcedCut", s.forcedCut, false, &st)) { if (err) *err = st; return std::nullopt; }
        double decodeMs = 0;
        if (!getDouble(root, "decodeMs", decodeMs, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("decodeMs"))
            s.decodeMs = static_cast<float>(decodeMs);
        int64_t pass = 0;
        if (!getInt(root, "pass", pass, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("pass"))
            s.pass = static_cast<int>(pass);
        if (!getString(root, "modelId", s.modelId, false, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "progress") {
        Progress p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        if (!getInt(root, "done16k", p.done16k, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getDouble(root, "decodeMsTotal", p.decodeMsTotal, false, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "done") {
        Done p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        out = std::move(p);
    } else if (type == "cancelled") {
        Cancelled p;
        int64_t job = 0;
        if (!getInt(root, "jobId", job, true, &st)) { if (err) *err = st; return std::nullopt; }
        p.jobId = static_cast<JobId>(job);
        out = std::move(p);
    } else if (type == "error") {
        Error p;
        int64_t job = 0, code = 0;
        if (!getInt(root, "jobId", job, false, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("jobId"))
            p.jobId = static_cast<JobId>(job);
        if (!getInt(root, "code", code, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (root.find("code"))
            p.code = static_cast<int>(code);
        if (!getString(root, "message", p.message, false, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else if (type == "log") {
        Log p;
        if (!getString(root, "level", p.level, true, &st)) { if (err) *err = st; return std::nullopt; }
        if (!getString(root, "text", p.text, true, &st)) { if (err) *err = st; return std::nullopt; }
        out = std::move(p);
    } else {
        if (err)
            *err = Status::error(ErrorCode::NotSupported, "unknown type: " + type);
        return std::nullopt;
    }
    return out;
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
