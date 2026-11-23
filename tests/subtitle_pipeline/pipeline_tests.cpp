#include "capture/test_hook_gate.h"
#include "encoder/ffaencoder.h"
#include "muxer/ffmuxer.h"
#include "queue/ffaframequeue.h"
#include "queue/ffapacketqueue.h"
#include "queue/recordersyncqueue.h"
#include "thread/ffaencoderthread.h"
#include "subtitle/audio/ingest_worker.h"
#include "subtitle/audio/pcm_spool.h"
#include "subtitle/host/asr_host_client.h"
#include "subtitle/live/local_agreement.h"
#include "subtitle/live/sentence_fixer.h"
#include "subtitle/live/subtitle_merger.h"
#include "subtitle/tap/audio_tap.h"

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <functional>
#include <thread>

using namespace subtitle;
using namespace std::chrono_literals;
namespace {
bool until(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) QTest::qWait(1);
    return done();
}
AVFrame *audioFrame(bool speech)
{
    auto *frame = av_frame_alloc();
    if (!frame) qFatal("frame allocation");
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = 48000;
    frame->nb_samples = 1024;
    av_channel_layout_default(&frame->ch_layout, 2);
    if (av_frame_get_buffer(frame, 0) < 0) qFatal("frame buffer");
    for (int channel = 0; channel < 2; ++channel) {
        auto *pcm = reinterpret_cast<float *>(frame->data[channel]);
        for (int i = 0; i < 1024; ++i)
            pcm[i] = speech ? float(0.8 * std::sin(6.283185307179586 * 440 * i / 48000)) : 0.f;
    }
    return frame;
}
struct AudioPipeline {
    std::shared_ptr<AudioTap> tap = std::make_shared<AudioTap>();
    FFAFrameQueue frames;
    FFAPacketQueue packets;
    RecorderSyncQueue sync;
    FFAEncoder encoder;
    FFMuxer muxer;
    FFAEncoderThread worker;
    std::thread consumer, feeder;
    std::atomic<bool> badPacket{false}, feedDone{false};
    AVFrame *speech = audioFrame(true), *silence = audioFrame(false);
    int64_t submitted = 0;

    AudioPipeline()
    {
        frames.start(); packets.start(); sync.reset(); encoder.init(&packets);
        worker.init(nullptr, &encoder, &muxer, &frames, &sync);
        worker.setAudioTap(tap);
        consumer = std::thread([this] {
            for (;;) {
                auto packet = packets.receive();
                if (packet.result != MediaQueueResult::Ok) break;
                if (muxer.write(&packet.item->packet) != MuxWriteResult::Ok) badPacket = true;
                FFPacketTraits::release(packet.item);
            }
        });
        worker.start();
    }
    ~AudioPipeline()
    {
        // Unblock both edges on assertion failure, then join before borrowed objects die.
        frames.close(); packets.close(); worker.requestAbort();
        if (feeder.joinable()) feeder.join();
        worker.wait();
        if (consumer.joinable()) consumer.join();
        worker.setAudioTap({});
        av_frame_free(&speech); av_frame_free(&silence);
    }
    void feed(int blocks, bool isSpeech)
    {
        for (int i = 0; i < blocks; ++i) {
            auto *frame = av_frame_clone(isSpeech ? speech : silence);
            if (!frame) qFatal("frame clone");
            // Deliberately unrelated capture timestamps. The sync queue must replace
            // them with the MP4 sample clock before the tap sees the frame.
            frame->pts = 48000 * 100 + submitted * 1024;
            frames.enqueue(frame); // moves frame refs
            av_frame_free(&frame);
            ++submitted;
        }
    }
    void feedAsync(int blocks)
    {
        feeder = std::thread([this, blocks] {
            feed(blocks, false);
            frames.sendFinish();
            feedDone.store(true, std::memory_order_release);
        });
    }
    bool finish()
    {
        frames.sendFinish();
        if (!until([this] { return worker.waitFor(0ms); }, 5000)) return false;
        if (consumer.joinable()) consumer.join();
        return !badPacket && muxer.writtenPackets() > 0;
    }
};
struct LivePipeline {
    QTemporaryDir dir;
    AudioPipeline audio;
    SteadyClock clock;
    std::shared_ptr<PcmSpool> spool = std::make_shared<PcmSpool>();
    IngestWorker ingest{audio.tap, spool, clock};
    AsrHostClient host{ASR_EXE};
    SubtitleDocument document;
    SentenceFixer fixer{FixerRules{}};
    LocalAgreement agreement{2, 300};
    SubtitleMerger merger{document, {}, fixer, agreement, clock};
    QTimer poll;
    std::vector<FinalSegment> finals;
    std::vector<int64_t> splices;
    QElapsedTimer pauseTimer;
    qint64 finalAfterPauseMs = -1;
    bool opened = false, done = false;
    int hostError = 0;

    LivePipeline()
    {
        if (!spool->create(std::filesystem::u8path(dir.filePath("audio.wav").toStdString())).ok())
            qFatal("spool create");
        if (!ingest.start().ok()) qFatal("ingest start");
        host.setExtraArguments({"--engine=fake", "--fake=rtf=0.01"});
        QObject::connect(&host, &AsrHostClient::ready, &poll, [this] {
            EngineRequest request;
            request.engineId = "fake";
            request.vad.minSpeechMs = 64;
            request.vad.minSilenceMs = 64;
            request.vad.speechPadMs = 0;
            LiveConfig live;
            live.partialIntervalMs = 0;
            live.maxSegmentMs = 10000;
            live.appendThrottleMs = 20;
            host.load(request, live);
        });
        QObject::connect(&host, &AsrHostClient::loaded, &poll, [this] {
            host.open(1, dir.filePath("audio.wav"), 0, spool->committedSamples(), true);
            opened = true;
        });
        QObject::connect(&host, &AsrHostClient::failed, &poll,
                         [this](JobId, int code, QString) { hostError = code; });
        QObject::connect(&host, &AsrHostClient::jobDone, &poll, [this] { done = true; });
        QObject::connect(&host, &AsrHostClient::finalReady, &poll, [this](FinalSegment segment) {
            if (pauseTimer.isValid() && finalAfterPauseMs < 0) finalAfterPauseMs = pauseTimer.elapsed();
            finals.push_back(segment);
            merger.onFinal(segment);
        });
        QObject::connect(&poll, &QTimer::timeout, &poll, [this] { forward(); });
        poll.start(20);
        host.start(true);
    }
    ~LivePipeline() { poll.stop(); host.kill(); }
    void forward()
    {
        if (!opened) return;
        host.append(1, spool->committedSamples());
        const auto splice = spool->takeSplice();
        if (splice >= 0) {
            splices.push_back(splice);
            merger.onSplice(splice);
            host.splice(1, splice);
        }
    }
    bool beforePause()
    {
        if (!until([this] { return opened || hostError; }, 3000) || hostError) return false;
        audio.feed(30, false); // 640 ms silence, independently known onset
        audio.feed(60, true);  // 1280 ms continuous speech: no silence endpoint
        return until([this] {
            // FIR retains ten 16-kHz samples until more input/EOF, < 1 ms.
            return audio.tap->stats().pushed == 90 && spool->committedSamples() >= 30710;
        }, 3000);
    }
    void pause()
    {
        pauseTimer.start();
        audio.worker.onPauseChanged(true, av_gettime_relative());
    }
};
}

class PipelineTests : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        qRegisterSubtitleMetaTypes();
        // Same runtime gate as the app; AsrHostClient sets the child-only fake opt-in.
        qputenv("SUBREC_AUTOTEST_SCENARIO", "subtitle_pipeline");
        qunsetenv("SUBREC_TEST_ASR_TIME_SCALE");
        QVERIFY(capture::test::hooksEnabled());
        QVERIFY2(avcodec_find_encoder(AV_CODEC_ID_AAC), "AAC encoder is required (no decoder needed)");
    }
    void cue_timing_splices()
    {
        LivePipeline f;
        QVERIFY(f.beforePause());
        QVERIFY(f.finals.empty()); // long segment, no endpoint before pause
        f.pause();
        QVERIFY(until([&] { return !f.finals.empty(); }, 1000));
        QVERIFY(!f.splices.empty());
        // Paused input is discarded and must not extend the subtitle timeline.
        f.audio.feed(12, true);
        QVERIFY(until([&] { return f.audio.frames.peekEmpty(); }, 1000));
        QTest::qWait(30);
        QCOMPARE(f.audio.tap->stats().pushed, uint64_t(90));
        f.audio.worker.onPauseChanged(false, av_gettime_relative());
        f.audio.feed(60, true);
        f.audio.feed(30, false);
        QVERIFY(f.audio.finish());
        QVERIFY(f.ingest.join(3000ms));
        f.forward();
        f.host.finish(1, f.spool->committedSamples());
        QVERIFY(until([&] { return f.done || f.hostError; }, 3000));
        QCOMPARE(f.hostError, 0);
        QVERIFY(f.done);
        QVERIFY(f.finals.size() >= 2);
        const auto &cues = f.document.cues();
        QCOMPARE(cues.size(), size_t(2));
        QVERIFY(std::abs(cues[0].beginMs - 640) <= 30);
        QVERIFY(std::abs(cues[1].beginMs - 1920) <= 30);
        for (const auto &cue : cues) {
            QVERIFY(cue.beginMs < cue.endMs);
            // An empty-queue resume consumes the mark at committedSamples();
            // otherwise the next block supplies its PTS. FIR retention means
            // the actual boundary need not be exactly 1920 ms.
            for (const auto splice : f.splices) {
                const auto spliceMs = splice / 16;
                QVERIFY2(!(cue.beginMs < spliceMs && cue.endMs > spliceMs),
                         qPrintable(QStringLiteral("cue [%1,%2] ms crosses splice %3 samples (%4 ms)")
                                        .arg(cue.beginMs).arg(cue.endMs).arg(splice).arg(spliceMs)));
            }
        }
        QCOMPARE(f.audio.tap->stats().splices, uint64_t(2));
        QCOMPARE(f.audio.tap->stats().droppedBlocks, uint64_t(0));
        QCOMPARE(f.audio.tap->stats().formatRejects, uint64_t(0));
        QVERIFY(f.ingest.lastError().ok());
        qInfo() << "[SubtitlePipeline] maxPushNs=" << f.audio.tap->stats().maxPushNs;
    }
    void pause_final_latency()
    {
        LivePipeline f;
        QVERIFY(f.beforePause());
        QVERIFY(f.finals.empty());
        f.pause();
        // No resume, EOF, or additional input is sent before the measured final.
        QVERIFY(until([&] { return f.finalAfterPauseMs >= 0; }, 1000));
        QVERIFY(f.finalAfterPauseMs <= 1000);
        QCOMPARE(f.audio.tap->stats().pushed, uint64_t(90));
        QVERIFY(!f.document.cues().empty());
        qInfo() << "[SubtitlePipeline] pause-to-final ms=" << f.finalAfterPauseMs;
    }
    void hung_host_throughput()
    {
        // Three alternating pairs, fixed work, strict median throughput comparison.
        // Both sides use the same tap/ingester/real AAC worker; only host liveness differs.
        std::vector<double> baseline, hung;
        constexpr int blocks = 100000;
        for (int trial = 0; trial < 6; ++trial) {
            const bool hang = (trial / 2) % 2 == 0 ? trial % 2 == 1 : trial % 2 == 0;
            QTemporaryDir dir;
            QFile script(dir.filePath("host.script"));
            QVERIFY(script.open(QIODevice::WriteOnly));
            script.write("expect hello\nreply-hello\nexpect load\n"
                         "send {\"type\":\"loaded\",\"backend\":\"cpu\",\"loadMs\":0}\n"
                         "expect open\n"
                         "send {\"type\":\"busy\",\"jobId\":1,\"kind\":\"final\",\"b16k\":0,\"e16k\":16000}\n");
            script.write(hang ? "hang\n" : "hb 50\nexpect quit\nexit 0\n");
            script.close();
            AsrHostClient host(FAKEHOST_EXE);
            host.setExtraArguments({"--script=" + script.fileName()});
            QSignalSpy ready(&host, &AsrHostClient::ready), loaded(&host, &AsrHostClient::loaded);
            QSignalSpy busy(&host, &AsrHostClient::busy), heartbeat(&host, &AsrHostClient::heartbeat);
            QSignalSpy errors(&host, &AsrHostClient::failed), finals(&host, &AsrHostClient::finalReady);
            AudioPipeline audio;
            SteadyClock clock;
            auto spool = std::make_shared<PcmSpool>();
            QVERIFY(spool->create(std::filesystem::u8path(dir.filePath("bench.wav").toStdString())).ok());
            IngestWorker ingest(audio.tap, spool, clock);
            QVERIFY(ingest.start().ok());
            host.start(true);
            QVERIFY(until([&] { return !ready.empty(); }, 3000));
            EngineRequest request; request.engineId = "fake";
            host.load(request);
            QVERIFY(until([&] { return !loaded.empty(); }, 3000));
            host.open(1, dir.filePath("bench.wav"), 0, 0, true);
            QVERIFY(until([&] { return !busy.empty(); }, 3000));
            // Warm codec allocations before measurement, and allow the script to enter hang.
            audio.feed(512, false);
            QVERIFY(until([&] { return audio.tap->stats().pushed == 512; }, 3000));
            QTest::qWait(100);
            if (hang) QVERIFY(heartbeat.empty());
            else QVERIFY(!heartbeat.empty());
            const auto before = audio.tap->stats();
            QTimer append;
            QObject::connect(&append, &QTimer::timeout, &host, [&] { host.append(1, spool->committedSamples()); });
            append.start(20);
            QElapsedTimer elapsed; elapsed.start();
            audio.feedAsync(blocks);
            QVERIFY(until([&] { return audio.feedDone && audio.worker.waitFor(0ms); }, 24000));
            const double seconds = elapsed.nsecsElapsed() / 1e9;
            audio.feeder.join();
            append.stop();
            QVERIFY(ingest.join(3000ms));
            const auto stats = audio.tap->stats();
            // Accepted + ring-full drops account for every real worker tap call.
            QCOMPARE(stats.pushed - before.pushed + stats.droppedBlocks - before.droppedBlocks, uint64_t(blocks));
            QCOMPARE(stats.formatRejects, uint64_t(0));
            QVERIFY(!audio.badPacket);
            QVERIFY(audio.muxer.writtenPackets() >= uint64_t(blocks));
            QVERIFY(errors.empty());
            QVERIFY(finals.empty());
            QVERIFY(host.isRunning());
            if (hang) QVERIFY(heartbeat.empty());
            (hang ? hung : baseline).push_back(blocks / seconds);
            qInfo() << "[SubtitlePipeline] hang=" << hang << "blocks/s=" << blocks / seconds
                    << "maxPushNs=" << stats.maxPushNs << "tapDrops=" << stats.droppedBlocks;
            host.kill();
        }
        std::sort(baseline.begin(), baseline.end()); std::sort(hung.begin(), hung.end());
        QCOMPARE(baseline.size(), size_t(3)); QCOMPARE(hung.size(), size_t(3));
        const double change = std::abs(hung[1] / baseline[1] - 1.0);
        qInfo() << "[SubtitlePipeline] median throughput change=" << change;
        QVERIFY2(change < 0.05, "Host-hang throughput must change by < 5%; timing failure is not skipped");
    }
    void tap_exit_paths()
    {
        // Verify RAII on an empty EOF, closed/error input and requested abort.
        for (int mode = 0; mode < 3; ++mode) {
            FFAFrameQueue frames;
            FFAEncoderThread worker;
            auto tap = std::make_shared<AudioTap>();
            frames.start();
            worker.init(nullptr, nullptr, nullptr, &frames, nullptr);
            worker.setAudioTap(tap);
            if (mode == 0) frames.sendFinish();
            if (mode == 1) frames.close(MediaQueueCloseReason::Error, AVERROR(EIO));
            worker.start();
            if (mode == 2) worker.requestAbort();
            const bool joined = worker.waitFor(1000ms);
            if (!joined) { worker.requestAbort(); worker.wait(); }
            QVERIFY(joined);
            AudioTap::Block block{};
            QCOMPARE(tap->pop(block), AudioTap::Pop::Eof);
        }
    }
};
QTEST_GUILESS_MAIN(PipelineTests)
#include "pipeline_tests.moc"
