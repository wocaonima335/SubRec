#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
#include <libavcodec/packet.h>
}

// Session-wide default; capture, filters and encoder must use this same rational.
// Encoder time_base = av_inv_q(fps), video PTS = integer tick (never us -> PTS).
inline constexpr AVRational kRecordFrameRate{30, 1};

// Declaration-only session service; implementation belongs in media_clock.cpp.
// All operations are internally synchronized, including concurrent readers.
// The session owns this noncopyable object and outlives its workers. start() is
// a new-session reset and must run before workers; pause/resume are serialized
// by the control thread. Neither call changes the av_gettime_relative domain.
class MediaClock {
public:
    MediaClock();
    ~MediaClock();
    MediaClock(const MediaClock &) = delete;
    MediaClock &operator=(const MediaClock &) = delete;

    void start(int64_t t0Us);
    // Duplicate pause/resume calls are no-ops. Transition times must be >= t0Us
    // and nondecreasing; invalid transitions are ignored. resume adds the
    // completed interval to pausedTotalUs; time is frozen while paused.
    void pause(int64_t tUs);
    void resume(int64_t tUs);

    // AV_NOPTS_VALUE before start or for AV_NOPTS_VALUE input; zero for t <= t0.
    // Otherwise t - t0 - duration of paused intervals intersecting [t0,t].
    // Historical capture stamps remain valid across pause/resume: implementation
    // must retain pause intervals, not subtract today's total from an old stamp.
    int64_t mediaUs(int64_t tUs) const;
    // Samples av_gettime_relative() inside the synchronized clock snapshot.
    // Downstream timestamping uses this service, not independent wall clocks.
    int64_t nowMediaUs() const;
    // Completed pause intervals only; an open pause is accounted in mediaUs().
    int64_t pausedTotalUs() const;
    bool isPaused() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace media_clock_detail {
// Private in-process format, not a file/wire ABI. memcpy avoids alignment and
// aliasing assumptions about AVBufferRef::data. All reserved fields are checked.
struct CaptureStamp {
    uint64_t tag;
    uint32_t version;
    uint32_t reserved;
    int64_t captureUs;
};
inline constexpr uint64_t captureStampTag = UINT64_C(0x5355425245435553);
static_assert(std::is_trivial<CaptureStamp>::value &&
              std::is_standard_layout<CaptureStamp>::value, "capture stamp must be POD");
}

// Transport contract for P1-Z:
// - Stamp immediately after a successful grab, before queueing/filtering, with
//   av_gettime_relative() microseconds in the SAME absolute domain as start(t0).
//   This is NOT ddagrab PTS, elapsed media time, milliseconds, or encoder PTS.
// - opaque_ref is reserved for this tagged buffer on recording frames. An
//   unrelated opaque_ref is rejected, never silently overwritten. opaque is unused.
// - Treat the buffer as immutable. Restamping allocates a fresh reference so
//   existing clones keep their original capture time. av_frame_unref frees it.
// - FFmpeg's av_frame_ref/clone/move_ref and av_frame_copy_props preserve it.
//   The contract test verifies ref/clone/copy_props on software frames. Hardware
//   frames use the same AVFrame property mechanism; D3D11/filter-graph end-to-end
//   propagation still requires Windows integration verification. A new frame
//   produced by a transform/GPU surface copy MUST copy source properties (check
//   av_frame_copy_props result), or explicitly transfer this stamp with helpers.
//   av_hwframe_transfer_data or copying pixels alone is NOT sufficient evidence.
// - Preserve the original capture stamp through the recording graph; fail/report
//   a missing stamp at the consumer, never substitute a later wall-clock time.
//   Hold frames keep the source stamp; scheduler tick supplies their new PTS.
// - Packet-based video inputs use setPacketCaptureUs() below. With
//   AV_CODEC_FLAG_COPY_OPAQUE enabled, libavcodec transfers that packet stamp
//   to the decoded frame.
// - Mutating a frame needs exclusive ownership of the AVFrame shell. Separate
//   referenced shells may be read concurrently; helpers do not synchronize them.
// Returns false for absent/foreign/malformed metadata or a null output pointer;
// *out is unchanged on failure. Capture zero is valid; AV_NOPTS_VALUE is not.
inline bool frameCaptureUs(const AVFrame *frame, int64_t *out)
{
    using namespace media_clock_detail;
    if (!frame || !out || !frame->opaque_ref || !frame->opaque_ref->data ||
        frame->opaque_ref->size != sizeof(CaptureStamp)) return false;
    CaptureStamp stamp{};
    std::memcpy(&stamp, frame->opaque_ref->data, sizeof(stamp));
    if (stamp.tag != captureStampTag || stamp.version != 1 || stamp.reserved != 0 ||
        stamp.captureUs == AV_NOPTS_VALUE) return false;
    *out = stamp.captureUs;
    return true;
}

// 0 on success, AVERROR(EINVAL) for invalid arguments, AVERROR(EEXIST) for
// foreign/malformed opaque_ref, AVERROR(ENOMEM) on allocation failure.
// Every failure leaves the frame and any previously stored stamp unchanged.
inline int setFrameCaptureUs(AVFrame *frame, int64_t captureUs)
{
    using namespace media_clock_detail;
    if (!frame || captureUs == AV_NOPTS_VALUE) return AVERROR(EINVAL);
    int64_t previous;
    if (frame->opaque_ref && !frameCaptureUs(frame, &previous)) return AVERROR(EEXIST);
    AVBufferRef *buffer = av_buffer_alloc(sizeof(CaptureStamp));
    if (!buffer) return AVERROR(ENOMEM);
    const CaptureStamp stamp{captureStampTag, 1, 0, captureUs};
    std::memcpy(buffer->data, &stamp, sizeof(stamp));
    av_buffer_unref(&frame->opaque_ref);
    frame->opaque_ref = buffer;
    return 0;
}

// Packet counterpart for packet-based inputs such as DirectShow. It uses the
// same tagged representation so AV_CODEC_FLAG_COPY_OPAQUE can carry the value
// into decoded AVFrames. Failures leave the packet unchanged.
inline int setPacketCaptureUs(AVPacket *packet, int64_t captureUs)
{
    using namespace media_clock_detail;
    if (!packet || captureUs == AV_NOPTS_VALUE) return AVERROR(EINVAL);
    if (packet->opaque_ref) return AVERROR(EEXIST);
    AVBufferRef *buffer = av_buffer_alloc(sizeof(CaptureStamp));
    if (!buffer) return AVERROR(ENOMEM);
    const CaptureStamp stamp{captureStampTag, 1, 0, captureUs};
    std::memcpy(buffer->data, &stamp, sizeof(stamp));
    packet->opaque_ref = buffer;
    return 0;
}
