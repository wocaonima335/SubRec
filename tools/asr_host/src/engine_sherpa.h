#pragma once
// Package C internal adapter; compiled only with SUBREC_ASR_WITH_SHERPA.
#include "asr_engine.h"
#include <sherpa-onnx/c-api/c-api.h>

#include <atomic>
#include <filesystem>

namespace asrhost {

// Pure configuration helpers. Returned C structs borrow their string arguments;
// keep those strings alive through the corresponding SherpaOnnxCreate* call.
// SenseVoice accepts auto/zh/en/ja/ko/yue; an empty hint means auto.
std::string sherpaLanguage(const std::string &language);
SherpaOnnxOfflineRecognizerConfig sherpaRecognizerConfig(
    const char *model, const char *tokens, const char *language, int threads);
SherpaOnnxVadModelConfig sherpaVadConfig(const subtitle::VadConfig &, const char *model,
                                        int threads);
// Prefer int8, fall back to model.onnx; never search/download outside modelDir.
subtitle::Status resolveSherpaModel(const std::filesystem::path &modelDir,
                                    std::string &model, std::string &tokens);
// Copies optional result fields before their C API owner is destroyed. Token
// times are relative seconds; SubRec words are absolute milliseconds.
void copySherpaResult(const SherpaOnnxOfflineRecognizerResult &, subtitle::FinalSegment &);

class SherpaAsrEngine final : public IAsrEngine {
public:
    subtitle::Status load(const subtitle::EngineRequest &, std::string *backendUsed) override;
    subtitle::Status recognize(const float *, size_t, RecognizeKind,
                               subtitle::FinalSegment &) override;
    void requestCancel() noexcept override;
    std::string engineId() const override { return "sherpa-onnx"; }

private:
    using Recognizer = std::unique_ptr<const SherpaOnnxOfflineRecognizer,
                                      decltype(&SherpaOnnxDestroyOfflineRecognizer)>;
    Recognizer m_recognizer{nullptr, &SherpaOnnxDestroyOfflineRecognizer};
    std::string m_modelId;
    // Only cancellation touches shared state. A generation avoids a cancelled
    // job poisoning the next open (the interface has no resetCancel method).
    std::atomic<uint64_t> m_cancelGeneration{0};
};

// D22: silero_vad.onnx lives alongside subrec-asr in the installer's asr/ dir.
// SUBREC_ASR_VAD_MODEL optionally selects an existing local file (UTF-8).
// An explicit path takes precedence; intended for embedding and isolated tests.
std::unique_ptr<IVad> createSherpaVad(const subtitle::EngineRequest &, subtitle::Status *,
                                     const std::filesystem::path &modelPath = {});

} // namespace asrhost
