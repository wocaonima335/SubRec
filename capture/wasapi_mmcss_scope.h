#pragma once

namespace capture::wasapi {
// Owned on the capture thread's stack: revert on that same thread, including
// exception/cancel paths. The small API seam permits failure-path tests without
// Windows or changing a test runner's scheduling priority.
template<class Api>
class MmcssScope {
public:
    explicit MmcssScope(Api &api) noexcept : api_(api) {
        typename Api::TaskIndex index = 0;
        handle_ = api_.registerTask(L"Pro Audio", &index);
        if (!handle_) api_.logError("AvSetMmThreadCharacteristicsW", api_.lastError());
    }
    ~MmcssScope() noexcept {
        if (handle_ && !api_.revert(handle_))
            api_.logError("AvRevertMmThreadCharacteristics", api_.lastError());
    }
    MmcssScope(const MmcssScope &) = delete;
    MmcssScope &operator=(const MmcssScope &) = delete;
private:
    Api &api_;
    typename Api::Handle handle_ = nullptr;
};
}
