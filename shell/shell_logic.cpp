#include "shell/shell_logic.h"

namespace subrec::shell {

RecState recStateFrom(const QString &state, bool hasVideoDevice)
{
    if (state == QLatin1String("Idle")) return hasVideoDevice ? RecState::Idle : RecState::NoDevice;
    if (state == QLatin1String("Preparing")) return RecState::Preparing;
    if (state == QLatin1String("Running")) return RecState::Running;
    if (state == QLatin1String("Paused")) return RecState::Paused;
    if (state == QLatin1String("Stopping")) return RecState::Stopping;
    // Unknown engine states must not look ready to start.
    return RecState::Failed;
}

bool isCapturing(RecState state)
{
    return state == RecState::Preparing || state == RecState::Running
        || state == RecState::Paused || state == RecState::Stopping;
}

CloseOutcome decideClose(bool trayEnabled, CloseAction action)
{
    if (!trayEnabled || action == CloseAction::Exit) return CloseOutcome::Exit;
    return action == CloseAction::Tray ? CloseOutcome::HideToTray : CloseOutcome::AskUser;
}

bool hideOnMinimize(bool trayEnabled, bool minimizeToTray)
{
    return trayEnabled && minimizeToTray;
}

bool exitNeedsConfirm(RecState state, bool subtitleFinishing, bool confirmSetting)
{
    return confirmSetting && (isCapturing(state) || subtitleFinishing);
}

int exitConfirmVariant(RecState state, bool subtitleOn)
{
    if (state == RecState::Stopping) return 3; // D1d: saving
    if (isCapturing(state)) return subtitleOn ? 1 : 0; // D1b / D1a
    return subtitleOn ? 2 : 0; // D1c: subtitles only
}

bool notificationAllowed(WindowVisibility window, RecState state)
{
    return window != WindowVisibility::Shown && !isCapturing(state);
}

bool shouldExclude(ExcludeMode mode, bool capturing, bool supported)
{
    return supported && (mode == ExcludeMode::Always
        || (mode == ExcludeMode::RecordingOnly && capturing));
}

TrayIconKind trayIconFor(RecState state)
{
    switch (state) {
    case RecState::Preparing:
    case RecState::Stopping: return TrayIconKind::Transition;
    case RecState::Running: return TrayIconKind::Recording;
    case RecState::Paused: return TrayIconKind::Paused;
    case RecState::Failed: return TrayIconKind::Error;
    default: return TrayIconKind::Idle;
    }
}

namespace {
QString statusText(const ShellSnapshot &s, bool tooltip)
{
    switch (s.state) {
    case RecState::Idle: return QStringLiteral("就绪");
    case RecState::NoDevice: return tooltip ? QStringLiteral("就绪") : QStringLiteral("没有可用的视频来源");
    case RecState::Preparing: return QStringLiteral("准备中…");
    case RecState::Running: return QStringLiteral("录制中 %1").arg(s.timeText);
    case RecState::Paused: return QStringLiteral("已暂停 %1").arg(s.timeText);
    case RecState::Stopping: return QStringLiteral("正在保存…");
    case RecState::Failed:
        return (tooltip ? QStringLiteral("录制出错：") : QStringLiteral("出错："))
            + s.captureError.left(tooltip ? 40 : 24);
    }
    return {};
}
}

QString trayToolTip(const ShellSnapshot &s)
{
    QString text = QStringLiteral("字录 SubRec — ") + statusText(s, true);
    if (s.subtitleFinishing && !isCapturing(s.state))
        text += QStringLiteral(" · 字幕生成中 %1%").arg(qBound(0, s.subtitleProgress, 100));
    return text;
}

TrayMenuModel buildTrayMenuModel(const ShellSnapshot &s)
{
    TrayMenuModel m;
    m.toggleWindowText = s.window == WindowVisibility::Shown
        ? QStringLiteral("隐藏主窗口") : QStringLiteral("显示主窗口");
    m.statusText = statusText(s, false);
    m.statusDot = trayIconFor(s.state);
    const bool canStop = s.state == RecState::Running || s.state == RecState::Paused;
    m.startStopText = canStop || s.state == RecState::Stopping
        ? QStringLiteral("停止录制") : QStringLiteral("开始录制");
    m.startStopEnabled = canStop || s.state == RecState::Idle || s.state == RecState::Failed;
    m.pauseText = s.state == RecState::Paused ? QStringLiteral("继续录制") : QStringLiteral("暂停录制");
    m.pauseEnabled = canStop;
    return m;
}

} // namespace subrec::shell
