#pragma once
// M0 冻结：SubtitleService（design-live-v2.md §10.3 D、§6.3、§12；上版 design-verified.md §11.3 D）。
// 接口归属：M0；实现归工作包 D。CMake 目标 subtitle_service（Qt Core）。
//
// - GUI 线程对象：只做属性镜像与消息投递，不做任何阻塞操作（§7.7 第 5 条）。
// - 实现 subtitle::RecordingAudioHook：beginRecording 创建 AudioTap + spool + Job 并投递给 Runner；
//   endRecording 只投递消息、立即返回。
// - 上版冻结的属性名（与 ui/backend/subtitle_view.h 转发一致）一个都不改；v2 只追加。
// - state 映射（§6.3）：L0/L1 → "live"，L2 → "lagging"，L3 → "live"（界面据 degradeLevel 显示「录完后生成」），
//   L4 → "failed"。
// - currentLine / currentConfirmed / currentTentative 节流 ≤ 4 Hz（§9.3）；暂停期间、L2+ 时为空。
// - 本类不被 appSubRec 链接，直到集成工作包 G（I4）。

#include "subtitle/tap/recording_hook.h"
#include "subtitle/subtitle_types.h"

#include <QObject>
#include <QString>

#include <filesystem>
#include <memory>
#include <functional>
#include <optional>

namespace subtitle {
class AudioTap;
}

class SubtitleService : public QObject, public subtitle::RecordingAudioHook {   // GUI 线程
    Q_OBJECT
    // ---- 上版冻结属性名（与 ui/backend/subtitle_view.h:39-45 一致）----
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)   // off|unavailable|idle|live|lagging|finishing|done|failed
    Q_PROPERTY(qreal progress READ progress NOTIFY changed)
    Q_PROPERTY(qreal lagSeconds READ lagSeconds NOTIFY changed)
    Q_PROPERTY(QString lastSrtPath READ lastSrtPath NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString currentJobVideoPath READ currentJobVideoPath NOTIFY changed)
    // ---- v2 追加（不改旧名）----
    Q_PROPERTY(QString currentLine READ currentLine NOTIFY currentLineChanged)   // 当前句（节流 4 Hz）= confirmed + tentative
    Q_PROPERTY(QString currentConfirmed READ currentConfirmed NOTIFY currentLineChanged)  // 实色部分
    Q_PROPERTY(QString currentTentative READ currentTentative NOTIFY currentLineChanged)  // 浅色部分
    Q_PROPERTY(int degradeLevel READ degradeLevel NOTIFY changed)                // 0..4
    Q_PROPERTY(QString degradeReason READ degradeReason NOTIFY changed)          // 中文
    Q_PROPERTY(QString refineState READ refineState NOTIFY refineChanged)        // M2："" | "queued" | "running" | "interrupted" | "done" | "failed"
    Q_PROPERTY(qreal refineProgress READ refineProgress NOTIFY refineChanged)    // M2：0..1

public:
    explicit SubtitleService(QObject *parent = nullptr);
    ~SubtitleService() override;

    // ---- RecordingAudioHook ----
    std::shared_ptr<subtitle::AudioTap> beginRecording(const std::filesystem::path &) noexcept override;
    void endRecording(bool mp4Finalized) noexcept override;

    // ---- 属性 ----
    bool available() const;
    QString state() const;
    qreal progress() const;
    qreal lagSeconds() const;
    QString lastSrtPath() const;
    QString lastError() const;
    QString currentJobVideoPath() const;
    QString currentLine() const;
    QString currentConfirmed() const;
    QString currentTentative() const;
    int degradeLevel() const;
    QString degradeReason() const;
    QString refineState() const;
    qreal refineProgress() const;

    Q_INVOKABLE void openLastSrtFolder();
    Q_INVOKABLE bool regenerate(const QString &videoPath);   // M3：需 spool 仍在
    Q_INVOKABLE void cancelCurrent();
    Q_INVOKABLE void restoreLive();                          // L3 → L2 手动恢复一次
    Q_INVOKABLE void cancelRefine();                         // M2
    void reloadSettings();
    void shutdown(int timeoutMs);

    // ---- M1 收尾追加（只追加，不改上方签名）----
    void onRecorderHealth(const subtitle::RecorderHealth &health);   // GUI 线程约 1 Hz；排队转给 Runner::onHealthSample；不健康 → 立即 L3
    using EngineRequestProvider = std::function<std::optional<subtitle::EngineRequest>(const QString &modelId)>;
    void setEngineRequestProvider(EngineRequestProvider provider);   // beginRecording 用 provider(asr/modelId)；未设置/nullopt 回退设置

signals:
    void changed();
    void currentLineChanged();
    void refineChanged();                                    // M2
    void jobProgress(QString videoPath, QString state, int percent);  // → RecordingLibrary::setSubtitleJob
};
