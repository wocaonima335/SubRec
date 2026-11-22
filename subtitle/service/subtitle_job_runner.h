#pragma once
// M0 冻结：SubtitleJobRunner（design-live-v2.md §10.3 D、§9.1、§9.3；上版 design-verified.md §11.3 D）。
// 接口归属：M0；实现归工作包 D。CMake 目标 subtitle_service（Qt Core）。
//
// - 活在 "subtitle" QThread；SubtitleService 通过 Queued 调用槽，Runner 通过 Queued 信号回报。
// - 内部组合：AsrHostClient（经 AsrHostClientFactory）+ HostWatchdog + DegradeController + SubtitleMerger
//   （+ LocalAgreement + SentenceFixer）+ 1 Hz 定时器；读 PcmSpool 的 committed16k / splice16k 原子量，
//   节流 200 ms 发 append/splice。
// - 多 Job FIFO：新录制开始时上一 Job 若仍在追赶则暂停并记 resume16k（§6.4 最后一条）。
// - 不阻塞 GUI 线程；shutdown 中的等待发生在本线程。

#include "subtitle/host/subtitle_metatypes.h"
#include "subtitle/subtitle_types.h"

#include <QObject>
#include <QString>

class AsrHostClientFactory;

class SubtitleJobRunner : public QObject { Q_OBJECT              // "subtitle" QThread
public:
    // factory 为空时使用 DefaultAsrHostClientFactory（<appDir>/asr/subrec-asr.exe）；测试注入假宿主。
    explicit SubtitleJobRunner(AsrHostClientFactory *factory = nullptr, QObject *parent = nullptr);
    ~SubtitleJobRunner() override;

public slots:
    // ---- 上版签名 ----
    void submit(quint64 jobId, QString mp4, QString spool, subtitle::EngineRequest, subtitle::CueConfig, bool live);
    void notifyCommitted(quint64 jobId, qint64 committed16k);
    void notifySplice(quint64 jobId, qint64 at16k);
    void finishJob(quint64 jobId, qint64 total16k, bool writeSrt);
    void cancelJob(quint64 jobId);  void shutdown(int timeoutMs);
    // ---- v2 新增 ----
    void onHealthSample(subtitle::RecorderHealth);                // 1 Hz，由 Service 从 FFRecorder 拉取后转发
    void onPauseSplice(subtitle::JobId);                          // 冗余通道（IngestWorker 已覆盖，测试用）
    void setLiveConfig(subtitle::LiveConfig);                     // M0 补齐：submit 没有 LiveConfig 参数
    void restoreLive(subtitle::JobId);                            // M0 补齐：Service::restoreLive 转发（L3 → L2）

signals:
    // ---- 上版 ----
    void stateChanged(quint64 jobId, int jobState, double progress, double lagSeconds);
    void jobFinished(quint64 jobId, QString srtPath);  void jobFailed(quint64 jobId, int code, QString message);
    // ---- v2 新增 ----
    void currentLineChanged(subtitle::JobId, QString line);
    void currentSplitChanged(subtitle::JobId, QString confirmed, QString tentative);   // M0 补齐：实色/浅色分开
    void levelChanged(subtitle::JobId, int level, QString reason);
    void hostRestarting(int attempt, int delayMs);
};
