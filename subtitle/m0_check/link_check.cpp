// M0 链接检查：调用全部纯 C++17 目标中声明的函数，确认每个非 inline 声明都有（桩）定义。
// 不检查行为——桩的返回值没有意义。返回 0 即通过。

#include "subtitle/audio/ingest_worker.h"
#include "subtitle/audio/mono_resampler_16k.h"
#include "subtitle/audio/pcm_spool.h"
#include "subtitle/core/cue_builder.h"
#include "subtitle/core/event_tags.h"
#include "subtitle/core/glossary.h"
#include "subtitle/core/hallucination_filter.h"
#include "subtitle/core/itn_extra.h"
#include "subtitle/core/subtitle_writers.h"
#include "subtitle/core/text_normalizer.h"
#include "subtitle/host/asr_protocol.h"
#include "subtitle/host/job_limits.h"
#include "subtitle/live/degrade_controller.h"
#include "subtitle/live/host_watchdog.h"
#include "subtitle/live/local_agreement.h"
#include "subtitle/live/sentence_fixer.h"
#include "subtitle/live/subtitle_merger.h"
#include "subtitle/subtitle_clock.h"
#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"
#include "subtitle/tap/audio_tap.h"
#include "subtitle/tap/recording_hook.h"

#include <cstdio>
#include <memory>

using namespace subtitle;

int main()
{
    int failures = 0;
    auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    };

    // ---- 错误码与类型 ----
    expect(std::string(errorCodeName(ErrorCode::HostHeartbeatLost)) == "HostHeartbeatLost", "errorCodeName");
    expect(errorCategory(ErrorCode::RefineHostFailed) == 7, "errorCategory");
    static_assert(tapToAsrSamples(48000) == 16000, "tapToAsrSamples");
    static_assert(asrSamplesToMs(16000) == 1000, "asrSamplesToMs");
    static_assert(JobLimits::liveCpuRate(8) == 1875, "liveCpuRate");

    // ---- A ----
    auto tap = std::make_shared<AudioTap>();
    tap->markSplice();
    tap->finish();
    tap->abort();
    AudioTap::Block block{};
    (void)tap->pop(block);
    tap->release();
    (void)tap->spliceRequested();
    tap->consumeSpliceRequest();
    (void)tap->stats();

    MonoResampler16k rs;
    std::vector<float> out;
    rs.process(nullptr, 0, out);
    rs.flush(out);
    rs.reset();

    auto spool = std::make_shared<PcmSpool>();
    (void)spool->create("x.wav");
    (void)spool->append(nullptr, 0);
    spool->publish(160);
    spool->markSplice16k(80);
    expect(spool->takeSplice() == 80 && spool->takeSplice() == -1, "PcmSpool::takeSplice");
    expect(spool->committedSamples() == 160, "PcmSpool::committedSamples");
    (void)spool->finalize();
    (void)spool->writtenSamples();
    (void)spool->path();

    ManualClock clock;
    {
        IngestWorker ingest(tap, spool, clock);
        (void)ingest.start();
        ingest.requestStop();
        (void)ingest.join(std::chrono::milliseconds(1));
        (void)ingest.stats();
        (void)ingest.lastError();
    }

    // ---- B / K ----
    (void)normalizeText("a", NormalizeOptions{});
    (void)countCodePoints("a");
    (void)effectiveCharCount("a");
    (void)asciiLetterRatio("a");
    (void)isLikelyHallucination(Segment{}, HallucinationRules{});
    FinalSegment fin;
    (void)buildCues(fin, CueConfig{}, {});
    std::vector<Cue> cues;
    resolveOverlaps(cues);
    (void)formatSrtTimestamp(0);
    (void)formatSrt(cues, true, true);
    (void)formatSrt(cues, SrtOptions{});
    (void)formatVtt(cues);
    (void)formatAss(cues, AssStyle{});
    std::vector<Segment> segs;
    (void)toJson(segs);
    (void)fromJson("[]", segs);
    (void)parseSrt("", cues);
    (void)writeFileAtomic("x.srt", "");
    (void)srtPathForMp4("a.mp4");
    (void)srtPartPath("a.srt");
    (void)liveSrtPath("a.srt");
    (void)refinePartPath("a.srt");
    std::string text = "a";
    (void)applyGlossary(text, {});
    (void)glossaryMiss(text, {});
    (void)buildWhisperPrompt({});
    (void)eventTagFor("BGM");
    (void)applyEventTag(fin);
    (void)applyItn("a", ItnRules{});

    // ---- H ----
    LocalAgreement la(2, 300);
    la.reset(1);
    (void)la.push(PartialSegment{});
    la.freeze();
    (void)la.state();
    (void)la.mismatchAgainst(fin);
    (void)la.stats();
    la.recordFinal(fin);

    SubtitleDocument doc;
    (void)doc.acceptFinal(fin);
    doc.addHardBoundary(10);
    doc.markPoison(0, 1);
    (void)doc.resumePoint16k();
    (void)doc.cues();
    (void)doc.segmentsForSidecar();
    (void)doc.revision();
    doc.setProvisional(1, ConfirmedPrefix{});
    doc.dropProvisional(1);
    (void)doc.revisableTail(2);
    (void)doc.acceptRefined(fin);
    (void)doc.state(1);
    doc.markUserEdited(1);
    (void)doc.refineProgress();
    (void)doc.hardBoundaries16k();
    doc.setSentenceState(1, SentenceState::Fixed);
    doc.setRefineTotal(1);

    SentenceFixer fixer(FixerRules{});
    (void)fixer.apply(fin, doc, true);
    SubtitleMerger merger(doc, CueConfig{}, fixer, la, clock);
    merger.setLiveConfig(LiveConfig{});
    merger.onPartial(PartialSegment{});
    merger.onFinal(fin);
    merger.onRefined(fin);
    merger.onLevelChanged(DegradeLevel::L1FinalOnly);
    merger.onSplice(10);
    merger.onHostRestarted();
    (void)merger.current();
    (void)merger.shouldFlushPart(0);
    (void)merger.flushPart("a.srt.part", SrtOptions{});
    (void)merger.finalize("a.srt", SrtOptions{});
    (void)merger.finalizeRefined("a.srt", SrtOptions{}, false);
    (void)SubtitleMerger::resumeFromPart("a.srt.part", doc);

    // ---- W ----
    DegradeController dc(DegradeThresholds{});
    (void)dc.tick(LoadSample{});
    dc.forceLevel(DegradeLevel::L3Deferred, "manual");
    expect(dc.level() == DegradeLevel::L3Deferred && dc.reason() == "manual", "DegradeController::forceLevel");

    HostWatchdog wd(WatchdogConfig{}, clock);
    wd.onProcessStarted();
    wd.onHeartbeat(1);
    wd.onBusy(0, 16000, 16000);
    wd.onIdle();
    wd.onLoadStarted();
    wd.onLoaded();
    wd.setBackend(Backend::Cpu);
    (void)wd.poll(0);
    (void)wd.onExit(1, true, 0);
    wd.resetForJob();

    // ---- C：协议 ----
    proto::Message hello = proto::Hello{};
    expect(std::string(proto::typeName(hello)) == "hello", "proto::typeName");
    expect(std::string(proto::typeName(proto::Message{proto::Heartbeat{}})) == "hb", "proto::typeName hb");
    (void)proto::encode(hello);
    Status err;
    (void)proto::decode("{}", &err);
    expect(!proto::acceptedByRole(proto::Message{proto::Append{}}, proto::kRoleRefine), "acceptedByRole refine");
    expect(proto::acceptedByRole(proto::Message{proto::Refine{}}, proto::kRoleRefine), "acceptedByRole refine2");
    expect(proto::parseBackend(proto::backendName(Backend::Vulkan)) == Backend::Vulkan, "backend roundtrip");

    std::printf(failures ? "subtitle_m0_link_check: %d failure(s)\n" : "subtitle_m0_link_check: OK\n", failures);
    return failures ? 1 : 0;
}
