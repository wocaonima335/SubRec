#pragma once
// M0 冻结：进程外假宿主 subrec-asr-fakehost 的脚本格式（design-live-v2.md §11.1 第 2 层）。
// 用途：AsrHostClient / SubtitleJobRunner / HostWatchdog 的协议与故障测试（tests/subtitle_host、tests/subtitle_service），
// 不依赖 sherpa、不需要模型。主进程测试经 AsrHostClientFactory 把 exePath 指向本程序。
// 接口归属：M0（格式是 Codex/ZCode 交汇点）；实现归工作包 C（ZCode 终端）。
//
// 启动：subrec-asr-fakehost --script=<path> [其余 subrec-asr 参数，接受但忽略]
// 脚本：UTF-8 文本，一行一条指令，'#' 开头为注释，空行忽略。按顺序执行：
//   expect <type>         阻塞读 stdin，直到收到 type 相同的命令（其间收到的其他命令写入 stderr 日志后忽略）；
//                         stdin EOF → 以 0 退出
//   send <json>           把 <json>（必须是单行合法 JSON）原样写到协议 stdout
//   reply-hello           按 kProtocolVersion 回 hello{protocol, version:"fake", backends:["cpu"]}
//   reply-hello <n>       回 protocol=n（测试 HostVersionMismatch）
//   hb <ms> | hb off      开始以 <ms> 周期自动发 hb{seq, state:"idle", rssMB:1} / 停止
//   sleep <ms>
//   garbage <text>        写一行非 JSON 文本到协议 stdout（测试 HostProtocolError）
//   longline <bytes>      写一行超长 JSON（测试 kMaxLineBytes）
//   exit <code>           以 <code> 退出
//   crash                 abort()（测试 exited(crashed=true)）
//   hang                  停止一切输出并无限 sleep（测试 hb 超时）
// 脚本执行完仍未退出时：继续发 hb（若开启）并等 quit 或 stdin EOF。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace asrhost::fake {

struct ScriptStep {
    enum class Op { Expect, Send, ReplyHello, Hb, Sleep, Garbage, LongLine, Exit, Crash, Hang } op = Op::Sleep;
    std::string text;          // expect 的 type、send 的 json、garbage 的文本
    int64_t value = 0;         // 毫秒、字节数、退出码、协议版本；hb off 时为 0
    int line = 0;              // 脚本行号（报错用）
};

// 解析失败返回 false，error 为 "<行号>: <原因>"。
bool parseScript(std::string_view text, std::vector<ScriptStep> &out, std::string *error);

} // namespace asrhost::fake
