#ifndef LATEST_PREVIEW_SLOT_H
#define LATEST_PREVIEW_SLOT_H
#include <mutex>
#include <utility>

// One pending value, plus at most one owned by the consumer. The producer never waits
// for a consumer holding the lock. Value must release its resource on destruction.
template<class Value> class LatestPreviewSlot
{
public:
    // true if accepted without replacing a pending value; false counts one dropped frame.
    bool offer(Value value) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        const bool empty = !value_;
        value_ = std::move(value);
        return empty;
    }
    Value take() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::move(value_);
    }
private:
    std::mutex mutex_;
    Value value_;
};
#endif
