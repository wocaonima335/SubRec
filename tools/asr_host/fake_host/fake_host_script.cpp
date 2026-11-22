// 假宿主脚本解析（M0 已冻结的实现，从桩里原样迁出；格式见 fake_host_script.h）。

#include "fake_host_script.h"

#include <cstdlib>
#include <string>

namespace asrhost::fake {

namespace {

bool parseInt(const std::string &s, int64_t &out)
{
    char *end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return !s.empty() && end && *end == '\0';
}

} // namespace

bool parseScript(std::string_view text, std::vector<ScriptStep> &out, std::string *error)
{
    out.clear();
    int lineNo = 0;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string line(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        line = line.substr(first);
        const size_t sp = line.find(' ');
        const std::string op = line.substr(0, sp);
        const std::string arg = sp == std::string::npos ? std::string() : line.substr(sp + 1);

        ScriptStep step;
        step.line = lineNo;
        int64_t v = 0;
        bool ok = true;
        if (op == "expect" && !arg.empty()) { step.op = ScriptStep::Op::Expect; step.text = arg; }
        else if (op == "send" && !arg.empty()) { step.op = ScriptStep::Op::Send; step.text = arg; }
        else if (op == "reply-hello") {
            step.op = ScriptStep::Op::ReplyHello;
            step.value = 0;                       // 0 = 使用 kProtocolVersion
            ok = arg.empty() || parseInt(arg, step.value);
        }
        else if (op == "hb") { step.op = ScriptStep::Op::Hb; ok = arg == "off" || (parseInt(arg, v) && v > 0); step.value = arg == "off" ? 0 : v; }
        else if (op == "sleep") { step.op = ScriptStep::Op::Sleep; ok = parseInt(arg, step.value) && step.value >= 0; }
        else if (op == "garbage") { step.op = ScriptStep::Op::Garbage; step.text = arg; }
        else if (op == "longline") { step.op = ScriptStep::Op::LongLine; ok = parseInt(arg, step.value) && step.value > 0; }
        else if (op == "exit") { step.op = ScriptStep::Op::Exit; ok = parseInt(arg, step.value); }
        else if (op == "crash" && arg.empty()) { step.op = ScriptStep::Op::Crash; }
        else if (op == "hang" && arg.empty()) { step.op = ScriptStep::Op::Hang; }
        else ok = false;
        if (!ok) {
            if (error)
                *error = std::to_string(lineNo) + ": bad step: " + line;
            return false;
        }
        out.push_back(std::move(step));
    }
    return true;
}

} // namespace asrhost::fake
