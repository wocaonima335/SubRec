#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace capture::media {

class FfmpegError : public std::runtime_error {
public:
    FfmpegError(const char* operation, int code);
    int code() const noexcept { return code_; }
private:
    int code_;
};

struct FrameDeleter { void operator()(AVFrame* p) const noexcept; };
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
FramePtr makeFrame();

// Positive time bases only; nearest rounding, ties away from zero.
// Preserves AV_NOPTS_VALUE; throws on overflow rather than manufacturing a PTS.
int64_t rescaleTimestamp(int64_t pts, AVRational from, AVRational to);

class AudioFormat {
public:
    AudioFormat(int rate, AVSampleFormat format, const AVChannelLayout& layout,
                AVRational timeBase);
    AudioFormat(const AudioFormat& other);
    AudioFormat& operator=(const AudioFormat& other);
    AudioFormat(AudioFormat&&) noexcept = default;
    AudioFormat& operator=(AudioFormat&&) noexcept = default;
    int sampleRate() const noexcept { return rate_; }
    AVSampleFormat sampleFormat() const noexcept { return format_; }
    AVRational timeBase() const noexcept { return timeBase_; }
    const AVChannelLayout& channelLayout() const;
private:
    struct LayoutDeleter { void operator()(AVChannelLayout* p) const noexcept; };
    using LayoutPtr = std::unique_ptr<AVChannelLayout, LayoutDeleter>;
    static LayoutPtr copyLayout(const AVChannelLayout& layout);
    int rate_;
    AVSampleFormat format_;
    AVRational timeBase_;
    LayoutPtr layout_;
};

// An owning value description (no external storage to release for video).
// This subset describes CPU BGRA and even-sized YUV420P only.
class VideoFormat {
public:
    VideoFormat(int width, int height, AVPixelFormat format, AVRational timeBase);
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    AVPixelFormat pixelFormat() const noexcept { return format_; }
    AVRational timeBase() const noexcept { return timeBase_; }
private:
    int width_, height_;
    AVPixelFormat format_;
    AVRational timeBase_;
};

class VideoConverter {
public:
    VideoConverter(VideoFormat input, VideoFormat output);
    VideoConverter(const VideoConverter&) = delete;
    VideoConverter& operator=(const VideoConverter&) = delete;
    // Borrowed input, independently owned output. Positive/negative stride.
    FramePtr convert(const AVFrame& input);
    const VideoFormat& outputFormat() const noexcept { return output_; }
private:
    struct Deleter { void operator()(SwsContext* p) const noexcept; };
    VideoFormat input_, output_;
    std::unique_ptr<SwsContext, Deleter> context_;
    bool failed_ = false;
};

class AudioConverter {
public:
    explicit AudioConverter(const AudioFormat& input);
    AudioConverter(const AudioConverter&) = delete;
    AudioConverter& operator=(const AudioConverter&) = delete;
    // Nonempty contiguous PCM only. nullptr means buffered, NOT end-of-stream.
    // Described timeBase is authoritative; AVFrame.time_base is not consulted.
    FramePtr convert(const AVFrame& input);
    // Terminal flush: repeat until nullptr. Idempotent after exhaustion.
    // Once flushing starts, convert() throws; create a new converter to restart.
    FramePtr drain();
    FramePtr flush() { return drain(); }
    const AudioFormat& outputFormat() const noexcept { return output_; }
private:
    struct Deleter { void operator()(SwrContext* p) const noexcept; };
    enum class State { Accepting, Draining, Drained, Failed };
    FramePtr allocateOutput(int capacity) const;
    FramePtr finish(FramePtr output, int count);
    AudioFormat input_, output_;
    std::unique_ptr<SwrContext, Deleter> context_;
    State state_ = State::Accepting;
    bool started_ = false;
    int64_t firstInputPts_ = AV_NOPTS_VALUE;
    int64_t outputAnchor_ = AV_NOPTS_VALUE;
    int64_t inputSamples_ = 0;
    int64_t outputSamples_ = 0;
};

} // namespace capture::media
