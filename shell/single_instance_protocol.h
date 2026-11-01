#pragma once

// Wire format shared by both sides of SingleInstance (S4). Header-only, no Qt, so it
// is unit-testable anywhere. One ASCII line per message, '\n'-terminated.
//   secondary -> primary : "show\n" | "noop\n"
//   primary   -> secondary: "ok\n" (only for a recognized request)

#include <string>
#include <string_view>

namespace subrec::shell::single_instance_protocol {

enum class Parsed { Show, Noop, Invalid };

inline constexpr std::string_view kShow = "show";
inline constexpr std::string_view kNoop = "noop";
inline constexpr std::string_view kAck = "ok";
inline constexpr std::size_t kMaxLine = 16; // anything longer is garbage

inline std::string requestLine(bool show)
{
    return std::string(show ? kShow : kNoop) + '\n';
}

inline std::string ackLine() { return std::string(kAck) + '\n'; }

// Trims trailing CR/LF/spaces; exact, case-sensitive match.
inline std::string_view trimLine(std::string_view line)
{
    while (!line.empty()) {
        const char c = line.back();
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
        line.remove_suffix(1);
    }
    return line;
}

inline Parsed parseRequest(std::string_view line)
{
    if (line.size() > kMaxLine) return Parsed::Invalid;
    line = trimLine(line);
    if (line == kShow) return Parsed::Show;
    if (line == kNoop) return Parsed::Noop;
    return Parsed::Invalid;
}

inline bool isAck(std::string_view line)
{
    return line.size() <= kMaxLine && trimLine(line) == kAck;
}

// Server name: per user, so different accounts on one machine never collide.
// Characters outside [A-Za-z0-9_.-] are replaced with '_'.
inline std::string serverNameFor(std::string_view userName)
{
    std::string out = "SubRec.SingleInstance.";
    if (userName.empty()) userName = "unknown";
    for (char c : userName) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                        || c == '_' || c == '.' || c == '-';
        out.push_back(ok ? c : '_');
    }
    return out;
}

} // namespace subrec::shell::single_instance_protocol
