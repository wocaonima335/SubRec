// 父进程看门（工作包 C，design-live-v2.md §7.3「父进程死亡」一行）。
//
// Windows：OpenProcess(SYNCHRONIZE) + WaitForSingleObject，父进程句柄 signaled 即 TerminateProcess
// 自杀（即使解码线程已挂死也能退出）。POSIX：看门线程轮询 kill(pid, 0)（ESRCH = 父进程已消失）。
// stdin EOF 是另一条互为冗余的退出路径（管道断开），两者都保证子进程不残留。

#include "host_runtime.h"

#include "subtitle/subtitle_error.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#endif

namespace asrhost {

namespace {

void defaultParentGone()
{
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), 0);
#else
    std::_Exit(0);
#endif
}

} // namespace

subtitle::Status ParentWatch::start(int64_t parentPid, std::function<void()> onParentGone)
{
    if (parentPid <= 0)
        return {};
    if (!onParentGone)
        onParentGone = defaultParentGone;

#if defined(_WIN32)
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parentPid));
    if (!parent)
        return subtitle::Status::error(subtitle::ErrorCode::InvalidArgument,
                                       "OpenProcess(parent) failed");
    std::thread watcher([parent, cb = std::move(onParentGone)] {
        WaitForSingleObject(parent, INFINITE);
        CloseHandle(parent);
        cb();
    });
    watcher.detach();
#else
    std::thread watcher([pid = static_cast<pid_t>(parentPid), cb = std::move(onParentGone)] {
        for (;;) {
            if (kill(pid, 0) == -1 && errno == ESRCH)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        cb();
    });
    watcher.detach();
#endif
    return {};
}

} // namespace asrhost
