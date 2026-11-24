#include "engine_sherpa.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace asrhost {
namespace {
using subtitle::ErrorCode;
using subtitle::Status;
constexpr int kWindow = 512;

bool regularFile(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path executableDirectory()
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!n)
            return {};
        if (n < buffer.size())
            return std::filesystem::path(std::wstring(buffer.data(), n)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    return std::filesystem::path(buffer.data()).parent_path();
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : exe.parent_path();
#endif
}

std::filesystem::path vadModelPath()
{
#if defined(_WIN32)
    // Environment paths must survive Windows' non-UTF-8 ANSI code pages.
    if (const wchar_t *value = _wgetenv(L"SUBREC_ASR_VAD_MODEL"))
        if (*value)
            return value;
#else
    if (const char *value = std::getenv("SUBREC_ASR_VAD_MODEL"))
        if (*value)
            return std::filesystem::u8path(value);
#endif
    const auto dir = executableDirectory();
    return dir.empty() ? std::filesystem::path{} : dir / "silero_vad.onnx";
}

class SileroVad final : public IVad {
public:
    using Detector = std::unique_ptr<const SherpaOnnxVoiceActivityDetector,
                                    decltype(&SherpaOnnxDestroyVoiceActivityDetector)>;
    SileroVad(Detector detector, const subtitle::VadConfig &cfg)
        : m_detector(std::move(detector)), m_pad(static_cast<int64_t>(cfg.speechPadMs) * 16),
          m_minSpeech(static_cast<int64_t>(cfg.minSpeechMs) * 16)
    {
        reset(0);
    }

    void feed(const float *pcm, size_t n, int64_t first,
              std::vector<std::pair<int64_t, int64_t>> &closed) override
    {
        if (first != m_received)
            reset(first);
        while (n) {
            const size_t take = std::min(n, m_frame.size() - m_used);
            std::copy_n(pcm, take, m_frame.data() + m_used);
            m_used += take;
            m_received += static_cast<int64_t>(take);
            pcm += take;
            n -= take;
            if (m_used == m_frame.size())
                acceptFrame(closed);
        }
    }

    void forceCut(int64_t at) override { reset(at); }

    void finish(std::vector<std::pair<int64_t, int64_t>> &closed) override
    {
        if (m_used) {
            std::fill(m_frame.begin() + m_used, m_frame.end(), 0.f);
            acceptFrame(closed);
        }
        SherpaOnnxVoiceActivityDetectorFlush(m_detector.get());
        drain(closed);
        if (m_begin >= 0)
            emit(m_begin, m_received, closed);
        m_begin = -1;
    }

    void reset(int64_t from) override
    {
        SherpaOnnxVoiceActivityDetectorReset(m_detector.get());
        SherpaOnnxVoiceActivityDetectorClear(m_detector.get());
        m_received = m_floor = m_lastEnd = from;
        // Absolute 512 grid, as for EnergyVad. The first partial window is
        // zero-prefixed; no samples preceding a splice/resume are reused.
        m_used = static_cast<size_t>(from % kWindow);
        m_base = from - static_cast<int64_t>(m_used);
        m_processed = m_base;
        m_frame.fill(0.f);
        m_begin = -1;
    }

    bool inSpeech() const override { return m_begin >= 0; }
    int64_t openSegmentBegin16k() const override { return m_begin; }

private:
    void acceptFrame(std::vector<std::pair<int64_t, int64_t>> &closed)
    {
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(m_detector.get(), m_frame.data(), kWindow);
        m_used = 0;
        m_processed += kWindow;
        drain(closed);
        if (SherpaOnnxVoiceActivityDetectorDetected(m_detector.get()) && m_begin < 0) {
            // The C API exposes no start for an *open* segment. Back up over
            // min_speech_duration plus its confirming window for partials.
            // Completed segments supply an exact start through Front().
            m_begin = std::max({m_floor, m_lastEnd,
                                m_processed - m_minSpeech - kWindow - m_pad});
        }
    }

    void emit(int64_t begin, int64_t end, std::vector<std::pair<int64_t, int64_t>> &closed)
    {
        begin = std::max({begin, m_floor, m_lastEnd});
        end = std::min(end, m_received);
        if (end > begin) {
            closed.emplace_back(begin, end);
            m_lastEnd = end;
        }
    }

    void drain(std::vector<std::pair<int64_t, int64_t>> &closed)
    {
        while (!SherpaOnnxVoiceActivityDetectorEmpty(m_detector.get())) {
            std::unique_ptr<const SherpaOnnxSpeechSegment, decltype(&SherpaOnnxDestroySpeechSegment)>
                segment(SherpaOnnxVoiceActivityDetectorFront(m_detector.get()),
                        &SherpaOnnxDestroySpeechSegment);
            if (segment && segment->start >= 0 && segment->n > 0) {
                const int64_t begin = m_base + segment->start;
                // Preserve the begin already announced to the state machine.
                // If no open transition was observed, use the completed start.
                emit(m_begin >= 0 ? m_begin : begin - m_pad,
                     begin + segment->n + m_pad, closed);
            }
            SherpaOnnxVoiceActivityDetectorPop(m_detector.get());
            m_begin = -1;
        }
    }

    Detector m_detector;
    int64_t m_pad, m_minSpeech;
    std::array<float, kWindow> m_frame{};
    size_t m_used = 0;
    int64_t m_base = 0, m_processed = 0, m_received = 0, m_floor = 0, m_lastEnd = 0;
    int64_t m_begin = -1;
};
} // namespace

std::string sherpaLanguage(const std::string &language)
{
    return language.empty() ? "auto" : language;
}

SherpaOnnxOfflineRecognizerConfig sherpaRecognizerConfig(
    const char *model, const char *tokens, const char *language, int threads)
{
    SherpaOnnxOfflineRecognizerConfig cfg{};
    cfg.feat_config.sample_rate = subtitle::kAsrSampleRate;
    cfg.feat_config.feature_dim = 80;
    cfg.model_config.sense_voice.model = model;
    cfg.model_config.sense_voice.language = language;
    cfg.model_config.sense_voice.use_itn = 1; // D33: built-in ITN enabled in M1.
    cfg.model_config.tokens = tokens;
    cfg.model_config.model_type = "sense_voice";
    cfg.model_config.num_threads = threads;
    cfg.model_config.provider = "cpu"; // D5: M1 always runs on CPU.
    cfg.decoding_method = "greedy_search";
    return cfg;
}

SherpaOnnxVadModelConfig sherpaVadConfig(const subtitle::VadConfig &vad,
                                        const char *model, int threads)
{
    SherpaOnnxVadModelConfig cfg{};
    cfg.silero_vad.model = model;
    cfg.silero_vad.threshold = vad.threshold;
    cfg.silero_vad.min_speech_duration = vad.minSpeechMs / 1000.f;
    cfg.silero_vad.min_silence_duration = vad.minSilenceMs / 1000.f;
    cfg.silero_vad.max_speech_duration = vad.maxSegmentMs > 0
        ? vad.maxSegmentMs / 1000.f : std::numeric_limits<float>::max();
    cfg.silero_vad.window_size = kWindow;
    cfg.sample_rate = subtitle::kAsrSampleRate;
    cfg.num_threads = threads;
    cfg.provider = "cpu";
    return cfg;
}

Status resolveSherpaModel(const std::filesystem::path &dir, std::string &model, std::string &tokens)
{
    model.clear();
    tokens.clear();
    try {
        if (dir.empty())
            return Status::error(ErrorCode::ModelNotInstalled, "SenseVoice modelDir is empty");
        const auto tokenPath = dir / "tokens.txt";
        auto modelPath = dir / "model.int8.onnx";
        if (!regularFile(modelPath))
            modelPath = dir / "model.onnx";
        if (!regularFile(modelPath))
            return Status::error(ErrorCode::ModelNotInstalled,
                                 "SenseVoice requires model.int8.onnx or model.onnx in modelDir");
        if (!regularFile(tokenPath))
            return Status::error(ErrorCode::ModelNotInstalled, "SenseVoice requires tokens.txt in modelDir");
        model = modelPath.u8string();
        tokens = tokenPath.u8string();
        return {};
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const std::exception &e) {
        return Status::error(ErrorCode::EngineLoadFailed, e.what());
    }
}

Status SherpaAsrEngine::load(const subtitle::EngineRequest &request, std::string *backendUsed)
try
{
    m_recognizer.reset();
    if (backendUsed)
        backendUsed->clear();
    std::string model, tokens;
    auto status = resolveSherpaModel(request.modelDir, model, tokens);
    if (!status.ok())
        return status;
    const auto language = sherpaLanguage(request.language);
    if (request.threads < 1 || (language != "auto" && language != "zh" && language != "en"
                              && language != "ja" && language != "ko" && language != "yue"))
        return Status::error(ErrorCode::InvalidArgument, "SenseVoice requires positive threads and auto/zh/en/ja/ko/yue language");
    const auto config = sherpaRecognizerConfig(model.c_str(), tokens.c_str(), language.c_str(), request.threads);
    m_recognizer.reset(SherpaOnnxCreateOfflineRecognizer(&config));
    if (!m_recognizer)
        return Status::error(ErrorCode::EngineLoadFailed, "sherpa-onnx could not load SenseVoice");
    m_modelId = request.modelId;
    if (backendUsed)
        *backendUsed = "cpu";
    return {};
}
catch (const std::bad_alloc &) { throw; }
catch (const std::exception &e)
{
    m_recognizer.reset();
    return Status::error(ErrorCode::EngineLoadFailed, e.what());
}

void copySherpaResult(const SherpaOnnxOfflineRecognizerResult &result, subtitle::FinalSegment &io)
{
    io.text = result.text ? result.text : "";
    io.language = result.lang ? result.lang : "";
    io.emotion = result.emotion ? result.emotion : "";
    io.event = result.event ? result.event : "";
    io.words.clear();
    if (!result.timestamps || !result.tokens_arr)
        return; // Protocol v2 explicitly supports results without token times.
    const int64_t base = subtitle::asrSamplesToMs(io.begin16k);
    const int64_t end = subtitle::asrSamplesToMs(io.end16k);
    const double seconds = static_cast<double>(io.end16k - io.begin16k) / subtitle::kAsrSampleRate;
    int64_t previous = base;
    for (int32_t i = 0; i < result.count; ++i) {
        if (!result.tokens_arr[i] || !std::isfinite(result.timestamps[i]))
            continue;
        double stop = seconds;
        if (result.durations && std::isfinite(result.durations[i]) && result.durations[i] >= 0)
            stop = result.timestamps[i] + static_cast<double>(result.durations[i]);
        else if (i + 1 < result.count && std::isfinite(result.timestamps[i + 1]))
            stop = result.timestamps[i + 1];
        subtitle::Word word;
        word.text = result.tokens_arr[i];
        word.beginMs = std::max(previous, std::min(end, base + static_cast<int64_t>(
            std::llround(std::clamp<double>(result.timestamps[i], 0, seconds) * 1000))));
        word.endMs = std::max(word.beginMs, std::min(end, base + static_cast<int64_t>(
            std::llround(std::clamp(stop, 0.0, seconds) * 1000))));
        word.prob = -1.f; // SenseVoice does not promise calibrated token probabilities.
        previous = word.endMs;
        io.words.push_back(std::move(word));
    }
}

Status SherpaAsrEngine::recognize(const float *pcm, size_t n, RecognizeKind,
                                 subtitle::FinalSegment &io)
try
{
    const auto generation = m_cancelGeneration.load(std::memory_order_relaxed);
    if (!m_recognizer)
        return Status::error(ErrorCode::EngineNotLoaded, "SenseVoice is not loaded");
    if (n > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        return Status::error(ErrorCode::EngineInputTooLong, "SenseVoice input exceeds the C API sample limit");
    if (!pcm || !n || io.begin16k < 0 || io.end16k < io.begin16k
        || static_cast<uint64_t>(io.end16k - io.begin16k) != n)
        return Status::error(ErrorCode::InvalidArgument, "invalid SenseVoice PCM range");
    const auto start = std::chrono::steady_clock::now();
    std::unique_ptr<const SherpaOnnxOfflineStream, decltype(&SherpaOnnxDestroyOfflineStream)>
        stream(SherpaOnnxCreateOfflineStream(m_recognizer.get()), &SherpaOnnxDestroyOfflineStream);
    if (!stream)
        return Status::error(ErrorCode::EngineRecognizeFailed, "could not create SenseVoice stream");
    SherpaOnnxAcceptWaveformOffline(stream.get(), subtitle::kAsrSampleRate, pcm, static_cast<int32_t>(n));
    if (generation != m_cancelGeneration.load(std::memory_order_relaxed))
        return Status::error(ErrorCode::Cancelled, "SenseVoice cancelled");
    SherpaOnnxDecodeOfflineStream(m_recognizer.get(), stream.get());
    if (generation != m_cancelGeneration.load(std::memory_order_relaxed))
        return Status::error(ErrorCode::Cancelled, "SenseVoice cancelled");
    std::unique_ptr<const SherpaOnnxOfflineRecognizerResult, decltype(&SherpaOnnxDestroyOfflineRecognizerResult)>
        result(SherpaOnnxGetOfflineStreamResult(stream.get()), &SherpaOnnxDestroyOfflineRecognizerResult);
    if (!result)
        return Status::error(ErrorCode::EngineRecognizeFailed, "SenseVoice returned no result");
    copySherpaResult(*result, io);
    io.modelId = m_modelId;
    io.decodeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (generation != m_cancelGeneration.load(std::memory_order_relaxed))
        return Status::error(ErrorCode::Cancelled, "SenseVoice cancelled");
    return {};
}
catch (const std::bad_alloc &) { throw; }
catch (const std::exception &e)
{
    return Status::error(ErrorCode::EngineRecognizeFailed, e.what());
}

void SherpaAsrEngine::requestCancel() noexcept
{
    m_cancelGeneration.fetch_add(1, std::memory_order_relaxed);
}

std::unique_ptr<IVad> createSherpaVad(const subtitle::EngineRequest &request, Status *err,
                                     const std::filesystem::path &modelPath)
{
    auto fail = [err](ErrorCode code, const std::string &message) -> std::unique_ptr<IVad> {
        if (err)
            *err = Status::error(code, message);
        return nullptr;
    };
    try {
        const auto path = modelPath.empty() ? vadModelPath() : modelPath;
        if (path.empty() || !regularFile(path))
            return fail(ErrorCode::VadLoadFailed,
                        "Silero model missing: install silero_vad.onnx beside subrec-asr or set SUBREC_ASR_VAD_MODEL");
        const auto &v = request.vad;
        if (request.threads < 1 || !std::isfinite(v.threshold) || v.threshold <= 0 || v.threshold >= 1
            || v.minSpeechMs <= 0 || v.minSilenceMs <= 0 || v.speechPadMs < 0 || v.maxSegmentMs < 0)
            return fail(ErrorCode::InvalidArgument, "invalid Silero VAD configuration");
        const std::string model = path.u8string();
        const auto config = sherpaVadConfig(v, model.c_str(), request.threads);
        SileroVad::Detector detector(SherpaOnnxCreateVoiceActivityDetector(&config,
            std::max(60.f, (static_cast<float>(v.maxSegmentMs) + v.minSpeechMs + v.minSilenceMs) / 1000.f + 1)),
            &SherpaOnnxDestroyVoiceActivityDetector);
        if (!detector)
            return fail(ErrorCode::VadLoadFailed, "sherpa-onnx could not load Silero VAD");
        auto vad = std::make_unique<SileroVad>(std::move(detector), v);
        if (err)
            *err = {};
        return vad;
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const std::exception &e) {
        return fail(ErrorCode::VadLoadFailed, e.what());
    }
}
} // namespace asrhost
