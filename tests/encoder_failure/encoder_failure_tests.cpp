#include "encoder/ffaencoder.h"
#include "encoder/ffvencoder.h"
#include "filter/ffvfilter.h"
#include "muxer/ffmuxer.h"
#include "queue/ffaframequeue.h"
#include "queue/ffvframequeue.h"
#include "queue/recordersyncqueue.h"
#include "recorder/ffrecorder.h"
#include "thread/ffaencoderthread.h"
#include "thread/ffvencoderthread.h"

#include <QCoreApplication>
#include <QEvent>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

namespace {
// The real class declarations are used so fake implementations must keep the
// production signatures. Access is single-worker, read only after joining.
struct CodecCalls {
    int result = 0;
    int encoded = 0;
    int finished = 0;
    int aborted = 0;
} calls;
constexpr int injectedError = AVERROR(EIO);
}

FFVEncoder::FFVEncoder() : codecCtx(new AVCodecContext{}) {}
FFVEncoder::~FFVEncoder() { delete codecCtx; }
bool FFVEncoder::initVideo(AVFrame *, AVRational) {
    codecCtx->time_base = {1, 30};
    return true;
}
AVCodecContext *FFVEncoder::getCodecCtx() { return codecCtx; }
VideoEncoderSelection FFVEncoder::videoEncoderSelection() const {
    VideoEncoderSelection selection;
    selection.success = true;
    return selection;
}
int FFVEncoder::encode(AVFrame *, int, int64_t, AVRational) { ++calls.encoded; return calls.result; }
int FFVEncoder::finish(int) { ++calls.finished; return 0; }
void FFVEncoder::requestAbort() { ++calls.aborted; }
void FFVEncoder::requestDrainStop() {}
void FFVEncoder::close() {}

FFAEncoder::FFAEncoder() : codecCtx(new AVCodecContext{}) {}
FFAEncoder::~FFAEncoder() {
    av_channel_layout_uninit(&codecCtx->ch_layout);
    delete codecCtx;
}
void FFAEncoder::initAudio(AVFrame *frame) {
    codecCtx->time_base = {1, frame->sample_rate};
    codecCtx->sample_rate = frame->sample_rate;
    codecCtx->sample_fmt = static_cast<AVSampleFormat>(frame->format);
    codecCtx->frame_size = 1024;
    CHECK(av_channel_layout_copy(&codecCtx->ch_layout, &frame->ch_layout) == 0);
}
AVCodecContext *FFAEncoder::getCodecCtx() { return codecCtx; }
int FFAEncoder::encode(AVFrame *, int, int64_t, AVRational) { ++calls.encoded; return calls.result; }
int FFAEncoder::finish(int) { ++calls.finished; return 0; }
void FFAEncoder::requestAbort() { ++calls.aborted; }
void FFAEncoder::requestDrainStop() {}
void FFAEncoder::close() {}

FFVFilter::FFVFilter() = default;
FFVFilter::~FFVFilter() = default;
AVRational FFVFilter::getFrameRate() { return {30, 1}; }
FFMuxer::FFMuxer() = default;
FFMuxer::~FFMuxer() = default;
int FFMuxer::addStream(const AVCodecContext *) { return 0; }
int FFMuxer::getAStreamIndex() { return 0; }
int FFMuxer::getVStreamIndex() { return 0; }

namespace {
void joinWorker(FFThread &worker) {
    if (!worker.waitFor(std::chrono::seconds(2))) {
        // Avoid a blocking FFThread destructor hiding a failure to exit.
        std::cerr << "encoder worker did not exit\n";
        std::_Exit(1);
    }
}

void checkDelivery(FFRecorder &recorder, bool fail, const char *kind) {
    // No UI events have been dispatched while the worker ran. A direct call
    // would already have changed these fields (and used the wrong thread).
    CHECK(recorder.captureError.isEmpty());
    CHECK(recorder.errorCalls == 0);
    CHECK(recorder.stopRequests == 0);
    QCoreApplication::sendPostedEvents(&recorder, QEvent::MetaCall);
    CHECK(recorder.deliveredOnRecorderThread);
    CHECK(calls.aborted == 0);
    CHECK(calls.finished == 1);
    if (fail) {
        CHECK(calls.encoded == 1); // The second queued frame must not be encoded.
        CHECK(!recorder.captureError.isEmpty());
        CHECK(recorder.captureError.contains(QString::fromLatin1(kind)));
        CHECK(recorder.captureError.contains(QString::number(injectedError)));
        CHECK(recorder.errorCalls == 1);
        CHECK(recorder.stopRequests == 1);
        CHECK(recorder.errorBeforeStop);
    } else {
        CHECK(calls.encoded == 2);
        CHECK(recorder.captureError.isEmpty());
        CHECK(recorder.errorCalls == 0);
        CHECK(recorder.stopRequests == 0);
    }
}

template<class Queue>
void submitFrames(Queue &queue, AVFrame *frame) {
    CHECK(av_frame_get_buffer(frame, 0) == 0);
    // Initialized synthetic data also exercises the real audio sync queue.
    if (frame->nb_samples > 0)
        CHECK(av_samples_set_silence(frame->extended_data, 0, frame->nb_samples,
                                    frame->ch_layout.nb_channels, AV_SAMPLE_FMT_FLTP) == 0);
    AVFrame *second = av_frame_clone(frame);
    CHECK(second);
    // The bounded queues move frame references on enqueue.
    queue.enqueue(frame);
    queue.enqueue(second);
    av_frame_free(&frame);
    av_frame_free(&second);
    CHECK(queue.sendFinish() == MediaQueueResult::Eof);
}

template<class Queue>
void checkQueue(const Queue &queue, bool fail) {
    if (fail) {
        CHECK(queue.isClosed());
        CHECK(queue.closeReason() == MediaQueueCloseReason::Error);
        CHECK(queue.errorCode() == injectedError);
    } else {
        CHECK(!queue.isClosed());
    }
}

void videoCase(bool fail) {
    calls = {};
    calls.result = fail ? injectedError : 0;
    FFRecorder recorder;
    FFVEncoder encoder;
    FFVFilter filter;
    FFMuxer muxer;
    FFVFrameQueue queue;
    AVFrame *frame = av_frame_alloc();
    CHECK(frame);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = frame->height = 16;
    submitFrames(queue, frame);
    FFVEncoderThread worker;
    worker.init(&filter, &encoder, &muxer, &queue, nullptr, &recorder);
    worker.start();
    joinWorker(worker);
    checkQueue(queue, fail);
    checkDelivery(recorder, fail, "Video");
}

void audioCase(bool fail, bool useSync) {
    calls = {};
    calls.result = fail ? injectedError : 0;
    FFRecorder recorder;
    FFAEncoder encoder;
    FFMuxer muxer;
    FFAFrameQueue queue;
    RecorderSyncQueue sync;
    AVFrame *frame = av_frame_alloc();
    CHECK(frame);
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = 48000;
    frame->nb_samples = 1024;
    av_channel_layout_default(&frame->ch_layout, 2);
    submitFrames(queue, frame);
    FFAEncoderThread worker;
    worker.init(nullptr, &encoder, &muxer, &queue, useSync ? &sync : nullptr, &recorder);
    worker.start();
    joinWorker(worker);
    checkQueue(queue, fail);
    checkDelivery(recorder, fail, "Audio");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        const std::string mode = argv[1];
        CHECK(mode == "video" || mode == "audio" || mode == "audio_sync");
        // Healthy controls prevent unconditional error/stop reporting from passing.
        for (bool fail : {false, true}) {
            if (mode == "video") videoCase(fail);
            else audioCase(fail, mode == "audio_sync");
        }
        std::cout << mode << ": PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
