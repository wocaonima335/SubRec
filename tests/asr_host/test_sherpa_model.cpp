// PC3 opt-in: no bundled/downloaded models or audio.
// Required: SUBREC_ASR_MODEL_DIR, SUBREC_ASR_TEST_WAV (mono 16 kHz, 3..120 s),
// SUBREC_ASR_TEST_KEYWORD (UTF-8, occurring early in a >=3 s speech segment).
// VAD: installed beside the host, or SUBREC_ASR_VAD_MODEL. Optional:
// SUBREC_ASR_TEST_LANGUAGE (default zh), SUBREC_ASR_TEST_MAX_RTF (default 1.0).
// The RTF includes partial work via progress.decodeMsTotal. PC3 should also
// record the stricter product budgets under concurrent recording load.
#include "test_framework.h"
#include "host_driver.h"
#include "wav_builder.h"
#include "engine_sherpa.h"

#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace subtitle;

namespace {
std::string environment(const char *name)
{
#ifdef _WIN32
    const auto wideName = testpaths::toWide(name);
    const wchar_t *value = _wgetenv(wideName.c_str());
    return value ? std::filesystem::path(value).u8string() : "";
#else
    const char *value = std::getenv(name);
    return value ? value : "";
#endif
}

std::string requiredEnvironment(const char *name)
{
    const auto value = environment(name);
    CHECK_MSG(!value.empty(), std::string("needs_asr_model: set ") + name + " to a local test fixture");
    return value;
}

struct SpoolFile {
    std::filesystem::path path = std::filesystem::current_path()
        / ("sherpa-growing-" + std::to_string(HostSession::nowMs()) + ".wav");
    ~SpoolFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};
struct CloseFile {
    void operator()(std::FILE *file) const { std::fclose(file); }
};
} // namespace

TEST(sherpa_growing_wav_keywords_rtf)
{
    const auto modelDir = requiredEnvironment("SUBREC_ASR_MODEL_DIR");
    const auto wavPath = requiredEnvironment("SUBREC_ASR_TEST_WAV");
    const auto keyword = requiredEnvironment("SUBREC_ASR_TEST_KEYWORD");
    const auto maxRtfText = environment("SUBREC_ASR_TEST_MAX_RTF");
    const double maxRtf = maxRtfText.empty() ? 1.0 : std::stod(maxRtfText);
    CHECK(std::isfinite(maxRtf) && maxRtf > 0);
    std::unique_ptr<const SherpaOnnxWave, decltype(&SherpaOnnxFreeWave)>
        wave(SherpaOnnxReadWave(wavPath.c_str()), &SherpaOnnxFreeWave);
    CHECK_MSG(wave, "needs_asr_model: cannot read SUBREC_ASR_TEST_WAV");
    CHECK_MSG(wave->sample_rate == 16000 && wave->num_samples >= 48000
              && wave->num_samples <= 120 * 16000, "test WAV must be mono 16 kHz, 3..120 seconds");

    SpoolFile spool;
    WavSpoolBuilder header;
    CHECK(header.create(spool.path.u8string()));
    header.close(); // Deliberately leave RIFF/data lengths stale while appending.
    std::unique_ptr<std::FILE, CloseFile>
        writer(testpaths::fopenUtf8(spool.path.u8string(), "ab"));
    CHECK(writer);
    HostSession host;
    CHECK(host.proc.spawn(asrExe(), {"--engine=sherpa-onnx"}));
    host.handshake();
    proto::Load load;
    load.request.engineId = "sherpa-onnx";
    load.request.modelId = "sense-voice-test";
    load.request.modelDir = std::filesystem::u8path(modelDir);
    const auto language = environment("SUBREC_ASR_TEST_LANGUAGE");
    load.request.language = language.empty() ? "zh" : language;
    load.request.threads = 1;
    load.request.vad.maxSegmentMs = load.live.maxSegmentMs;
    host.send(load);
    const int64_t loadDeadline = HostSession::nowMs() + 60000;
    for (;;) {
        CHECK_MSG(HostSession::nowMs() < loadDeadline, "SenseVoice load timed out");
        proto::Message message;
        CHECK(host.readMsg(message, 5000));
        if (const auto *error = std::get_if<proto::Error>(&message))
            CHECK_MSG(false, "SenseVoice load: " + error->message);
        if (std::holds_alternative<proto::Loaded>(message))
            break;
    }

    host.openJob(spool.path.u8string(), 0, 0);
    const int64_t start = HostSession::nowMs();
    // One second of trailing silence exercises natural VAD endpointing too.
    const int64_t total = static_cast<int64_t>(wave->num_samples) + 16000;
    int64_t published = 0, previousEnd = 0;
    bool finishing = false, done = false, partialKeyword = false;
    std::string finalText;
    double decodeMs = 0;
    std::vector<int64_t> latencies;
    const int64_t deadline = start + total / 16 + 60000;
    while (!done && HostSession::nowMs() < deadline) {
        const auto now = HostSession::nowMs();
        // Publish 200 ms batches at real-time speed, with writer flush before append.
        if (published < total && now - start >= std::min(total, published + 3200) / 16) {
            const int64_t next = std::min(total, published + 3200);
            std::vector<unsigned char> bytes;
            for (int64_t i = published; i < next; ++i) {
                const float sample = i < wave->num_samples ? wave->samples[i] : 0.f;
                const int value = static_cast<int>(std::clamp(sample, -1.f, 1.f) * 32767);
                bytes.push_back(static_cast<unsigned char>(value & 255));
                bytes.push_back(static_cast<unsigned char>((value >> 8) & 255));
            }
            CHECK(std::fwrite(bytes.data(), 1, bytes.size(), writer.get()) == bytes.size());
            CHECK(std::fflush(writer.get()) == 0);
            published = next;
            host.appendCommitted(published);
        }
        if (published == total && !finishing) {
            host.finishJob(total);
            finishing = true;
        }
        proto::Message message;
        if (!host.readMsg(message, 10)) {
            CHECK_MSG(!host.proc.isEof(), "SenseVoice host exited during WAV replay");
            continue;
        }
        if (const auto *error = std::get_if<proto::Error>(&message))
            CHECK_MSG(false, "SenseVoice replay: " + error->message);
        if (const auto *partial = std::get_if<proto::PartialMsg>(&message)) {
            CHECK(partial->seg.begin16k >= 0 && partial->seg.upto16k <= published);
            partialKeyword = partialKeyword || partial->seg.text.find(keyword) != std::string::npos;
        }
        if (const auto *final = std::get_if<proto::FinalMsg>(&message)) {
            const auto &f = final->seg;
            CHECK(f.begin16k >= previousEnd && f.end16k > f.begin16k && f.end16k <= published);
            CHECK(f.modelId == "sense-voice-test" && f.pass == 1);
            for (const auto &word : f.words)
                CHECK(word.beginMs >= f.begin16k / 16 && word.endMs <= f.end16k / 16
                      && word.endMs >= word.beginMs);
            previousEnd = f.end16k;
            finalText += f.text;
            latencies.push_back(HostSession::nowMs() - start - f.end16k / 16);
        }
        if (const auto *progress = std::get_if<proto::Progress>(&message))
            decodeMs = progress->decodeMsTotal;
        done = std::holds_alternative<proto::Done>(message);
    }
    CHECK_MSG(done, "SenseVoice did not finish growing WAV replay");
    CHECK_MSG(partialKeyword, "no partial contained SUBREC_ASR_TEST_KEYWORD; use >=3 s speech with an early keyword");
    CHECK_MSG(finalText.find(keyword) != std::string::npos, "final keyword missing: " + finalText);
    const double rtf = decodeMs / (wave->num_samples / 16.0);
    CHECK(std::isfinite(rtf) && rtf > 0 && rtf < maxRtf);
    CHECK(!latencies.empty());
    std::sort(latencies.begin(), latencies.end());
    const auto percentile = [&](double p) {
        return latencies[static_cast<size_t>(std::ceil(p * latencies.size())) - 1];
    };
    std::printf("SenseVoice total RTF %.3f; final latency P50 %lld ms, P95 %lld ms\n", rtf,
                static_cast<long long>(percentile(.5)), static_cast<long long>(percentile(.95)));
    CHECK(percentile(.5) <= 1500 && percentile(.95) <= 3000); // design §4.1 targets, measured from padded VAD ends
    host.quit();
    const auto exit = host.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}

TEST(sherpa_vad_stream_boundaries)
{
    const auto path = requiredEnvironment("SUBREC_ASR_TEST_WAV");
    std::unique_ptr<const SherpaOnnxWave, decltype(&SherpaOnnxFreeWave)>
        wave(SherpaOnnxReadWave(path.c_str()), &SherpaOnnxFreeWave);
    CHECK(wave && wave->sample_rate == 16000 && wave->num_samples >= 48000);
    EngineRequest req;
    req.engineId = "sherpa-onnx";
    Status st;
    auto vad = asrhost::createVad(req, &st);
    CHECK_MSG(vad, "needs_asr_model: " + st.message);
    const int64_t total = wave->num_samples;
    using Ranges = std::vector<std::pair<int64_t, int64_t>>;
    auto feed = [&](int64_t from, int64_t end, size_t chunk, Ranges &ranges) {
        while (from < end) {
            const size_t n = static_cast<size_t>(std::min<int64_t>(end - from, chunk));
            vad->feed(wave->samples + from, n, from, ranges);
            if (vad->inSpeech()) {
                CHECK(vad->openSegmentBegin16k() >= 0);
                CHECK(vad->openSegmentBegin16k() < from + static_cast<int64_t>(n));
            }
            from += static_cast<int64_t>(n);
        }
    };
    auto checkRanges = [](const Ranges &ranges, int64_t floor, int64_t end) {
        for (const auto &range : ranges) {
            CHECK(range.first >= floor && range.second > range.first && range.second <= end);
            floor = range.second;
        }
    };

    Ranges oneBatch, fragmented;
    vad->reset(0);
    feed(0, total, static_cast<size_t>(total), oneBatch);
    vad->finish(oneBatch);
    CHECK_MSG(!oneBatch.empty(), "test WAV must contain speech detected by Silero");
    CHECK(!vad->inSpeech() && vad->openSegmentBegin16k() == -1);
    checkRanges(oneBatch, 0, total);
    vad->reset(0);
    feed(0, total, 137, fragmented); // deliberately not a 512 multiple
    vad->finish(fragmented);
    CHECK(oneBatch == fragmented);

    const int64_t resume = oneBatch.front().first + 37;
    Ranges resumed;
    vad->reset(resume);
    CHECK(!vad->inSpeech());
    feed(resume, total, 701, resumed);
    vad->finish(resumed);
    checkRanges(resumed, resume, total);

    // Splice while speech is open; subsequent VAD padding cannot cross it.
    vad->reset(0);
    Ranges before;
    int64_t cut = 0;
    while (cut < total && !vad->inSpeech()) {
        const auto next = std::min(total, cut + 137);
        feed(cut, next, 137, before);
        cut = next;
    }
    CHECK(vad->inSpeech());
    vad->forceCut(cut);
    CHECK(!vad->inSpeech() && vad->openSegmentBegin16k() == -1);
    Ranges after;
    feed(cut, total, 701, after);
    vad->finish(after);
    checkRanges(after, cut, total);
    CHECK(!vad->inSpeech());
    const auto count = after.size();
    vad->finish(after);
    CHECK(after.size() == count); // finish is idempotent
}
