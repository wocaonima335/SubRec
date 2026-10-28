#include "logging/frame_log.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
namespace {
struct Line { QtMsgType type; std::string category, text; };
std::vector<Line> received;
void collect(QtMsgType type, const QMessageLogContext &context, const QString &text) {
    received.push_back({type, context.category ? context.category : "", text.toStdString()});
}
bool has(QtMsgType type, const char *text) {
    return std::any_of(received.begin(), received.end(), [&](const Line &line) {
        return line.type == type && line.text == text;
    });
}
}
int main(int argc, char **argv) {
    bool expected = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--expect-debug") expected = true;
    const bool enabled = subrec::logging::configure(argc, argv); // Same order as main.
    QCoreApplication app(argc, argv);
    const auto previous = qInstallMessageHandler(collect);
    int result = 0;
    try {
        CHECK(enabled == expected);
        subrec::logging::reportLevel(enabled);
        CHECK(received.size() == 1);
        CHECK(has(QtInfoMsg, subrec::logging::levelMessage(expected)));
        int evaluated = 0;
        for (int i = 0; i < 10000; ++i)
            qCDebug(subrec::logging::frameLog) << "[FFBoundedQueue<AVFrame>] enqueue before lock" << ++evaluated;
        CHECK(evaluated == (expected ? 10000 : 0));
        CHECK(received.size() == (expected ? 10001u : 1u));
        const char *traces[] = {
            "[Mux Input]", "[Mux Write]", "[FFVFilterThread] dequeue done, frame=",
            "[VEnc] before send_frame", "[AEnc] send_frame: nb_samples=",
            "[VDec] before receive_frame", "[DemuxHash] type=", "[PreviewInputMd5]"
        };
        for (auto text : traces) {
            qCDebug(subrec::logging::frameLog).noquote() << text;
            CHECK(has(QtDebugMsg, text) == expected);
        }
        for (const auto &line : received)
            if (line.type == QtDebugMsg) CHECK(line.category == "subrec.frames");

        // These use exactly their original category/severity, even debug-level
        // lifecycle messages and legacy errors. No filtering/reformatting of text.
        const char *summaries[] = {"[WASAPI] finished samples=48000", "[MuxThread] closed bootstrap_max_packets=24",
            "[AudioMix] finished system_frames=48", "[AutoTest] exit code 3", "[initVideo] ddagrab screen ready:"};
        for (auto text : summaries) { qInfo().noquote() << text; CHECK(has(QtInfoMsg, text)); }
        qDebug() << "[CaptureScheduler] state Preparing -> Running";
        CHECK(has(QtDebugMsg, "[CaptureScheduler] state Preparing -> Running"));
        qDebug() << "[Muxer] Mux Fail!";
        CHECK(has(QtDebugMsg, "[Muxer] Mux Fail!"));
        qWarning() << "[initVideo] ddagrab screen failed:";
        CHECK(has(QtWarningMsg, "[initVideo] ddagrab screen failed:"));
        qCritical() << "capture error";
        CHECK(has(QtCriticalMsg, "capture error"));
        // Category warnings are never disabled with frame debug.
        qCWarning(subrec::logging::frameLog) << "frame warning";
        CHECK(has(QtWarningMsg, "frame warning"));
        std::printf("PASS Qt frame logging: debug=%d evaluated=%d; summaries/state/errors unchanged\n", enabled, evaluated);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        result = 1;
    }
    qInstallMessageHandler(previous);
    return result;
}
