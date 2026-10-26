#ifndef FFOPENSOURCEEVENT_H
#define FFOPENSOURCEEVENT_H

#include "ffevent.h"
#include <functional>
#include <optional>
#include <string>

class FFOpenSourceEvent : public FFEvent
{
public:
    // 构造函数：同时打开视频和音频源
    FFOpenSourceEvent(FFRecorder *recorderCtx,
                      enum FFRecordContextType::demuxerType videoSourceType_,
                      std::string const &videoUrl_,
                      enum FFRecordContextType::demuxerType audioSourceType_,
                      std::string const &audioUrl_,
                      std::string const &format_);

    FFOpenSourceEvent(FFRecorder *recorderCtx,
                      const InputRequest &videoInput,
                      const InputRequest &audioInput);
    virtual void work() override;
    CaptureStartResult openAndStartResult(const std::function<bool()> &shouldStart = {});
    bool openAndStart(const std::function<bool()> &shouldStart = {});
    virtual bool requiresSerialization() const override { return true; }
    // DirectShow screen request used when the ddagrab screen cannot be opened.
    void setMixMicrophone(const std::optional<InputRequest> &input) { microphoneMixInput = input; }
    void setAudioFallback(const std::optional<InputRequest> &fallback) { audioFallback = fallback; }
    void setVideoFallback(const std::optional<InputRequest> &fallback) { videoFallback = fallback; }

private:
    bool initVideo(const std::function<bool()> &shouldStart);
    bool initAudio(const std::function<bool()> &shouldStart);
    bool checkContinue(const InputRequest &input, const std::function<bool()> &shouldStart);
    bool fail(const InputRequest &input, CaptureErrorStage stage, int code, const QString &message);
    CaptureStartResult startupResult;
    InputRequest videoInput;
    InputRequest audioInput;
    bool preparedScreen = false;
    // Screen comes from FFScreenGrabThread (ddagrab) instead of demuxer + decoder.
    bool usingScreenGrab = false;
    std::optional<InputRequest> videoFallback;
    std::optional<InputRequest> audioFallback;
    bool usingWasapi = false;
    std::optional<InputRequest> microphoneMixInput;
    bool openScreenGrab(const std::function<bool()> &shouldStart);
    void switchToVideoFallback();
    void startVideo();
    void startAudio();

    std::string videoUrl;
    std::string audioUrl;
    std::string videoFormat;
    std::string audioFormat;

    int videoSourceType;
    int audioSourceType;
    int videoIndex;
    int audioIndex;
};

#endif // FFOPENSOURCEEVENT_H
