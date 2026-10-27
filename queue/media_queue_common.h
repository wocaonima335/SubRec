#ifndef MEDIA_QUEUE_COMMON_H
#define MEDIA_QUEUE_COMMON_H

#include <cstdint>

// Snapshot since start(). Nanoseconds retain sub-ms waits; counts are items,
// not audio samples. Only a full queue's CV wait counts as producer backpressure.
struct MediaQueueDiagnostics {
    uint64_t blockedEnqueues = 0, blockedNs = 0, maxBlockedNs = 0;
    uint64_t rejected = 0, discarded = 0;
};

enum class MediaQueueResult
{
    Ok,
    Eof,
    Again,
    Closed,
    DownstreamFinished,
    Error,
};

enum class MediaQueueCloseReason
{
    None,
    RequestedStop,
    Error,
    DownstreamFinished,
};

template<typename T>
struct MediaQueueReadResult
{
    MediaQueueResult result = MediaQueueResult::Again;
    T *item = nullptr;
    MediaQueueCloseReason closeReason = MediaQueueCloseReason::None;
    int errorCode = 0;

    bool ok() const { return result == MediaQueueResult::Ok && item != nullptr; }
};

inline MediaQueueResult mediaQueueResultFromCloseReason(MediaQueueCloseReason reason)
{
    switch (reason) {
    case MediaQueueCloseReason::Error:
        return MediaQueueResult::Error;
    case MediaQueueCloseReason::DownstreamFinished:
        return MediaQueueResult::DownstreamFinished;
    case MediaQueueCloseReason::RequestedStop:
        return MediaQueueResult::Closed;
    case MediaQueueCloseReason::None:
    default:
        return MediaQueueResult::Closed;
    }
}

inline const char *mediaQueueResultName(MediaQueueResult result)
{
    switch (result) {
    case MediaQueueResult::Ok:
        return "Ok";
    case MediaQueueResult::Eof:
        return "Eof";
    case MediaQueueResult::Again:
        return "Again";
    case MediaQueueResult::Closed:
        return "Closed";
    case MediaQueueResult::DownstreamFinished:
        return "DownstreamFinished";
    case MediaQueueResult::Error:
        return "Error";
    }

    return "Unknown";
}

inline const char *mediaQueueCloseReasonName(MediaQueueCloseReason reason)
{
    switch (reason) {
    case MediaQueueCloseReason::None:
        return "None";
    case MediaQueueCloseReason::RequestedStop:
        return "RequestedStop";
    case MediaQueueCloseReason::Error:
        return "Error";
    case MediaQueueCloseReason::DownstreamFinished:
        return "DownstreamFinished";
    }

    return "Unknown";
}

#endif // MEDIA_QUEUE_COMMON_H
