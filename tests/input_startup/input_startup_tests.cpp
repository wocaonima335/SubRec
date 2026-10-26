#include "demuxer/demuxer.h"
#include "thread/ffdemuxerthread.h"
#include "recorder/ffrecorder_p.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>

using namespace FFRecordContextType;
static void check(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static void failed(Demuxer &demux, const InputRequest &input,
                   CaptureErrorStage stage, int code)
{
    const auto result = demux.openResult(input);
    check(!result.ok && result.error.has_value(), "structured failure");
    check(result.error->stage == stage, "failure stage");
    check(result.error->ffmpegError == code, "FFmpeg code");
    check(!result.error->ffmpegMessage.isEmpty(), "FFmpeg message");
    check(result.error->role == input.kind && result.error->backend == input.backend,
          "role/backend preserved");
    check(result.error->deviceId == input.deviceId
          && result.error->deviceName == input.displayName, "device preserved");
    check(!demux.isOpen() && !demux.getAStream() && !demux.getVStream(), "failed source cleared");
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    InputRequest audio;
    audio.kind = CaptureDeviceKind::Microphone;
    audio.deviceId = QStringLiteral("synthetic-audio");
    audio.displayName = QStringLiteral("Synthetic microphone");
    InputRequest video;
    video.kind = CaptureDeviceKind::Camera;
    video.deviceId = QStringLiteral("synthetic-video");
    video.displayName = QStringLiteral("Synthetic camera");
    Demuxer demux;
    demux.init("unused", "missing-r02-format", nullptr, nullptr, MICROPHONE);
    failed(demux, audio, CaptureErrorStage::InputFormat, AVERROR_DEMUXER_NOT_FOUND);
    demux.init("r02-input-that-does-not-exist.wav", "wav", nullptr, nullptr, MICROPHONE);
    failed(demux, audio, CaptureErrorStage::InputOpen, AVERROR(ENOENT));
    demux.init("r02-input-that-does-not-exist.mkv", "matroska", nullptr, nullptr, CAMERA);
    failed(demux, video, CaptureErrorStage::InputOpen, AVERROR(ENOENT));
    demux.init("unused", "missing-r02-format", nullptr, nullptr, CAMERA, true, [] { return false; });
    failed(demux, video, CaptureErrorStage::Configuration, AVERROR_EXIT);
    // lavfi generators only: no device open, demux loop, recording or output file.
    demux.init("sine=frequency=440:duration=0.05", "lavfi", nullptr, nullptr, CAMERA);
    failed(demux, video, CaptureErrorStage::StreamInfo, AVERROR_STREAM_NOT_FOUND);
    demux.init("color=c=black:s=16x16:d=0.05", "lavfi", nullptr, nullptr, MICROPHONE);
    failed(demux, audio, CaptureErrorStage::StreamInfo, AVERROR_STREAM_NOT_FOUND);
    demux.init("sine=frequency=440:duration=0.05", "lavfi", nullptr, nullptr, MICROPHONE);
    check(demux.openResult(audio).ok && demux.getAStream(), "synthetic audio succeeds");
    demux.close();
    check(!demux.getAStream() && !demux.getVStream(), "close clears streams");
    int calls = 0;
    demux.init("sine=duration=0.05", "lavfi", nullptr, nullptr, MICROPHONE, true,
               [&] { return ++calls < 3; });
    failed(demux, audio, CaptureErrorStage::Configuration, AVERROR_EXIT);
    demux.init("unused", "missing-r02-format", nullptr, nullptr, SCREEN, false);
    FFDemuxerThread owner;
    owner.init(&demux);
    check(!owner.prepareOpenAndWait(), "owner-thread failure propagates");
    owner.requestAbort(); owner.wait();
    failed(demux, video, CaptureErrorStage::InputFormat, AVERROR_DEMUXER_NOT_FOUND);
    owner.close();
    demux.init("color=c=black:s=16x16:d=0.05", "lavfi", nullptr, nullptr, SCREEN, false);
    owner.init(&demux);
    check(owner.prepareOpenAndWait(), "prepared synthetic video succeeds");
    check(demux.openResult(video).ok && demux.getVStream(), "prepared video has stream");
    owner.requestAbort(); owner.wait(); owner.close();
    check(!demux.isOpen() && !demux.getVStream(), "cancelled owner cleaned up");
    std::cout << "input_startup: all checks passed\n";
}
