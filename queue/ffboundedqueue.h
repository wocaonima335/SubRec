#ifndef FFBOUNDEDQUEUE_H
#define FFBOUNDEDQUEUE_H

#include "logging/frame_log.h"

#include "ffpacket.h"
#include "media_queue_common.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <QDebug>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <corecrt.h>
#include <functional>
#include <mutex>
#include <queue>
#include <type_traits>

using namespace std;

extern "C" {
#include <libavformat/avformat.h>
}

struct FFPacketTraits
{
    static FFPacket *allocateFromSrc(FFPacket *src) { return src; }

    static void releaseSrc(FFPacket *src)
    {
        // enqueue owns this moved packet even when EOF/close rejects it.
        release(src);
    }

    static FFPacket *allocateNull()
    {
        FFPacket *pkt = static_cast<FFPacket *>(av_mallocz(sizeof(FFPacket)));
        pkt->type = NULLP;
        pkt->serial = 0; // 实际 serial 由外部设置更合适
        return pkt;
    }

    static void release(FFPacket *&pkt)
    {
        if (pkt) {
            av_packet_unref(&pkt->packet);
            av_freep(&pkt);
            pkt = nullptr;
        }
    }

    static bool isNull(const FFPacket *pkt) { return pkt && pkt->type == NULLP; }
};

struct AVFrameTraits
{
    static AVFrame *allocateFromSrc(AVFrame *src)
    {
        if (!src)
            return nullptr;
        AVFrame *dest = av_frame_alloc();
        if (!dest)
            return nullptr;
        av_frame_move_ref(dest, src);
        av_frame_unref(src);
        return dest;
    }

    static void releaseSrc(AVFrame *src)
    {
        if (src)
            av_frame_unref(src);
    }

    static AVFrame *allocateNull()
    {
        AVFrame *f = av_frame_alloc();
        if (!f)
            return nullptr;
        f->data[0] = nullptr;
        f->data[1] = nullptr;
        f->data[2] = nullptr;
        return f;
    }

    static void release(AVFrame *&frame)
    {
        if (frame) {
            av_frame_unref(frame);
            av_frame_free(&frame);
        }
    }

    static bool isNull(const AVFrame *frame)
    {
        if (!frame)
            return false;
        return frame->data[0] == nullptr && frame->data[1] == nullptr && frame->data[2] == nullptr;
    }
};

template<typename T, typename Traits>

class FFBoundedQueue
{
public:
    explicit FFBoundedQueue(size_t maxSize = 30);

    void start();
    void wakeAllThread();
    void close(MediaQueueCloseReason reason = MediaQueueCloseReason::RequestedStop,
               int errorCode = 0);
    MediaQueueResult enqueueFromSrc(T *src, const std::atomic<bool> *cancelled = nullptr,
                                    uint64_t *blockedNs = nullptr);
    MediaQueueDiagnostics diagnostics() const {
        std::lock_guard<std::mutex> lock(mutex); return diagnostics_;
    }
    bool tryEnqueueFromSrc(T *src);
    MediaQueueResult sendFinish(const std::atomic<bool> *cancelled = nullptr);
    void receiveFinish();
    void enqueueNull();
    MediaQueueReadResult<T> receive();
    T *dequeue();
    MediaQueueReadResult<T> tryReceive();
    T *tryDequeue();
    bool peekEmpty() const;
    T *peekQueue() const;
    T *peekBack() const;
    size_t length() const;
    static bool isNull(const T *item);
    bool isFinished() const;
    bool isClosed() const;
    MediaQueueCloseReason closeReason() const;
    int errorCode() const;
    void clearQueue();
    void flushQueue();

private:
    void clearQueueLocked();

private:
    mutable std::mutex mutex;
    std::condition_variable cond;
    std::queue<T *> q;
    size_t m_maxSize;
    MediaQueueDiagnostics diagnostics_;
    uint64_t diagnosticsGeneration_ = 0;
    bool m_upstreamFinished = false;
    bool m_closed = false;
    MediaQueueCloseReason m_closeReason = MediaQueueCloseReason::None;
    int m_errorCode = 0;
};

template<typename T, typename Traits>
FFBoundedQueue<T, Traits>::FFBoundedQueue(size_t maxSize)
    : m_maxSize(maxSize)
{}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::start()
{
    std::lock_guard<std::mutex> lock(mutex);
    m_upstreamFinished = false;
    m_closed = false;
    diagnostics_ = {};
    ++diagnosticsGeneration_;
    m_closeReason = MediaQueueCloseReason::None;
    m_errorCode = 0;
    cond.notify_all();
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::wakeAllThread()
{
    close(MediaQueueCloseReason::RequestedStop, 0);
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::close(MediaQueueCloseReason reason, int errorCode)
{
    std::lock_guard<std::mutex> lock(mutex);
    m_closed = true;
    m_closeReason = reason;
    m_errorCode = errorCode;
    clearQueueLocked();
    cond.notify_all();
}

template<typename T, typename Traits>
MediaQueueResult FFBoundedQueue<T, Traits>::enqueueFromSrc(T *src, const std::atomic<bool> *cancelled, uint64_t *blockedNs)
{
    if constexpr (std::is_same_v<T, AVFrame>) {
        qCDebug(subrec::logging::frameLog) << "[FFBoundedQueue<AVFrame>] enqueue before lock";
    }
    std::unique_lock<std::mutex> lock(mutex);
    if constexpr (std::is_same_v<T, AVFrame>) {
        qCDebug(subrec::logging::frameLog) << "[FFBoundedQueue<AVFrame>] enqueue after lock";
    }

    const auto generation = diagnosticsGeneration_;
    if (blockedNs) *blockedNs = 0;
    const bool blocked = q.size() >= m_maxSize && !m_closed && !m_upstreamFinished &&
                         !(cancelled && cancelled->load());
    const auto begin = blocked ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    // A cancelled producer stays cancelled even if a subsequent session calls start().
    cond.wait(lock, [this, cancelled]() {
        return q.size() < m_maxSize || m_closed || m_upstreamFinished || (cancelled && cancelled->load());
    });
    if (blocked) {
        const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-begin).count());
        // A cancelled old producer may wake after start() resets this queue.
        // Keep its per-call wait for its sink, without polluting the new session.
        if (generation == diagnosticsGeneration_) {
            ++diagnostics_.blockedEnqueues;
            diagnostics_.blockedNs += ns;
            diagnostics_.maxBlockedNs = std::max(diagnostics_.maxBlockedNs, ns);
        }
        if (blockedNs) *blockedNs = ns;
    }
    if (cancelled && cancelled->load()) {
        if (generation == diagnosticsGeneration_) ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return MediaQueueResult::Closed;
    }

    if (m_closed) {
        if (generation == diagnosticsGeneration_) ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return mediaQueueResultFromCloseReason(m_closeReason);
    }

    if (m_upstreamFinished) {
        if (generation == diagnosticsGeneration_) ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return MediaQueueResult::Closed;
    }
    T *dest = Traits::allocateFromSrc(src); // 由 Traits 执行 move/ref
    if (!dest) {
        if (generation == diagnosticsGeneration_) ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return MediaQueueResult::Error;
    }
    q.push(dest);
    if constexpr (std::is_same_v<T, AVFrame>) {
        qCDebug(subrec::logging::frameLog) << "[FFBoundedQueue<AVFrame>] enqueue after push";
    }
    cond.notify_one();
    return MediaQueueResult::Ok;
}

template<typename T, typename Traits>
bool FFBoundedQueue<T, Traits>::tryEnqueueFromSrc(T *src)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (m_closed || m_upstreamFinished) {
        ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return false;
    }

    // 检查队列是否已满
    if (q.size() >= m_maxSize) {
        ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return false; // 队列已满，不阻塞直接返回
    }

    T *dest = Traits::allocateFromSrc(src);
    if (!dest) {
        ++diagnostics_.rejected;
        Traits::releaseSrc(src);
        return false;
    }

    q.push(dest);
    cond.notify_one();
    return true;
}

template<typename T, typename Traits>
MediaQueueResult FFBoundedQueue<T, Traits>::sendFinish(const std::atomic<bool> *cancelled)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (cancelled && cancelled->load()) return MediaQueueResult::Closed;
    if (m_closed) {
        return mediaQueueResultFromCloseReason(m_closeReason);
    }
    if (!m_upstreamFinished) {
        m_upstreamFinished = true;
        cond.notify_all();
    }
    return MediaQueueResult::Eof;
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::receiveFinish()
{
    close(MediaQueueCloseReason::DownstreamFinished, 0);
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::enqueueNull()
{
    sendFinish();
}

template<typename T, typename Traits>
MediaQueueReadResult<T> FFBoundedQueue<T, Traits>::receive()
{
    std::unique_lock<std::mutex> lock(mutex);
    cond.wait(lock, [this]() { return !q.empty() || m_upstreamFinished || m_closed; });

    if (!q.empty()) {
        T *item = q.front();
        q.pop();
        cond.notify_all();
        return MediaQueueReadResult<T>{MediaQueueResult::Ok, item, MediaQueueCloseReason::None, 0};
    }

    if (m_closed) {
        return MediaQueueReadResult<T>{
            mediaQueueResultFromCloseReason(m_closeReason), nullptr, m_closeReason, m_errorCode};
    }

    return MediaQueueReadResult<T>{MediaQueueResult::Eof, nullptr, MediaQueueCloseReason::None, 0};
}

template<typename T, typename Traits>
T *FFBoundedQueue<T, Traits>::dequeue()
{
    return receive().item;
}

template<typename T, typename Traits>
MediaQueueReadResult<T> FFBoundedQueue<T, Traits>::tryReceive()
{
    std::unique_lock<std::mutex> lock(mutex);
    if (!q.empty()) {
        T *item = q.front();
        q.pop();
        cond.notify_all();
        return MediaQueueReadResult<T>{MediaQueueResult::Ok, item, MediaQueueCloseReason::None, 0};
    }

    if (m_closed) {
        return MediaQueueReadResult<T>{
            mediaQueueResultFromCloseReason(m_closeReason), nullptr, m_closeReason, m_errorCode};
    }

    if (m_upstreamFinished) {
        return MediaQueueReadResult<T>{MediaQueueResult::Eof, nullptr, MediaQueueCloseReason::None, 0};
    }

    return MediaQueueReadResult<T>{MediaQueueResult::Again, nullptr, MediaQueueCloseReason::None, 0};
}

template<typename T, typename Traits>
inline T *FFBoundedQueue<T, Traits>::tryDequeue()
{
    return tryReceive().item;
}

template<typename T, typename Traits>
bool FFBoundedQueue<T, Traits>::peekEmpty() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return q.empty();
}

template<typename T, typename Traits>
T *FFBoundedQueue<T, Traits>::peekQueue() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return q.empty() ? nullptr : q.front();
}

template<typename T, typename Traits>
T *FFBoundedQueue<T, Traits>::peekBack() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return q.empty() ? nullptr : q.back();
}

template<typename T, typename Traits>
size_t FFBoundedQueue<T, Traits>::length() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return q.size();
}

template<typename T, typename Traits>
bool FFBoundedQueue<T, Traits>::isNull(const T *item)
{
    return Traits::isNull(item);
}

template<typename T, typename Traits>
bool FFBoundedQueue<T, Traits>::isFinished() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return m_upstreamFinished;
}

template<typename T, typename Traits>
bool FFBoundedQueue<T, Traits>::isClosed() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return m_closed;
}

template<typename T, typename Traits>
MediaQueueCloseReason FFBoundedQueue<T, Traits>::closeReason() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return m_closeReason;
}

template<typename T, typename Traits>
int FFBoundedQueue<T, Traits>::errorCode() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return m_errorCode;
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::clearQueueLocked()
{
    while (!q.empty()) {
        T *item = q.front();
        q.pop();
        ++diagnostics_.discarded;
        Traits::release(item);
    }
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::clearQueue()
{
    std::lock_guard<std::mutex> lock(mutex);
    clearQueueLocked();
    cond.notify_all();
}

template<typename T, typename Traits>
void FFBoundedQueue<T, Traits>::flushQueue()
{
    std::lock_guard<std::mutex> lock(mutex);
    clearQueueLocked();
    cond.notify_all();
}

#endif
