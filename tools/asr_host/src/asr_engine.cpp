// 引擎 / VAD 工厂（工作包 C）。
//
// createEngine：M1 第一阶段只有 fake（须 SUBREC_ASR_TEST_HOOKS=1，否则 EngineUnknown，§8）。
// sherpa-onnx / whisper.cpp 是已知引擎但尚未接入（S0 / M1 sherpa 阶段），返回 NotSupported。
//
// createVad：能量阈值 VAD（fake 引擎与测试配套）。帧长 512 样本（32 ms，与 Silero 一致），
// 帧边界对齐自 0 起的绝对 512 网格（C2 问题 3：重启/拼接后网格不随恢复点重排）；
// RMS 超过 threshold 判为语音帧，连续 minSpeechMs 开段、段后连续 minSilenceMs 静音端点，
// speechPadMs 向两侧外扩（sherpa C API 没有 pad 参数，外扩本来就要 SubRec 自己做，§3.5）。
// 它只在解码线程使用；resume 断点经 reset(from16k) 从静音初始化（§7.4），
// 恢复点后的首帧不足 512 样本、只用于对齐，段起点钳在 resumeFrom16k 之后。

#include "asr_engine.h"
#include "engine_fake.h"
#if defined(SUBREC_ASR_HAS_SHERPA)
#include "engine_sherpa.h"
#endif

#include "subtitle/subtitle_error.h"
#include "subtitle/subtitle_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace asrhost {

namespace {

constexpr size_t kVadFrameSamples = 512;   // 32 ms @16 kHz

class EnergyVad final : public IVad {
public:
    explicit EnergyVad(const subtitle::VadConfig &cfg)
        : m_threshold(cfg.threshold),
          m_minSpeechFrames(std::max<int>(1, cfg.minSpeechMs * subtitle::kAsrSamplesPerMs
                                               / static_cast<int>(kVadFrameSamples))),
          m_minSilenceFrames(std::max<int>(1, cfg.minSilenceMs * subtitle::kAsrSamplesPerMs
                                              / static_cast<int>(kVadFrameSamples))),
          m_pad16k(std::max<int64_t>(0, static_cast<int64_t>(cfg.speechPadMs)
                                           * subtitle::kAsrSamplesPerMs))
    {
    }

    void feed(const float *pcm16k, size_t n, int64_t first16k,
              std::vector<std::pair<int64_t, int64_t>> &closed) override
    {
        if (first16k != m_nextFrameStart) {
            // 输入应严格连续（解码线程顺序喂入）；容错重置。
            m_nextFrameStart = first16k;
            m_frame.clear();
            realignFrame();
        }
        for (size_t i = 0; i < n; ++i)
            m_frame.push_back(pcm16k[i]);
        while (m_frame.size() >= frameLen()) {
            const size_t len = frameLen();
            const bool speech = frameIsSpeech(m_frame.data(), len);
            const int64_t frameStart = m_nextFrameStart;
            m_frame.erase(m_frame.begin(), m_frame.begin() + len);
            m_nextFrameStart = m_frameEnd;
            m_frameEnd += static_cast<int64_t>(kVadFrameSamples);
            advanceFrame(speech, frameStart, closed);
        }
    }

    void forceCut(int64_t at16k) override
    {
        // splice / 外部强切：丢弃当前语音状态与未成帧的残留样本（其后音频重新判段，段状态机在
        // at16k 封口），帧网格重新对齐绝对 512 网格（拼接点未必落在网格上，见 realignFrame）。
        if (m_inSpeech) {
            m_inSpeech = false;
            m_runFrames = 0;
            m_runStart = -1;
        }
        m_nextFrameStart = std::max(m_nextFrameStart, at16k);
        m_frame.clear();
        realignFrame();
        m_lastEmitEnd = std::max(m_lastEmitEnd, at16k);
    }

    void finish(std::vector<std::pair<int64_t, int64_t>> &closed) override
    {
        if (!m_inSpeech)
            return;
        // 封口穿过已喂入的全部样本：不足一帧的尾部样本也计入最后一段（C2 问题 2，
        // 「finish 生效前所有已提交样本处理完」）；录制结束是自然端点，forcedCut 由段状态机置位。
        emitClosed(m_speechBegin, m_nextFrameStart + static_cast<int64_t>(m_frame.size()), closed);
        m_inSpeech = false;
        m_runFrames = 0;
        m_runStart = -1;
    }

    void reset(int64_t from16k) override
    {
        m_inSpeech = false;
        m_runFrames = 0;
        m_runStart = -1;
        m_minBegin = from16k;
        m_lastEmitEnd = from16k;
        m_nextFrameStart = from16k;
        m_speechBegin = -1;
        m_frame.clear();
        realignFrame();
    }

    bool inSpeech() const override { return m_inSpeech; }
    int64_t openSegmentBegin16k() const override { return m_inSpeech ? m_speechBegin : -1; }

private:
    bool frameIsSpeech(const float *p, size_t n)
    {
        double acc = 0;
        for (size_t i = 0; i < n; ++i)
            acc += static_cast<double>(p[i]) * p[i];
        const double rms = std::sqrt(acc / static_cast<double>(n));
        return rms >= static_cast<double>(m_threshold);
    }

    // 帧边界对齐绝对 512 网格（C2 问题 3）：恢复点/拼接点未必落在网格上，其后首个帧
    // 不足 512 样本、只用于对齐——有/无重启时后续帧是同一套边界，padding 下同一段的
    // 端点不随重启漂移；段起点仍被 minBegin / lastEmitEnd 钳在恢复点之后（不早于 resumeFrom16k）。
    void realignFrame()
    {
        const int64_t r = m_nextFrameStart % static_cast<int64_t>(kVadFrameSamples);
        m_frameEnd = m_nextFrameStart + (r ? static_cast<int64_t>(kVadFrameSamples) - r
                                           : static_cast<int64_t>(kVadFrameSamples));
    }
    size_t frameLen() const
    {
        return static_cast<size_t>(std::max<int64_t>(1, m_frameEnd - m_nextFrameStart));
    }

    void advanceFrame(bool speech, int64_t frameStart,
                      std::vector<std::pair<int64_t, int64_t>> &closed)
    {
        if (!m_inSpeech) {
            if (!speech) {
                m_runFrames = 0;
                m_runStart = -1;
                return;
            }
            if (m_runFrames == 0)
                m_runStart = frameStart;
            ++m_runFrames;
            if (m_runFrames >= m_minSpeechFrames) {
                m_inSpeech = true;
                m_speechBegin = std::max(m_runStart - m_pad16k, std::max(m_minBegin, m_lastEmitEnd));
                m_silenceFrames = 0;
                m_silenceStart = -1;
            }
        } else if (speech) {
            m_silenceFrames = 0;
            m_silenceStart = -1;
        } else {
            if (m_silenceFrames == 0)
                m_silenceStart = frameStart;
            ++m_silenceFrames;
            if (m_silenceFrames >= m_minSilenceFrames) {
                // 端点：静音起点 + speechPad（不越过已处理帧的末尾；此时 m_nextFrameStart 已指向下一帧）。
                emitClosed(m_speechBegin, std::min(m_silenceStart + m_pad16k, m_nextFrameStart), closed);
                m_inSpeech = false;
                m_runFrames = 0;
                m_runStart = -1;
            }
        }
    }

    void emitClosed(int64_t b, int64_t e,
                    std::vector<std::pair<int64_t, int64_t>> &closed)
    {
        b = std::max(b, m_lastEmitEnd);
        if (e <= b)
            return;
        closed.emplace_back(b, e);
        m_lastEmitEnd = e;
    }

    float m_threshold = 0.5f;      // VadConfig.threshold：帧 RMS 判语音的阈值
    int m_minSpeechFrames = 8;
    int m_minSilenceFrames = 16;
    int64_t m_pad16k = 2400;

    bool m_inSpeech = false;
    int64_t m_speechBegin = -1;
    int64_t m_runStart = -1;
    int m_runFrames = 0;
    int m_silenceFrames = 0;
    int64_t m_silenceStart = -1;
    int64_t m_minBegin = 0;
    int64_t m_lastEmitEnd = 0;
    int64_t m_nextFrameStart = 0;   // 正在聚集的帧起点（绝对位置）
    int64_t m_frameEnd = 512;       // 该帧的绝对结束边界（对齐 512 网格；恢复/拼接后首帧可不足 512）
    std::vector<float> m_frame;
};

} // namespace

std::unique_ptr<IAsrEngine> createEngine(const std::string &engineId, const std::string &fakeOptions,
                                         subtitle::Status *err)
{
    if (engineId == "fake") {
        if (!fakeEngineAllowed()) {
            if (err)
                *err = subtitle::Status::error(subtitle::ErrorCode::EngineUnknown,
                                               "engine fake requires SUBREC_ASR_TEST_HOOKS=1");
            return nullptr;
        }
        FakeEngineOptions o;
        if (!parseFakeOptions(fakeOptions, o, err))
            return nullptr;
        return std::make_unique<FakeAsrEngine>(o);
    }
#if defined(SUBREC_ASR_HAS_SHERPA)
    if (engineId == "sherpa-onnx") {
        if (err)
            *err = {};
        return std::make_unique<SherpaAsrEngine>();
    }
#endif
    if (engineId == "sherpa-onnx" || engineId == "whisper.cpp") {
        if (err)
            *err = subtitle::Status::error(subtitle::ErrorCode::NotSupported,
                                           "engine not built in this phase: " + engineId);
        return nullptr;
    }
    if (err)
        *err = subtitle::Status::error(subtitle::ErrorCode::EngineUnknown, "engine " + engineId);
    return nullptr;
}

std::unique_ptr<IVad> createVad(const subtitle::EngineRequest &req, subtitle::Status *err)
{
#if defined(SUBREC_ASR_HAS_SHERPA)
    if (req.engineId == "sherpa-onnx")
        return createSherpaVad(req, err);
#endif
    (void)err;
    return std::make_unique<EnergyVad>(req.vad);
}

} // namespace asrhost
