#include "capture/wasapi_mmcss_scope.h"
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct FakeApi {
    using Handle = void *;
    using TaskIndex = unsigned;
    bool failRegister = false, failRevert = false;
    int registrations = 0, reverts = 0, warnings = 0;
    bool correctTask = true, correctHandle = true, sameThread = true;
    const char *lastOperation = "";
    unsigned reportedError = 0;
    std::thread::id registeredOn;
    Handle registerTask(const wchar_t *name, TaskIndex *index) noexcept {
        ++registrations;
        correctTask = std::wcscmp(name, L"Pro Audio") == 0 && *index == 0;
        *index = 42;
        registeredOn = std::this_thread::get_id();
        return failRegister ? nullptr : this;
    }
    bool revert(Handle handle) noexcept {
        ++reverts;
        correctHandle = handle == this;
        sameThread = registeredOn == std::this_thread::get_id();
        return !failRevert;
    }
    unsigned lastError() noexcept { return failRegister ? 5 : 6; }
    void logError(const char *operation, unsigned error) noexcept {
        ++warnings; lastOperation = operation; reportedError = error;
    }
};
using Scope = capture::wasapi::MmcssScope<FakeApi>;
static_assert(!std::is_copy_constructible_v<Scope> && !std::is_move_constructible_v<Scope>);
static_assert(std::is_nothrow_destructible_v<Scope>);
void run(FakeApi &api, int exitPath) {
    Scope scope(api);
    if (exitPath == 1) return; // cancel/abort before capture starts
    if (exitPath == 2) throw std::runtime_error("capture failure");
}
int main() {
    try {
        for (int exitPath = 0; exitPath < 3; ++exitPath) {
            FakeApi api;
            std::thread worker([&] { try { run(api, exitPath); } catch (const std::runtime_error &) {} });
            worker.join();
            CHECK(api.registrations == 1 && api.reverts == 1 && api.warnings == 0);
            CHECK(api.correctTask && api.correctHandle && api.sameThread);
        }
        FakeApi rejected; rejected.failRegister = true;
        bool continued = false;
        { Scope scope(rejected); continued = true; }
        CHECK(continued && rejected.registrations == 1 && rejected.reverts == 0);
        CHECK(rejected.warnings == 1 && rejected.reportedError == 5);
        CHECK(std::string(rejected.lastOperation) == "AvSetMmThreadCharacteristicsW");
        FakeApi revertFailed; revertFailed.failRevert = true;
        run(revertFailed, 0);
        CHECK(revertFailed.reverts == 1 && revertFailed.warnings == 1 && revertFailed.reportedError == 6);
        CHECK(std::string(revertFailed.lastOperation) == "AvRevertMmThreadCharacteristics");
        std::cout << "PASS: MMCSS Pro Audio registration, same-thread revert, normal/cancel/exception and nonfatal API failures\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
