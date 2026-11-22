#pragma once
// tests/asr_host：极简测试框架（纯 C++17，无外部依赖）。
// 用法：TEST(name) { CHECK(...); } 定义用例；test_main.cpp 聚合，ctest 按名字单独运行（可设 TIMEOUT）。

#include <functional>
#include <string>
#include <vector>

struct TestCase {
    std::string name;
    void (*fn)();
};

std::vector<TestCase> &testRegistry();
int runTestsSelected(int argc, char **argv);   // 无参：全部；有参：只跑指定名字

struct TestFailure {
    std::string what;
};

void throwFail(const char *file, int line, const std::string &msg);

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond))                                                                 \
            throwFail(__FILE__, __LINE__, "CHECK failed: " #cond);                   \
    } while (0)

#define CHECK_MSG(cond, msg)                                                         \
    do {                                                                             \
        if (!(cond))                                                                 \
            throwFail(__FILE__, __LINE__, (msg));                                    \
    } while (0)

#define TEST(name)                                                                   \
    static void test_##name();                                                       \
    static const bool reg_##name =                                                   \
        (testRegistry().push_back({#name, &test_##name}), true);                     \
    static void test_##name()
