#include "logging/frame_log.h"
#include "ffglitem.h"
#include "ffglrenderer.h"

#include <cstring>
#include <QCryptographicHash>
#include <QDateTime>
#include <QMetaObject>

namespace {
quint64 fnv1a64(const char *data, int size, int samples = 64)
{
    static constexpr quint64 kOffset = 1469598103934665603ull;
    static constexpr quint64 kPrime = 1099511628211ull;

    if (!data || size <= 0) {
        return 0;
    }

    quint64 hash = kOffset;
    const int step = std::max(1, size / samples);
    for (int index = 0; index < size; index += step) {
        hash ^= static_cast<unsigned char>(data[index]);
        hash *= kPrime;
    }
    hash ^= static_cast<unsigned char>(data[size - 1]);
    hash *= kPrime;
    return hash;
}

quint64 previewFingerprint(const QByteArray &y, const QByteArray &u, const QByteArray &v)
{
    quint64 hash = 1469598103934665603ull;
    auto mix = [&hash](quint64 value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(fnv1a64(y.constData(), y.size(), 128));
    mix(fnv1a64(u.constData(), u.size(), 64));
    mix(fnv1a64(v.constData(), v.size(), 64));
    return hash;
}

PreviewFrameData makePreviewFrame(const AVFrame *frame)
{
    PreviewFrameData preview;
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) {
        return preview;
    }

    const int w = frame->width;
    const int h = frame->height;
    if (w <= 0 || h <= 0) {
        return preview;
    }

    preview.width = w;
    preview.height = h;
    preview.y = QByteArray(w * h, Qt::Uninitialized);
    preview.u = QByteArray(w * h / 4, Qt::Uninitialized);
    preview.v = QByteArray(w * h / 4, Qt::Uninitialized);

    auto copyPlane = [](char *dst, const uint8_t *src, int width, int height, int linesize) {
        for (int row = 0; row < height; ++row) {
            memcpy(dst + row * width, src + row * linesize, width);
        }
    };

    copyPlane(preview.y.data(), frame->data[0], w, h, frame->linesize[0]);
    copyPlane(preview.u.data(), frame->data[1], w / 2, h / 2, frame->linesize[1]);
    copyPlane(preview.v.data(), frame->data[2], w / 2, h / 2, frame->linesize[2]);
    preview.fingerprint = previewFingerprint(preview.y, preview.u, preview.v);
    return preview;
}

qint64 currentDebugMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

const char *debugStageName(PreviewBridge::DebugStage stage)
{
    switch (stage) {
    case PreviewBridge::DebugStage::Submit:
        return "submit";
    case PreviewBridge::DebugStage::Apply:
        return "apply";
    case PreviewBridge::DebugStage::Synchronize:
        return "sync";
    case PreviewBridge::DebugStage::Render:
        return "render";
    }

    return "unknown";
}
}

QQuickFramebufferObject::Renderer *FFGLItem::createRenderer() const
{
    return new FFGLRenderer(const_cast<FFGLItem *>(this));
}
bool FFGLItem::keepRatio() const
{
    return m_keepRatio;
}

void FFGLItem::setYUVData(const QByteArray &y, const QByteArray &u, const QByteArray &v, int w, int h)
{
    QMutexLocker lk(&mtx);
    yData = y;
    uData = u;
    vData = v;
    width = w;
    height = h;
    update();
}

void FFGLItem::applyPendingPreviewFrame()
{
    PreviewFrameData frame;
    quint64 version = 0;
    auto &bridge = PreviewBridge::instance();
    if (!bridge.takeLatestFrame(frame, version)) {
        bridge.finishDispatch(0);
        return;
    }

    {
        QMutexLocker lk(&mtx);
        yData = std::move(frame.y);
        uData = std::move(frame.u);
        vData = std::move(frame.v);
        width = frame.width;
        height = frame.height;
        previewVersion = version;
        previewFingerprint = frame.fingerprint;
    }

    bridge.markApplied(version, frame.width, frame.height, frame.fingerprint);
    update();
    bridge.finishDispatch(version);
}

void FFGLItem::setFrame(AVFrame *frame)
{
    if (!frame)
        return;
    // 将 AVFrame 拆成 Y/U/V 三路并复制到 QByteArray
    QMutexLocker lk(&mtx);
    width = frame->width;
    height = frame->height;
    yData = QByteArray(reinterpret_cast<const char *>(frame->data[0]), width * height);
    uData = QByteArray(reinterpret_cast<const char *>(frame->data[1]), width * height / 4);
    vData = QByteArray(reinterpret_cast<const char *>(frame->data[2]), width * height / 4);
    update();
    av_frame_unref(frame);
    av_frame_free(&frame);
}

void PreviewBridge::setPreviewItem(FFGLItem *item)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_item = item;
    if (!m_item) {
        m_latestFrame = {};
        m_latestVersion = 0;
        m_dispatchPending = false;
        resetDebugLocked();
        return;
    }

    if (m_latestFrame.isValid()) {
        scheduleDispatchLocked(m_item);
    }
}

void PreviewBridge::submitFrame(const AVFrame *frame)
{
    PreviewFrameData latest = makePreviewFrame(frame);
    if (!latest.isValid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_latestFrame = std::move(latest);
    ++m_latestVersion;
    const qint64 nowMs = currentDebugMs();
    m_debug.submittedCount++;
    m_debug.latestVersion = m_latestVersion;
    m_debug.lastSubmitMs = nowMs;
    m_debug.latestWidth = m_latestFrame.width;
    m_debug.latestHeight = m_latestFrame.height;
    m_debug.latestFingerprint = m_latestFrame.fingerprint;
    maybeLogLocked(DebugStage::Submit, m_latestVersion, m_latestFrame.width, m_latestFrame.height, nowMs);

    FFGLItem *item = m_item.data();
    if (item) {
        scheduleDispatchLocked(item);
    }
}

bool PreviewBridge::takeLatestFrame(PreviewFrameData &frame, quint64 &version)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_item || !m_latestFrame.isValid()) {
        return false;
    }

    frame = std::move(m_latestFrame);
    version = m_latestVersion;
    return true;
}

void PreviewBridge::finishDispatch(quint64 appliedVersion)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FFGLItem *item = m_item.data();
    const bool needReschedule =
        item && m_latestFrame.isValid() && appliedVersion != 0 && appliedVersion < m_latestVersion;

    m_dispatchPending = false;
    if (needReschedule) {
        scheduleDispatchLocked(item);
    }
}

void PreviewBridge::markApplied(quint64 version, int width, int height, quint64 fingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const qint64 nowMs = currentDebugMs();
    m_debug.appliedCount++;
    m_debug.latestAppliedVersion = version;
    m_debug.lastApplyMs = nowMs;
    m_debug.latestWidth = width;
    m_debug.latestHeight = height;
    m_debug.latestFingerprint = fingerprint;
    maybeLogLocked(DebugStage::Apply, version, width, height, nowMs);
}

void PreviewBridge::markSynchronized(quint64 version, int width, int height, quint64 fingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const qint64 nowMs = currentDebugMs();
    m_debug.synchronizedCount++;
    m_debug.latestSynchronizedVersion = version;
    m_debug.lastSynchronizeMs = nowMs;
    m_debug.latestWidth = width;
    m_debug.latestHeight = height;
    m_debug.latestFingerprint = fingerprint;
    maybeLogLocked(DebugStage::Synchronize, version, width, height, nowMs);
}

void PreviewBridge::markRendered(quint64 version, int width, int height, quint64 fingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const qint64 nowMs = currentDebugMs();
    m_debug.renderedCount++;
    m_debug.latestRenderedVersion = version;
    m_debug.lastRenderMs = nowMs;
    m_debug.latestWidth = width;
    m_debug.latestHeight = height;
    m_debug.latestFingerprint = fingerprint;
    maybeLogLocked(DebugStage::Render, version, width, height, nowMs);
}

PreviewDebugSnapshot PreviewBridge::debugSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_debug;
}

void PreviewBridge::scheduleDispatchLocked(FFGLItem *item)
{
    if (!item || m_dispatchPending) {
        return;
    }

    m_dispatchPending = true;
    QMetaObject::invokeMethod(item, "applyPendingPreviewFrame", Qt::QueuedConnection);
}

void PreviewBridge::maybeLogLocked(DebugStage stage, quint64 version, int width, int height, qint64 nowMs)
{
    qint64 *lastLogMs = nullptr;
    quint64 stageCount = 0;

    switch (stage) {
    case DebugStage::Submit:
        lastLogMs = &m_lastSubmitLogMs;
        stageCount = m_debug.submittedCount;
        break;
    case DebugStage::Apply:
        lastLogMs = &m_lastApplyLogMs;
        stageCount = m_debug.appliedCount;
        break;
    case DebugStage::Synchronize:
        lastLogMs = &m_lastSynchronizeLogMs;
        stageCount = m_debug.synchronizedCount;
        break;
    case DebugStage::Render:
        lastLogMs = &m_lastRenderLogMs;
        stageCount = m_debug.renderedCount;
        break;
    }

    if (!lastLogMs) {
        return;
    }

    const bool forceDetailedSubmitLog =
        stage == DebugStage::Submit && version <= 60;
    const bool shouldLog =
        forceDetailedSubmitLog || stageCount <= 3 || (nowMs - *lastLogMs) >= 500;
    if (!shouldLog) {
        return;
    }
    *lastLogMs = nowMs;

    qCDebug(subrec::logging::frameLog).noquote()
        << "[PreviewDebug]"
        << "stage=" << debugStageName(stage)
        << "version=" << version
        << "size=" << QString("%1x%2").arg(width).arg(height)
        << "fingerprint=0x" << QString::number(m_debug.latestFingerprint, 16)
        << "submitAgeMs=" << (m_debug.lastSubmitMs > 0 ? nowMs - m_debug.lastSubmitMs : -1)
        << "counts={submit:" << m_debug.submittedCount
        << ",apply:" << m_debug.appliedCount
        << ",sync:" << m_debug.synchronizedCount
        << ",render:" << m_debug.renderedCount
        << "}"
        << "lastVersion={submit:" << m_debug.latestVersion
        << ",apply:" << m_debug.latestAppliedVersion
        << ",sync:" << m_debug.latestSynchronizedVersion
        << ",render:" << m_debug.latestRenderedVersion
        << "}";

    if (stage == DebugStage::Submit && m_latestFrame.isValid()
        && (version <= 60 || shouldLog)) {
        QCryptographicHash md5(QCryptographicHash::Md5);
        md5.addData(m_latestFrame.y);
        md5.addData(m_latestFrame.u);
        md5.addData(m_latestFrame.v);
        qCDebug(subrec::logging::frameLog).noquote()
            << "[PreviewInputMd5]"
            << "version=" << version
            << "md5=" << md5.result().toHex();
    }
}

void PreviewBridge::resetDebugLocked()
{
    m_debug = {};
    m_lastSubmitLogMs = 0;
    m_lastApplyLogMs = 0;
    m_lastSynchronizeLogMs = 0;
    m_lastRenderLogMs = 0;
}
