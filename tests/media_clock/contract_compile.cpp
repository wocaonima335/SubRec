#include "queue/media_clock.h"
#include "queue/video_tick_scheduler.h"
#include <type_traits>
#include <utility>

static_assert(kRecordFrameRate.num == 30 && kRecordFrameRate.den == 1);
static_assert(std::is_same<decltype(std::declval<const MediaClock &>().mediaUs(int64_t{})), int64_t>::value);
static_assert(std::is_same<decltype(std::declval<const MediaClock &>().nowMediaUs()), int64_t>::value);
static_assert(std::is_same<decltype(std::declval<MediaClock &>().start(int64_t{})), void>::value);
static_assert(std::is_same<decltype(std::declval<MediaClock &>().pause(int64_t{})), void>::value);
static_assert(std::is_same<decltype(std::declval<MediaClock &>().resume(int64_t{})), void>::value);
static_assert(std::is_same<decltype(std::declval<VideoTickScheduler &>().configure(kRecordFrameRate)), bool>::value);
static_assert(std::is_same<decltype(std::declval<VideoTickScheduler &>().reset()), void>::value);
static_assert(std::is_same<decltype(std::declval<VideoTickScheduler &>().beginSegment(int64_t{})), bool>::value);
static_assert(std::is_same<decltype(std::declval<VideoTickScheduler &>().decide(int64_t{})), TickDecision>::value);
static_assert(std::is_same<VideoTickScheduler::HoldFrameCallback, std::function<bool(int64_t, int64_t)>>::value);
static_assert(std::is_same<decltype(std::declval<VideoTickScheduler &>().requestHoldover(int64_t{})), int64_t>::value);
static_assert(std::is_same<decltype(std::declval<const VideoTickScheduler &>().counters()), VideoTickCounters>::value);
static_assert(TickDecision{TickDecision::Kind::Lagged, 3, 2}.accepted());
static_assert(!TickDecision{TickDecision::Kind::SlotCollision, 3, 0}.accepted());

struct FrameDelete { void operator()(AVFrame *frame) const { av_frame_free(&frame); } };
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

// No instantiation/linkage of the future .cpp services. Exercise only the inline
// transport, using real FFmpeg: this is not a D3D11/filter-graph acceptance test.
int main()
{
    AVPacket packet{};
    CHECK(setPacketCaptureUs(&packet, 456) == 0);
    CHECK(setPacketCaptureUs(&packet, 789) == AVERROR(EEXIST));
    av_buffer_unref(&packet.opaque_ref);
    Frame source(av_frame_alloc()), referenced(av_frame_alloc()), props(av_frame_alloc());
    CHECK(source && referenced && props);
    int64_t stamp = 123;
    CHECK(!frameCaptureUs(source.get(), &stamp) && stamp == 123);
    CHECK(setFrameCaptureUs(nullptr, 0) == AVERROR(EINVAL));
    CHECK(setFrameCaptureUs(source.get(), AV_NOPTS_VALUE) == AVERROR(EINVAL));
    source->format = AV_PIX_FMT_YUV420P; source->width = source->height = 16;
    CHECK(av_frame_get_buffer(source.get(), 0) == 0);
    CHECK(setFrameCaptureUs(source.get(), 123456789) == 0);
    CHECK(av_frame_ref(referenced.get(), source.get()) == 0);
    Frame cloned(av_frame_clone(source.get())); CHECK(cloned);
    CHECK(av_frame_copy_props(props.get(), source.get()) == 0);
    CHECK(setFrameCaptureUs(source.get(), 0) == 0); // Must not alter shared stamps.
    CHECK(frameCaptureUs(source.get(), &stamp) && stamp == 0);
    CHECK(frameCaptureUs(referenced.get(), &stamp) && stamp == 123456789);
    CHECK(frameCaptureUs(cloned.get(), &stamp) && stamp == 123456789);
    CHECK(frameCaptureUs(props.get(), &stamp) && stamp == 123456789);
    av_frame_unref(source.get());
    CHECK(frameCaptureUs(cloned.get(), &stamp) && stamp == 123456789);
    CHECK(!frameCaptureUs(cloned.get(), nullptr));
    CHECK(setFrameCaptureUs(cloned.get(), AV_NOPTS_VALUE) == AVERROR(EINVAL));
    CHECK(frameCaptureUs(cloned.get(), &stamp) && stamp == 123456789);
    source->opaque_ref = av_buffer_allocz(sizeof(media_clock_detail::CaptureStamp));
    CHECK(source->opaque_ref); // Same size, wrong tag: must not replace foreign data.
    auto *foreign = source->opaque_ref;
    CHECK(setFrameCaptureUs(source.get(), 42) == AVERROR(EEXIST));
    CHECK(source->opaque_ref == foreign);
    CHECK(!frameCaptureUs(source.get(), &stamp) && stamp == 123456789);
}
