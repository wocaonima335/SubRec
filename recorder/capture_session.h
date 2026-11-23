#ifndef CAPTURE_SESSION_H
#define CAPTURE_SESSION_H

#include "capture/capture_types.h"

#include <atomic>
#include <functional>
#include <thread>
#if defined(SUBREC_HAS_SUBTITLE)
#include <memory>
#include <utility>
namespace subtitle { class AudioTap; }
#endif

class CaptureLifecycleTest;

class FFRecorder;

class CaptureSession
{
public:
    explicit CaptureSession(FFRecorder &recorder);
    ~CaptureSession();
    CaptureSession(const CaptureSession &) = delete;
    CaptureSession &operator=(const CaptureSession &) = delete;

#if defined(SUBREC_HAS_SUBTITLE)
    void setAudioTap(std::shared_ptr<subtitle::AudioTap> tap) { m_audioTap = std::move(tap); }
    bool mp4Finalized() const { return !m_startFailed && m_subtitleMuxerFinalized; }
#endif
    bool create();
    bool start(const CaptureConfig &config,
               std::function<void(const CaptureStartResult &)> onStarted);
    void requestStop();
    void join();
    bool release(); // False: cancellation timed out; keep this session owned.

    bool isCreated() const;
    bool isStarted() const;
    const CaptureConfig &config() const;

    // Lifecycle-harness seam: when non-null, replaces FFRecorder::startRecord
    // and the real FFmpeg opener. Production code never sets this.
    std::function<void(CaptureSession &,
                       std::function<void(const CaptureStartResult &)>)>
        testOpenerOverride;

private:
    bool cleanup(); // Only opener, or controller after join. Never concurrent.
    void abortPipeline(); // Wake every edge before waiting on any worker.
    friend class CaptureLifecycleTest;
    bool closeVideoSource();
    bool closeAudioSource();
    bool finalizeMuxer();

private:
#if defined(SUBREC_HAS_SUBTITLE)
    std::shared_ptr<subtitle::AudioTap> m_audioTap;
    bool m_subtitleMuxerFinalized = false;
#endif
    FFRecorder &m_recorder;
    CaptureConfig m_config;
    bool m_created = false;
    bool m_videoClosed = false, m_audioClosed = false, m_muxerClosed = false;
    bool m_startFailed = false;
    bool m_cleanupNeeded = false; // Published by start, read after opener join.
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_starting{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_openThread;
};

#endif // CAPTURE_SESSION_H
