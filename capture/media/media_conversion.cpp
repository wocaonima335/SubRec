#include "media_conversion.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <utility>
#include <vector>
extern "C" {
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

namespace capture::media {
namespace {
void check(int result, const char* operation) {
    if (result < 0) throw FfmpegError(operation, result);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
void validateTimeBase(AVRational value) {
    require(value.num > 0 && value.den > 0, "time base must have positive numerator and denominator");
}
int64_t add(int64_t a, int64_t b) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        throw std::overflow_error("timestamp/sample counter overflow");
    const auto result = a + b;
    if (result == AV_NOPTS_VALUE) throw std::overflow_error("timestamp collides with AV_NOPTS_VALUE");
    return result;
}
bool commonPcm(AVSampleFormat format) {
    switch (av_get_packed_sample_fmt(format)) {
    case AV_SAMPLE_FMT_U8: case AV_SAMPLE_FMT_S16: case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S64: case AV_SAMPLE_FMT_FLT: case AV_SAMPLE_FMT_DBL:
        return true;
    default: return false;
    }
}
AudioFormat outputAudioFormat() {
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    return AudioFormat(48000, AV_SAMPLE_FMT_FLTP, stereo, {1, 48000});
}
std::string errorText(const char* operation, int code) {
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(code, text, sizeof(text)) < 0) return std::string(operation) + ": FFmpeg error " + std::to_string(code);
    return std::string(operation) + ": " + text + " (" + std::to_string(code) + ")";
}
}

FfmpegError::FfmpegError(const char* operation, int code)
    : std::runtime_error(errorText(operation, code)), code_(code) {}
void FrameDeleter::operator()(AVFrame* p) const noexcept { av_frame_free(&p); }
FramePtr makeFrame() {
    FramePtr frame(av_frame_alloc());
    if (!frame) throw std::bad_alloc();
    return frame;
}
int64_t rescaleTimestamp(int64_t pts, AVRational from, AVRational to) {
    validateTimeBase(from);
    validateTimeBase(to);
    if (pts == AV_NOPTS_VALUE) return pts;
    const auto result = av_rescale_q_rnd(pts, from, to, AV_ROUND_NEAR_INF);
    if (result == INT64_MIN) throw std::overflow_error("time base conversion overflow");
    return result;
}

void AudioFormat::LayoutDeleter::operator()(AVChannelLayout* p) const noexcept {
    if (p) { av_channel_layout_uninit(p); delete p; }
}
AudioFormat::LayoutPtr AudioFormat::copyLayout(const AVChannelLayout& layout) {
    require(av_channel_layout_check(&layout) == 1, "invalid channel layout");
    LayoutPtr result(new AVChannelLayout{});
    check(av_channel_layout_copy(result.get(), &layout), "av_channel_layout_copy");
    return result;
}
AudioFormat::AudioFormat(int rate, AVSampleFormat format, const AVChannelLayout& layout,
                         AVRational timeBase)
    : rate_(rate), format_(format), timeBase_(timeBase) {
    require(rate > 0, "sample rate must be positive");
    require(commonPcm(format), "unsupported PCM sample format");
    validateTimeBase(timeBase);
    layout_ = copyLayout(layout);
}
AudioFormat::AudioFormat(const AudioFormat& other)
    : AudioFormat(other.rate_, other.format_, other.channelLayout(), other.timeBase_) {}
AudioFormat& AudioFormat::operator=(const AudioFormat& other) {
    if (this != &other) {
        AudioFormat copy(other);
        *this = std::move(copy);
    }
    return *this;
}
const AVChannelLayout& AudioFormat::channelLayout() const {
    if (!layout_) throw std::logic_error("AudioFormat was moved from");
    return *layout_;
}
VideoFormat::VideoFormat(int width, int height, AVPixelFormat format, AVRational timeBase)
    : width_(width), height_(height), format_(format), timeBase_(timeBase) {
    require(width > 0 && height > 0 && width <= INT32_MAX / 4,
            "invalid or overflowing video dimensions");
    require(format == AV_PIX_FMT_BGRA || format == AV_PIX_FMT_YUV420P,
            "only CPU BGRA/YUV420P are described by this subset");
    require(format != AV_PIX_FMT_YUV420P || (width % 2 == 0 && height % 2 == 0),
            "YUV420P output dimensions must be even");
    validateTimeBase(timeBase);
}
void VideoConverter::Deleter::operator()(SwsContext* p) const noexcept { sws_freeContext(p); }
VideoConverter::VideoConverter(VideoFormat input, VideoFormat output)
    : input_(input), output_(output) {
    require(input.pixelFormat() == AV_PIX_FMT_BGRA && output.pixelFormat() == AV_PIX_FMT_YUV420P,
            "VideoConverter requires BGRA -> YUV420P");
    context_.reset(sws_getContext(input.width(), input.height(), input.pixelFormat(),
                                  output.width(), output.height(), output.pixelFormat(),
                                  SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!context_) throw FfmpegError("sws_getContext (invalid configuration or allocation failure)", AVERROR(EINVAL));
    const int* coefficients = sws_getCoefficients(SWS_CS_ITU601);
    check(sws_setColorspaceDetails(context_.get(), coefficients, 1, coefficients, 0,
                                   0, 1 << 16, 1 << 16), "sws_setColorspaceDetails");
}
FramePtr VideoConverter::convert(const AVFrame& frame) {
    if (failed_) throw std::logic_error("VideoConverter failed; recreate it");
    require(!frame.hw_frames_ctx, "hardware frames are unsupported");
    require(frame.format == input_.pixelFormat() && frame.width == input_.width()
            && frame.height == input_.height(), "video input format/size changed; recreate converter");
    const int64_t stride = frame.linesize[0];
    require(frame.data[0] && (stride >= int64_t(frame.width) * 4 || stride <= -int64_t(frame.width) * 4)
            && stride != INT32_MIN, "BGRA plane is missing or stride is too small/invalid");
    require(frame.duration >= 0, "negative video duration");
    const auto pts = rescaleTimestamp(frame.pts, input_.timeBase(), output_.timeBase());
    const auto duration = rescaleTimestamp(frame.duration, input_.timeBase(), output_.timeBase());
    auto output = makeFrame();
    output->format = output_.pixelFormat();
    output->width = output_.width();
    output->height = output_.height();
    output->time_base = output_.timeBase();
    output->pts = pts;
    output->duration = duration;
    output->colorspace = AVCOL_SPC_SMPTE170M;
    output->color_range = AVCOL_RANGE_MPEG;
    check(av_frame_get_buffer(output.get(), 32), "av_frame_get_buffer(video)");
    const int rows = sws_scale(context_.get(), frame.data, frame.linesize, 0, frame.height,
                               output->data, output->linesize);
    if (rows != output->height) {
        failed_ = true;
        throw FfmpegError("sws_scale incomplete/failed", rows < 0 ? rows : AVERROR(EIO));
    }
    return output;
}

void AudioConverter::Deleter::operator()(SwrContext* p) const noexcept { swr_free(&p); }
AudioConverter::AudioConverter(const AudioFormat& input)
    : input_(input), output_(outputAudioFormat()) {
    require(input_.channelLayout().order != AV_CHANNEL_ORDER_UNSPEC,
            "resampling needs explicit channel identities, not just a channel count");
    SwrContext* raw = nullptr;
    const int result = swr_alloc_set_opts2(&raw, &output_.channelLayout(), output_.sampleFormat(),
                                          output_.sampleRate(), &input_.channelLayout(),
                                          input_.sampleFormat(), input_.sampleRate(), 0, nullptr);
    context_.reset(raw);
    check(result, "swr_alloc_set_opts2");
    check(swr_init(context_.get()), "swr_init");
}
FramePtr AudioConverter::allocateOutput(int capacity) const {
    auto output = makeFrame();
    output->format = output_.sampleFormat();
    output->sample_rate = output_.sampleRate();
    output->time_base = output_.timeBase();
    output->nb_samples = std::max(capacity, 1);
    check(av_channel_layout_copy(&output->ch_layout, &output_.channelLayout()), "av_channel_layout_copy(frame)");
    check(av_frame_get_buffer(output.get(), 32), "av_frame_get_buffer(audio)");
    return output;
}
FramePtr AudioConverter::finish(FramePtr output, int count) {
    check(count, "swr_convert");
    if (count == 0) return nullptr;
    output->nb_samples = count;
    output->duration = count;
    output->pts = outputAnchor_ == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : add(outputAnchor_, outputSamples_);
    outputSamples_ = add(outputSamples_, count);
    return output;
}
FramePtr AudioConverter::convert(const AVFrame& frame) {
    if (state_ != State::Accepting) throw std::logic_error("audio conversion after drain/failure; recreate converter");
    require(!frame.hw_frames_ctx, "hardware audio frames are unsupported");
    require(frame.sample_rate == input_.sampleRate() && frame.format == input_.sampleFormat()
            && av_channel_layout_compare(&frame.ch_layout, &input_.channelLayout()) == 0,
            "audio input format/rate/layout changed");
    require(frame.nb_samples > 0 && frame.extended_data, "empty/missing PCM input; use drain for EOS");
    const bool planar = av_sample_fmt_is_planar(input_.sampleFormat()) != 0;
    const int channels = input_.channelLayout().nb_channels;
    const int planes = planar ? channels : 1;
    const int64_t channelBytes = int64_t(frame.nb_samples) * av_get_bytes_per_sample(input_.sampleFormat());
    const int interleavedChannels = planar ? 1 : channels;
    require(channelBytes <= INT32_MAX / interleavedChannels, "PCM plane byte count overflows linesize");
    const int64_t planeBytes = channelBytes * interleavedChannels;
    require(frame.linesize[0] >= planeBytes, "PCM plane linesize is too small");
    std::vector<const uint8_t*> source(static_cast<size_t>(planes));
    for (int i = 0; i < planes; ++i) {
        require(frame.extended_data[i] != nullptr, "missing PCM plane");
        source[static_cast<size_t>(i)] = frame.extended_data[i];
    }
    if (started_) {
        require((frame.pts == AV_NOPTS_VALUE) == (firstInputPts_ == AV_NOPTS_VALUE),
                "cannot mix known and unknown audio timestamps");
        if (frame.pts != AV_NOPTS_VALUE) {
            const auto expected = add(firstInputPts_, rescaleTimestamp(inputSamples_, {1, input_.sampleRate()}, input_.timeBase()));
            // A one-tick tolerance accommodates quantized input time bases.
            require(frame.pts == expected || (expected < INT64_MAX && frame.pts == expected + 1)
                    || (expected > INT64_MIN + 1 && frame.pts == expected - 1),
                    "audio timestamp discontinuity; drain and recreate converter");
        }
    }
    const auto newInputSamples = add(inputSamples_, frame.nb_samples);
    const auto anchor = started_ ? outputAnchor_ : rescaleTimestamp(frame.pts, input_.timeBase(), output_.timeBase());
    const int capacity = swr_get_out_samples(context_.get(), frame.nb_samples);
    check(capacity, "swr_get_out_samples");
    auto output = allocateOutput(capacity);
    // Reject foreseeable counter overflow before consuming input.
    const auto maxOutputSamples = add(outputSamples_, std::max(capacity, 1));
    if (anchor != AV_NOPTS_VALUE) (void)add(anchor, maxOutputSamples);
    try {
        const int count = swr_convert(context_.get(), output->extended_data, output->nb_samples,
                                      source.data(), frame.nb_samples);
        check(count, "swr_convert");
        if (!started_) { firstInputPts_ = frame.pts; outputAnchor_ = anchor; started_ = true; }
        inputSamples_ = newInputSamples;
        return finish(std::move(output), count);
    } catch (...) { state_ = State::Failed; throw; }
}
FramePtr AudioConverter::drain() {
    if (state_ == State::Failed) throw std::logic_error("AudioConverter failed; recreate it");
    if (state_ == State::Drained) return nullptr;
    state_ = State::Draining;
    try {
        const int capacity = swr_get_out_samples(context_.get(), 0);
        check(capacity, "swr_get_out_samples(drain)");
        auto output = allocateOutput(capacity);
        const int count = swr_convert(context_.get(), output->extended_data, output->nb_samples, nullptr, 0);
        auto result = finish(std::move(output), count);
        if (!result) state_ = State::Drained;
        return result;
    } catch (...) { state_ = State::Failed; throw; }
}

} // namespace capture::media
