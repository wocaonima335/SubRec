#include "test_framework.h"
#include "test_paths.h"
#include "engine_sherpa.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace asrhost;
using namespace subtitle;

namespace {
// Each case owns its fixtures; safe with ctest -j and repeated runs.
struct ModelFiles {
    std::filesystem::path dir;
    ModelFiles()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = std::filesystem::current_path() / std::filesystem::u8path("sherpa-模型-" + std::to_string(stamp));
        CHECK(std::filesystem::create_directory(dir));
    }
    ~ModelFiles() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    void put(const char *name) { CHECK(testpaths::writeFileUtf8((dir / name).u8string(), "fixture")); }
};

struct VadEnvironment {
    bool hadValue = false;
    std::string previous;
    explicit VadEnvironment(const std::filesystem::path &path)
    {
#ifdef _WIN32
        if (const wchar_t *old = _wgetenv(L"SUBREC_ASR_VAD_MODEL")) {
            hadValue = true;
            previous = std::filesystem::path(old).u8string();
        }
        CHECK(_wputenv_s(L"SUBREC_ASR_VAD_MODEL", path.c_str()) == 0);
#else
        if (const char *old = std::getenv("SUBREC_ASR_VAD_MODEL")) {
            hadValue = true;
            previous = old;
        }
        CHECK(setenv("SUBREC_ASR_VAD_MODEL", path.c_str(), 1) == 0);
#endif
    }
    ~VadEnvironment()
    {
#ifdef _WIN32
        _wputenv_s(L"SUBREC_ASR_VAD_MODEL", testpaths::toWide(previous).c_str());
#else
        if (hadValue)
            setenv("SUBREC_ASR_VAD_MODEL", previous.c_str(), 1);
        else
            unsetenv("SUBREC_ASR_VAD_MODEL");
#endif
    }
};
} // namespace

TEST(sherpa_missing_model_files)
{
    SherpaAsrEngine engine;
    EngineRequest request;
    request.engineId = "sherpa-onnx";
    std::string backend = "unchanged";
    auto st = engine.load(request, &backend);
    CHECK(st.code == ErrorCode::ModelNotInstalled && backend.empty());
    ModelFiles files;
    request.modelDir = files.dir / "absent";
    CHECK(engine.load(request, nullptr).code == ErrorCode::ModelNotInstalled);
    request.modelDir = files.dir;
    files.put("model.int8.onnx");
    st = engine.load(request, nullptr);
    CHECK(st.code == ErrorCode::ModelNotInstalled);
    CHECK(st.message.find("tokens.txt") != std::string::npos);
    std::filesystem::remove(files.dir / "model.int8.onnx");
    files.put("tokens.txt");
    CHECK(engine.load(request, nullptr).code == ErrorCode::ModelNotInstalled);
}

TEST(sherpa_model_resolution)
{
    ModelFiles files;
    files.put("tokens.txt");
    files.put("model.onnx");
    std::string model, tokens;
    CHECK(resolveSherpaModel(files.dir, model, tokens).ok());
    CHECK(model == (files.dir / "model.onnx").u8string());
    CHECK(tokens == (files.dir / "tokens.txt").u8string());
    files.put("model.int8.onnx");
    CHECK(resolveSherpaModel(files.dir, model, tokens).ok());
    CHECK(model == (files.dir / "model.int8.onnx").u8string());
    std::filesystem::remove(files.dir / "tokens.txt");
    CHECK(resolveSherpaModel(files.dir, model, tokens).code == ErrorCode::ModelNotInstalled);
    CHECK(model.empty() && tokens.empty());
}

TEST(sherpa_missing_vad_model)
{
    ModelFiles files;
    EngineRequest req;
    req.engineId = "sherpa-onnx";
    Status st;
    CHECK(!createSherpaVad(req, &st, files.dir / "absent.onnx"));
    CHECK(st.code == ErrorCode::VadLoadFailed);
    CHECK(!createSherpaVad(req, nullptr, files.dir / "absent.onnx"));
    VadEnvironment environment(files.dir / "absent.onnx");
    CHECK(!createVad(req, &st));
    CHECK(st.code == ErrorCode::VadLoadFailed);
    req.engineId = "fake";
    CHECK(createVad(req, &st)); // fake still uses EnergyVad, even in an ON build.
}

TEST(sherpa_invalid_config)
{
    ModelFiles files;
    files.put("model.onnx");
    files.put("tokens.txt");
    EngineRequest req;
    req.modelDir = files.dir;
    req.language = "unsupported-language";
    SherpaAsrEngine engine;
    CHECK(engine.load(req, nullptr).code == ErrorCode::InvalidArgument);
    req.language = "zh";
    req.threads = 0;
    CHECK(engine.load(req, nullptr).code == ErrorCode::InvalidArgument);
    Status st;
    CHECK(!createSherpaVad(req, &st, files.dir / "model.onnx"));
    CHECK(st.code == ErrorCode::InvalidArgument);
    req.threads = 1;
    req.vad.threshold = NAN;
    CHECK(!createSherpaVad(req, &st, files.dir / "model.onnx"));
    CHECK(st.code == ErrorCode::InvalidArgument);
}

TEST(sherpa_config_mapping)
{
    for (const std::string language : {"zh", "auto", "en", "ja", "ko", "yue"})
        CHECK(sherpaLanguage(language) == language);
    CHECK(sherpaLanguage("") == "auto");
    const auto r = sherpaRecognizerConfig("model.onnx", "tokens.txt", "auto", 2);
    CHECK(std::string(r.model_config.sense_voice.model) == "model.onnx");
    CHECK(std::string(r.model_config.tokens) == "tokens.txt");
    CHECK(std::string(r.model_config.sense_voice.language) == "auto");
    CHECK(r.model_config.sense_voice.use_itn == 1);
    CHECK(std::string(r.model_config.model_type) == "sense_voice");
    CHECK(std::string(r.model_config.provider) == "cpu");
    CHECK(r.model_config.num_threads == 2);
    CHECK(r.feat_config.sample_rate == 16000 && r.feat_config.feature_dim == 80);
    CHECK(std::string(r.decoding_method) == "greedy_search");
    VadConfig v;
    v.threshold = .6f;
    v.minSpeechMs = 350;
    v.minSilenceMs = 750;
    v.maxSegmentMs = 10000;
    v.speechPadMs = 200;
    const auto c = sherpaVadConfig(v, "silero_vad.onnx", 1);
    CHECK(c.silero_vad.threshold == .6f);
    CHECK(std::abs(c.silero_vad.min_speech_duration - .35f) < 1e-6f);
    CHECK(std::abs(c.silero_vad.min_silence_duration - .75f) < 1e-6f);
    CHECK(c.silero_vad.max_speech_duration == 10.f);
    CHECK(c.silero_vad.window_size == 512);
    CHECK(c.sample_rate == 16000 && c.num_threads == 1);
    CHECK(std::string(c.provider) == "cpu");
    CHECK(std::string(c.silero_vad.model) == "silero_vad.onnx");
}

TEST(sherpa_result_timestamps)
{
    const char *tokens[] = {"你", "好"};
    float times[] = {.12f, .3f};
    SherpaOnnxOfflineRecognizerResult r{};
    r.text = "你好";
    r.lang = "zh";
    r.emotion = "NEUTRAL";
    r.event = "Speech";
    r.count = 2;
    r.tokens_arr = tokens;
    r.timestamps = times;
    FinalSegment f;
    f.job = 9;
    f.id = 4;
    f.begin16k = 32000;
    f.end16k = 48000;
    f.forcedCut = true;
    f.pass = 2;
    copySherpaResult(r, f);
    CHECK(f.text == "你好" && f.language == "zh");
    CHECK(f.emotion == "NEUTRAL" && f.event == "Speech");
    CHECK(f.words.size() == 2 && f.words[0].text == "你");
    CHECK(f.words[0].beginMs == 2120 && f.words[0].endMs == 2300);
    CHECK(f.words[1].beginMs == 2300 && f.words[1].endMs == 3000);
    CHECK(f.words[0].prob == -1.f);
    CHECK(f.job == 9 && f.id == 4 && f.forcedCut && f.pass == 2);
    CHECK(f.begin16k == 32000 && f.end16k == 48000);
    float durations[] = {.06f, 5.f};
    r.durations = durations;
    copySherpaResult(r, f);
    CHECK(f.words[0].endMs == 2180 && f.words[1].endMs == 3000);
    r.timestamps = nullptr;
    copySherpaResult(r, f);
    CHECK(f.words.empty() && f.text == "你好");
    copySherpaResult(SherpaOnnxOfflineRecognizerResult{}, f);
    CHECK(f.text.empty() && f.language.empty() && f.emotion.empty() && f.event.empty());
}

TEST(sherpa_unloaded_engine)
{
    SherpaAsrEngine engine;
    FinalSegment f;
    float sample = 0;
    CHECK(engine.recognize(&sample, 1, RecognizeKind::Final, f).code == ErrorCode::EngineNotLoaded);
    engine.requestCancel();
    CHECK(engine.recognize(&sample, 1, RecognizeKind::Partial, f).code == ErrorCode::EngineNotLoaded);
}
