// tests/asr_host 入口：聚合全部用例；环境变量 SUBREC_ASR_TEST_HOOKS=1 供进程内假引擎单元测试使用
// （不设置子进程的路径——门控测试通过 spawn 的 unsetEnv 控制）。

#include "test_framework.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <cstdlib>
#define SETENV(k, v) _putenv(k "=" v)
#else
#include <csignal>
#include <cstdlib>
extern char **environ;
static void SETENV(const char *k, const char *v) { setenv(k, v, 0 /*不覆盖*/); }
#endif

std::vector<TestCase> &testRegistry()
{
    static std::vector<TestCase> reg;
    return reg;
}

void throwFail(const char *file, int line, const std::string &msg)
{
    throw TestFailure{std::string(file) + ":" + std::to_string(line) + ": " + msg};
}

int runTestsSelected(int argc, char **argv)
{
    auto &reg = testRegistry();
    if (argc > 1) {
        // 按名字过滤（保持注册顺序）
        std::vector<TestCase> sel;
        for (int i = 1; i < argc; ++i) {
            for (const TestCase &t : reg)
                if (t.name == argv[i])
                    sel.push_back(t);
        }
        reg = sel;
    }

    int failed = 0;
    for (const TestCase &t : reg) {
        std::printf("[ RUN  ] %s\n", t.name.c_str());
        std::fflush(stdout);
        try {
            t.fn();
            std::printf("[  OK  ] %s\n", t.name.c_str());
        } catch (const TestFailure &f) {
            ++failed;
            std::printf("[ FAIL ] %s\n        %s\n", t.name.c_str(), f.what.c_str());
        } catch (...) {
            ++failed;
            std::printf("[ FAIL ] %s\n        unknown exception\n", t.name.c_str());
        }
        std::fflush(stdout);
    }
    std::printf("%zu test(s), %d failure(s)\n", reg.size(), failed);
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    SETENV("SUBREC_ASR_TEST_HOOKS", "1");
#ifndef _WIN32
    // 子进程先退出（脚本 exit、崩溃注入）后测试再写它的 stdin 时，POSIX 默认 SIGPIPE 会直接杀掉
    // 测试进程（退出码 141，ctest 偶发失败且无 FAIL 输出）。忽略后写入返回 EPIPE，由用例断言判定。
    // Windows 无 SIGPIPE，WriteFile 返回 ERROR_NO_DATA，不受影响。子进程在 fork 后恢复 SIG_DFL。
    std::signal(SIGPIPE, SIG_IGN);
#endif
    return runTestsSelected(argc, argv);
}
