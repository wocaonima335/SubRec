#include "subtitle/core/subtitle_writers.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace subtitle {
#ifdef SUBREC_CORE_TEST_SEAM
// Only the dedicated test executable compiles this symbol. No environment knob,
// public header or production ABI: fail after this many bytes reach the temp file.
namespace testing { thread_local int64_t atomicFailAfter = -1; }
#endif
namespace {
std::string newlineText(std::string_view text, std::string_view nl)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' || text[i] == '\n') {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out += nl;
        } else out += text[i];
    }
    return out;
}
std::string escapeVtt(std::string_view text)
{
    std::string out;
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}
std::string assTime(int64_t ms)
{
    ms = std::max<int64_t>(0, ms);
    std::ostringstream s; s.imbue(std::locale::classic());
    s << ms / 3600000 << ':' << std::setfill('0') << std::setw(2) << ms / 60000 % 60
      << ':' << std::setw(2) << ms / 1000 % 60 << '.' << std::setw(2) << ms / 10 % 100;
    return s.str();
}
std::string assText(std::string_view text)
{
    std::string out;
    for (char c : newlineText(text, "\n")) {
        if (c == '\n') out += "\\N";
        else if (c == '\\') out += "\\\\";
        else if (c == '{') out += "\\{";
        else if (c == '}') out += "\\}";
        else out += c;
    }
    return out;
}
void appendUtf8(std::string &s, uint32_t cp)
{
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) { s += char(0xc0 | (cp >> 6)); s += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) {
        s += char(0xe0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63));
    } else {
        s += char(0xf0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 63));
        s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63));
    }
}
bool validUtf8(std::string_view s)
{
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i++]);
        if (c < 0x80) continue;
        const int n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!n || s.size() - i < size_t(n)) return false;
        uint32_t cp = c & (0x7f >> (n + 1));
        for (int j = 0; j < n; ++j) {
            const auto b = static_cast<unsigned char>(s[i++]);
            if ((b & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (b & 63);
        }
        if ((n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
std::string quote(std::string_view s)
{
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
std::string number(float f)
{
    if (!std::isfinite(f)) return "null";
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::setprecision(std::numeric_limits<float>::max_digits10) << f;
    return s.str();
}
// A small strict JSON reader keeps the pure C++17 core independent of Qt and IPC.
// Number spelling is retained so int64 values never pass through a double.
struct Json {
    enum Kind { Null, Boolean, Number, String, Array, Object } kind = Null;
    std::string value;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};
struct JsonError {};
class JsonReader {
    std::string_view input;
    size_t at = 0;
    void ws() { while (at < input.size() && (input[at] == ' ' || input[at] == '\n' || input[at] == '\r' || input[at] == '\t')) ++at; }
    char take() { if (at == input.size()) throw JsonError{}; return input[at++]; }
    bool eat(char c) { ws(); if (at < input.size() && input[at] == c) { ++at; return true; } return false; }
    uint32_t hex4() {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = take();
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) throw JsonError{};
            n = n * 16 + unsigned(d);
        }
        return n;
    }
    std::string string() {
        if (take() != '"') throw JsonError{};
        std::string out;
        for (;;) {
            const auto c = static_cast<unsigned char>(take());
            if (c == '"') break;
            if (c < 32) throw JsonError{};
            if (c != '\\') { out += char(c); continue; }
            switch (take()) {
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break;
            case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (take() != '\\' || take() != 'u') throw JsonError{};
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) throw JsonError{};
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) throw JsonError{};
                appendUtf8(out, cp); break;
            }
            default: throw JsonError{};
            }
        }
        if (!validUtf8(out)) throw JsonError{};
        return out;
    }
    Json value(int depth) {
        if (depth > 64) throw JsonError{};
        ws(); if (at == input.size()) throw JsonError{};
        Json v;
        if (input[at] == '"') { v.kind = Json::String; v.value = string(); }
        else if (eat('[')) {
            v.kind = Json::Array;
            if (!eat(']')) do { v.array.push_back(value(depth + 1)); if (eat(']')) return v; } while (eat(','));
            else return v;
            throw JsonError{};
        } else if (eat('{')) {
            v.kind = Json::Object;
            if (!eat('}')) do {
                ws(); auto key = string(); if (!eat(':')) throw JsonError{};
                if (!v.object.emplace(std::move(key), value(depth + 1)).second) throw JsonError{};
                if (eat('}')) return v;
            } while (eat(','));
            else return v;
            throw JsonError{};
        } else if (input.substr(at, 4) == "null") { at += 4; }
        else if (input.substr(at, 4) == "true") { at += 4; v.kind = Json::Boolean; }
        else if (input.substr(at, 5) == "false") { at += 5; v.kind = Json::Boolean; }
        else {
            v.kind = Json::Number;
            const size_t start = at;
            if (input[at] == '-') ++at;
            if (at == input.size()) throw JsonError{};
            if (input[at] == '0') ++at;
            else { if (input[at] < '1' || input[at] > '9') throw JsonError{}; digits(); }
            if (at < input.size() && input[at] == '.') { ++at; digits(); }
            if (at < input.size() && (input[at] == 'e' || input[at] == 'E')) {
                ++at; if (at < input.size() && (input[at] == '+' || input[at] == '-')) ++at;
                digits();
            }
            v.value = std::string(input.substr(start, at - start));
        }
        return v;
    }
    void digits() {
        const size_t start = at;
        while (at < input.size() && input[at] >= '0' && input[at] <= '9') ++at;
        if (at == start) throw JsonError{};
    }
public:
    explicit JsonReader(std::string_view s) : input(s) {}
    Json read() { auto v = value(0); ws(); if (at != input.size()) throw JsonError{}; return v; }
};
const Json &field(const Json &v, const char *key)
{
    if (v.kind != Json::Object) throw JsonError{};
    const auto it = v.object.find(key);
    if (it == v.object.end()) throw JsonError{};
    return it->second;
}
int64_t integer(const Json &v)
{
    if (v.kind != Json::Number) throw JsonError{};
    int64_t n;
    const auto r = std::from_chars(v.value.data(), v.value.data() + v.value.size(), n);
    if (r.ec != std::errc{} || r.ptr != v.value.data() + v.value.size()) throw JsonError{};
    return n;
}
std::string stringValue(const Json &v) { if (v.kind != Json::String) throw JsonError{}; return v.value; }
float floating(const Json &v)
{
    if (v.kind == Json::Null) return NAN;
    if (v.kind != Json::Number) throw JsonError{};
    std::istringstream s(v.value); s.imbue(std::locale::classic());
    float f; s >> f;
    if (!s || !std::isfinite(f)) throw JsonError{};
    return f;
}
bool decimal(std::string_view s, int64_t &n)
{
    if (s.empty() || s.front() < '0' || s.front() > '9') return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
bool timestamp(std::string_view s, int64_t &ms)
{
    const auto colon = s.find(':');
    if (colon == std::string_view::npos || colon < 2 || s.size() != colon + 10 || s[colon + 3] != ':' || s[colon + 6] != ',') return false;
    int64_t h, m, sec, milli;
    if (!decimal(s.substr(0, colon), h) || !decimal(s.substr(colon + 1, 2), m) ||
        !decimal(s.substr(colon + 4, 2), sec) || !decimal(s.substr(colon + 7, 3), milli) || m >= 60 || sec >= 60) return false;
    const int64_t rest = m * 60000 + sec * 1000 + milli;
    if (h > (std::numeric_limits<int64_t>::max() - rest) / 3600000) return false;
    ms = h * 3600000 + rest;
    return true;
}
} // namespace

std::string formatSrtTimestamp(int64_t ms)
{
    ms = std::max<int64_t>(0, ms);
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::setfill('0') << std::setw(2) << ms / 3600000 << ':' << std::setw(2) << ms / 60000 % 60
      << ':' << std::setw(2) << ms / 1000 % 60 << ',' << std::setw(3) << ms % 1000;
    return s.str();
}
std::string formatSrt(const std::vector<Cue> &cues, bool bom, bool crlf)
{
    std::string out = bom ? "\xef\xbb\xbf" : "";
    const std::string nl = crlf ? "\r\n" : "\n";
    size_t index = 0;
    for (const auto &cue : cues) if (!cue.text.empty()) {
        out += std::to_string(++index) + nl + formatSrtTimestamp(cue.beginMs) + " --> " + formatSrtTimestamp(cue.endMs) + nl;
        out += newlineText(cue.text, nl) + nl + nl;
    }
    return out;
}
std::string formatSrt(const std::vector<Cue> &cues, const SrtOptions &o) { return formatSrt(cues, o.bom, o.crlf); }
std::string formatVtt(const std::vector<Cue> &cues)
{
    // WebVTT: UTF-8 without BOM, canonical CRLF; optional cue identifiers omitted.
    std::string out = "WEBVTT\r\n\r\n";
    for (const auto &cue : cues) if (!cue.text.empty()) {
        auto b = formatSrtTimestamp(cue.beginMs), e = formatSrtTimestamp(cue.endMs);
        b[b.size() - 4] = '.'; e[e.size() - 4] = '.';
        out += b + " --> " + e + "\r\n" + newlineText(escapeVtt(cue.text), "\r\n") + "\r\n\r\n";
    }
    return out;
}
std::string formatAss(const std::vector<Cue> &cues, const AssStyle &style)
{
    std::string font = style.fontName;
    for (char &c : font) if (c == ',' || c == '\r' || c == '\n') c = ' ';
    std::string out = "[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 1920\r\nPlayResY: 1080\r\nWrapStyle: 0\r\n\r\n"
        "[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
        "Style: Default," + font + "," + std::to_string(std::max(1, style.fontSize)) +
        ",&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,1,2,20,20," +
        std::to_string(std::max(0, style.marginV)) + ",1\r\n\r\n[Events]\r\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
    for (const auto &cue : cues) if (!cue.text.empty())
        out += "Dialogue: 0," + assTime(cue.beginMs) + "," + assTime(cue.endMs) + ",Default,,0,0,0,," + assText(cue.text) + "\r\n";
    return out;
}
std::string toJson(const std::vector<Segment> &segments)
{
    std::string out = "[";
    for (const auto &s : segments) {
        if (out.size() > 1) out += ',';
        out += "{\"id\":" + std::to_string(s.id) + ",\"begin16k\":" + std::to_string(s.begin16k) +
            ",\"end16k\":" + std::to_string(s.end16k) + ",\"text\":" + quote(s.text) +
            ",\"language\":" + quote(s.language) + ",\"avgLogProb\":" + number(s.avgLogProb) +
            ",\"noSpeechProb\":" + number(s.noSpeechProb) + ",\"words\":[";
        bool first = true;
        for (const auto &w : s.words) {
            if (!first) out += ',';
            first = false;
            out += "{\"beginMs\":" + std::to_string(w.beginMs) + ",\"endMs\":" + std::to_string(w.endMs) +
                ",\"text\":" + quote(w.text) + ",\"prob\":" + number(w.prob) + '}';
        }
        out += "]}";
    }
    return out + ']';
}
bool fromJson(std::string_view input, std::vector<Segment> &out)
{
    try {
        const auto root = JsonReader(input).read();
        if (root.kind != Json::Array) return false;
        std::vector<Segment> parsed;
        for (const auto &v : root.array) {
            Segment s;
            s.id = integer(field(v, "id")); s.begin16k = integer(field(v, "begin16k")); s.end16k = integer(field(v, "end16k"));
            s.text = stringValue(field(v, "text"));
            if (v.object.count("language")) s.language = stringValue(field(v, "language"));
            if (v.object.count("avgLogProb")) s.avgLogProb = floating(field(v, "avgLogProb"));
            if (v.object.count("noSpeechProb")) s.noSpeechProb = floating(field(v, "noSpeechProb"));
            if (v.object.count("words")) {
                const auto &words = field(v, "words");
                if (words.kind != Json::Array) return false;
                for (const auto &w : words.array) {
                    Word word;
                    word.beginMs = integer(field(w, "beginMs")); word.endMs = integer(field(w, "endMs"));
                    word.text = stringValue(field(w, "text"));
                    if (w.object.count("prob")) word.prob = floating(field(w, "prob"));
                    s.words.push_back(std::move(word));
                }
            }
            parsed.push_back(std::move(s));
        }
        out = std::move(parsed);
        return true;
    } catch (const JsonError &) { return false; }
}
bool parseSrt(std::string_view input, std::vector<Cue> &out)
{
    if (input.substr(0, 3) == "\xef\xbb\xbf") input.remove_prefix(3);
    if (!validUtf8(input)) return false;
    const auto normalized = newlineText(input, "\n");
    std::istringstream stream(normalized);
    std::string line;
    std::vector<Cue> parsed;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        int64_t index;
        if (!decimal(line, index) || index <= 0 || !std::getline(stream, line)) return false;
        const auto arrow = line.find(" --> ");
        Cue cue;
        if (arrow == std::string::npos || !timestamp(std::string_view(line).substr(0, arrow), cue.beginMs) ||
            !timestamp(std::string_view(line).substr(arrow + 5), cue.endMs) || cue.endMs < cue.beginMs) return false;
        while (std::getline(stream, line) && !line.empty()) {
            if (!cue.text.empty()) cue.text += '\n';
            cue.text += line;
        }
        if (cue.text.empty()) return false;
        parsed.push_back(std::move(cue));
    }
    out = std::move(parsed);
    return true;
}

Status writeFileAtomic(const std::filesystem::path &target, std::string_view bytes)
{
    // The Runner serializes writes to a given target (the frozen .tmp convention).
    std::filesystem::path temp;
    bool ownsTemp = false;
    auto cleanup = [&] { if (ownsTemp) { std::error_code ec; std::filesystem::remove(temp, ec); } };
    try {
        if (target.empty() || target.filename().empty()) return Status::error(ErrorCode::InvalidArgument, "Empty subtitle target");
        temp = target; temp += ".tmp";
#ifdef _WIN32
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return Status::error(ErrorCode::SrtTargetNotWritable, "Cannot create subtitle temporary file");
#else
        const int file = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (file < 0) return Status::error(ErrorCode::SrtTargetNotWritable, "Cannot create subtitle temporary file");
#endif
        ownsTemp = true;
        bool good = true;
        size_t offset = 0;
        while (offset < bytes.size()) {
            size_t count = std::min<size_t>(bytes.size() - offset, 65536);
#ifdef SUBREC_CORE_TEST_SEAM
            if (testing::atomicFailAfter >= 0) {
                if (offset >= uint64_t(testing::atomicFailAfter)) { good = false; break; }
                count = std::min(count, size_t(testing::atomicFailAfter) - offset);
            }
#endif
#ifdef _WIN32
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, DWORD(count), &written, nullptr) || !written) { good = false; break; }
#else
            const auto written = ::write(file, bytes.data() + offset, count);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { good = false; break; }
#endif
            offset += size_t(written);
        }
#ifdef _WIN32
        if (good && !FlushFileBuffers(file)) good = false;
        if (!CloseHandle(file)) good = false;
#else
        if (good) {
            int flushed;
            do { flushed = ::fsync(file); } while (flushed < 0 && errno == EINTR);
            if (flushed < 0) good = false;
        }
        if (::close(file) != 0) good = false;
#endif
        if (!good) { cleanup(); return Status::error(ErrorCode::SrtWriteFailed, "Writing/flushing subtitle temporary file failed"); }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
        if (::rename(temp.c_str(), target.c_str()) != 0) {
#endif
            cleanup(); return Status::error(ErrorCode::SrtRenameFailed, "Replacing subtitle target failed");
        }
        ownsTemp = false;
        return Status::success();
    } catch (const std::bad_alloc &) {
        cleanup(); return Status::error(ErrorCode::OutOfMemory);
    } catch (const std::exception &) {
        cleanup(); return Status::error(ErrorCode::SrtWriteFailed, "Subtitle filesystem operation failed");
    }
}
std::filesystem::path srtPathForMp4(const std::filesystem::path &mp4)
{
    auto p = mp4; return p.replace_extension(".srt");
}
std::filesystem::path srtPartPath(const std::filesystem::path &srt) { auto p = srt; p += ".part"; return p; }
std::filesystem::path liveSrtPath(const std::filesystem::path &srt) { auto p = srt; return p.replace_extension(".live.srt"); }
std::filesystem::path refinePartPath(const std::filesystem::path &srt) { auto p = srt; p += ".refine.part"; return p; }
} // namespace subtitle
