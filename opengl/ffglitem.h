#ifndef FFGLITEM_H
#define FFGLITEM_H

#include <QByteArray>
#include <QMutex>
#include <QPointer>
#include <QQuickFramebufferObject>
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

    Q_INVOKABLE void setYUVData(
        const QByteArray &y, const QByteArray &u, const QByteArray &v, int w, int h);
    Q_INVOKABLE void applyPendingPreviewFrame();
    Q_INVOKABLE void setFrame(AVFrame *frame); // 与现有 FFGLRenderWidget 接口一致

signals:
    void keepRatioChanged();
    void aspectChanged();

public:
    mutable QMutex mtx;
    QByteArray yData, uData, vData;
    int width = 0, height = 0;
    quint64 previewVersion = 0;
    quint64 previewFingerprint = 0;
    bool m_keepRatio = false;
    float m_aspect = 1.60f / 9.0f;
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
    void submitFrame(const AVFrame *frame);
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
