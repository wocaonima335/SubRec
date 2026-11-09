#ifndef FFGLITEM_H
#define FFGLITEM_H

#include <QByteArray>
#include <QMutex>
#include <QPointer>
#include <QQuickFramebufferObject>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
}

struct PreviewFrameData
{
    QByteArray y;
    QByteArray u;
    QByteArray v;
    int width = 0;
    int height = 0;
    quint64 fingerprint = 0;

    bool isValid() const
    {
        return width > 0 && height > 0 && !y.isEmpty();
    }
};

struct PreviewDebugSnapshot
{
    quint64 submittedCount = 0;
    quint64 appliedCount = 0;
    quint64 synchronizedCount = 0;
    quint64 renderedCount = 0;
    // Frames skipped because the main window was not shown (setPreviewWanted(false)).
    // An intentional skip, never counted as a drop; cumulative, not reset by
    // setPreviewItem(nullptr).
    quint64 skippedHiddenCount = 0;

    quint64 latestVersion = 0;
    quint64 latestAppliedVersion = 0;
    quint64 latestSynchronizedVersion = 0;
    quint64 latestRenderedVersion = 0;

    qint64 lastSubmitMs = 0;
    qint64 lastApplyMs = 0;
    qint64 lastSynchronizeMs = 0;
    qint64 lastRenderMs = 0;

    int latestWidth = 0;
    int latestHeight = 0;
    quint64 latestFingerprint = 0;
};

class FFGLItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(bool keepRatio READ keepRatio WRITE setKeepRatio NOTIFY keepRatioChanged)
    Q_PROPERTY(float aspect READ aspect WRITE setAspect NOTIFY aspectChanged)
    // Size of the last frame handed to the item (0 until the first frame). GUI thread.
    // aspect follows it (width / height) so QML can size the preview to the source.
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY frameSizeChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY frameSizeChanged)
public:
    FFGLItem() = default;
    Renderer *createRenderer() const override;

    bool keepRatio() const;
    void setKeepRatio(bool v)
    {
        if (m_keepRatio != v) {
            m_keepRatio = v;
            emit keepRatioChanged();
            update();
        }
    }
    float aspect() const { return m_aspect; }
    void setAspect(float v)
    {
        if (m_aspect != v) {
            m_aspect = v;
            emit aspectChanged();
            update();
        }
    }

    int frameWidth() const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }

    Q_INVOKABLE void setYUVData(
        const QByteArray &y, const QByteArray &u, const QByteArray &v, int w, int h);
    Q_INVOKABLE void applyPendingPreviewFrame();
    Q_INVOKABLE void setFrame(AVFrame *frame); // 与现有 FFGLRenderWidget 接口一致

signals:
    void keepRatioChanged();
    void aspectChanged();
    void frameSizeChanged();

protected:
    // Moving the window to a screen with another device pixel ratio changes the FBO size
    // QQuickFramebufferObject needs; request a repaint so it is recreated at once.
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    // Publishes frameWidth/frameHeight/aspect; safe to call from any thread.
    void noteFrameSize(int w, int h);

public:
    mutable QMutex mtx;
    QByteArray yData, uData, vData;
    int width = 0, height = 0;
    quint64 previewVersion = 0;
    quint64 previewFingerprint = 0;
    bool m_keepRatio = false;
    float m_aspect = 16.0f / 9.0f; // was 1.60f / 9.0f (a typo for 16:9, never read)
    int m_frameWidth = 0;
    int m_frameHeight = 0;
};

class PreviewBridge : public QObject
{
public:
    enum class DebugStage
    {
        Submit,
        Apply,
        Synchronize,
        Render
    };

    static PreviewBridge &instance()
    {
        static PreviewBridge b;
        return b;
    }
    void setPreviewItem(FFGLItem *item);
    FFGLItem *get() const { return m_item; }
    // false counts a discarded input or an overwritten pending frame. Never waits on UI.
    bool submitFrame(const AVFrame *frame);
    // S0b contract (MINIMIZE_DESIGN.md §4.7, §8 alignment). Set by WindowController on
    // the GUI thread: true only when the main window is shown (W1); false when
    // minimized (W2) or hidden to tray (W3). submitFrame() checks it first with a
    // relaxed atomic load; when false it does no copy, takes no lock, increments
    // skippedHiddenCount and returns TRUE, so callers (GpuPreview, the encoder thread)
    // do not count the frame as dropped. Default true: behavior is unchanged until
    // something calls this. The next frame after it becomes true is previewed again.
    // Callable from any thread.
    void setPreviewWanted(bool wanted) noexcept;
    bool previewWanted() const noexcept;
    bool takeLatestFrame(PreviewFrameData &frame, quint64 &version);
    void finishDispatch(quint64 appliedVersion);
    void markApplied(quint64 version, int width, int height, quint64 fingerprint);
    void markSynchronized(quint64 version, int width, int height, quint64 fingerprint);
    void markRendered(quint64 version, int width, int height, quint64 fingerprint);
    PreviewDebugSnapshot debugSnapshot() const;

private:
    void scheduleDispatchLocked(FFGLItem *item);
    void maybeLogLocked(DebugStage stage, quint64 version, int width, int height, qint64 nowMs);
    void resetDebugLocked();

private:
    mutable std::mutex m_mutex;
    std::atomic<bool> m_previewWanted{true};
    std::atomic<quint64> m_skippedHidden{0};
    QPointer<FFGLItem> m_item;
    PreviewFrameData m_latestFrame;
    quint64 m_latestVersion = 0;
    bool m_dispatchPending = false;
    PreviewDebugSnapshot m_debug;
    qint64 m_lastSubmitLogMs = 0;
    qint64 m_lastApplyLogMs = 0;
    qint64 m_lastSynchronizeLogMs = 0;
    qint64 m_lastRenderLogMs = 0;
};

#endif // FFGLITEM_H
