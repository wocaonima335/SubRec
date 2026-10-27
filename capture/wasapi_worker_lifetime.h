#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace capture::wasapi {
// Owner-thread API. The task owns its resources; it must never capture the owner.
// Detached tasks reclaim themselves when the driver returns, without a process-
// global reaper whose destructor could itself wait forever.
class WorkerLifetime {
public:
    struct Signals {
        std::mutex mutex;
        std::condition_variable cv;
        bool ready = false, done = false, abandoned = false;
        std::exception_ptr error;
        void prepared() {
            std::lock_guard<std::mutex> lock(mutex);
            if (!abandoned) ready = true;
            cv.notify_all();
        }
        bool timedOut() {
            std::lock_guard<std::mutex> lock(mutex);
            return abandoned;
        }
    };
    WorkerLifetime() : signals_(std::make_shared<Signals>()) {}
    ~WorkerLifetime() { if (worker_.joinable()) abandon(); }
    WorkerLifetime(const WorkerLifetime &) = delete;
    WorkerLifetime &operator=(const WorkerLifetime &) = delete;
    std::shared_ptr<Signals> completion() const { return signals_; }
    bool started() const { return started_; }
    void start(std::function<void()> task) {
        if (started_) throw std::logic_error("capture worker is single-use");
        worker_ = std::thread([signal = signals_, task = std::move(task)]() mutable {
            // Production tasks translate exceptions to capture errors themselves.
            std::exception_ptr error;
            try { task(); } catch (...) { error = std::current_exception(); }
            task = {}; // Release owned task resources before advertising completion.
            std::lock_guard<std::mutex> lock(signal->mutex);
            signal->error = error;
            signal->done = true;
            signal->cv.notify_all();
        });
        started_ = true;
    }
    bool waitPrepared(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(signals_->mutex);
        if (!signals_->cv.wait_for(lock, timeout, [&] { return signals_->ready || signals_->done; }) ||
            !signals_->ready) {
            signals_->abandoned = true;
            return false;
        }
        return !signals_->abandoned;
    }
    bool joinFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(signals_->mutex);
        // A previously detached worker must never add another timeout to destruction.
        if (!worker_.joinable()) return !started_ || signals_->done;
        if (!signals_->cv.wait_for(lock, timeout, [&] { return signals_->done; })) return false;
        lock.unlock();
        worker_.join(); // Task and its resource destruction have already completed.
        return true;
    }
    void abandon() {
        { std::lock_guard<std::mutex> lock(signals_->mutex); signals_->abandoned = true; }
        if (worker_.joinable()) worker_.detach();
    }
private:
    std::shared_ptr<Signals> signals_;
    std::thread worker_;
    bool started_ = false;
};

// Shared by the real stop path and the fake blocked-driver/queue tests.
template<class Capture, class CloseQueues, class Log>
bool stopWithDeadline(Capture &capture, CloseQueues closeQueues, Log log,
                      std::chrono::milliseconds drain = std::chrono::seconds(5),
                      std::chrono::milliseconds abort = std::chrono::milliseconds(500)) {
    capture.requestDrainStop();
    if (capture.joinFor(drain)) return true;
    log("drain deadline exceeded; closing queues and aborting");
    closeQueues();
    capture.requestAbort();
    if (capture.joinFor(abort)) return true;
    log("abort deadline exceeded; detached worker retains its resources until exit");
    capture.abandon();
    return false;
}
} // namespace capture::wasapi
