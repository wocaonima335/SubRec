#include "shell/tray_controller.h"
#include "shell/shell_logic.h"
#include "shell/tray_icon_factory.h"

#include <QAction>
#include <QMenu>
#include <QPainter>
#include <array>
#include <optional>

namespace subrec::shell {

class TrayController::Private {
public:
    QMenu menu;
    QSystemTrayIcon icon;
    ShellSnapshot snapshot;
    std::optional<NotifyId> lastNotification;
    std::array<QIcon, 5> icons;
    QAction *toggle = nullptr, *status = nullptr, *startStop = nullptr, *pause = nullptr;

    void refresh()
    {
        const auto model = buildTrayMenuModel(snapshot);
        toggle->setText(model.toggleWindowText);
        status->setText(model.statusText);
        startStop->setText(model.startStopText);
        startStop->setEnabled(model.startStopEnabled);
        pause->setText(model.pauseText);
        pause->setEnabled(model.pauseEnabled);
        // An 8px dot in the standard 16px icon slot. Keep its color when disabled.
        const char *color = "#22C55E";
        switch (snapshot.state) {
        case RecState::NoDevice: color = "#8A909C"; break;
        case RecState::Preparing:
        case RecState::Stopping: color = "#38BDF8"; break;
        case RecState::Running:
        case RecState::Failed: color = "#EF4444"; break;
        case RecState::Paused: color = "#F59E0B"; break;
        default: break;
        }
        QPixmap dot(16, 16);
        dot.fill(Qt::transparent);
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(color));
        painter.drawEllipse(QRectF(4, 4, 8, 8));
        painter.end();
        QIcon statusIcon(dot);
        statusIcon.addPixmap(dot, QIcon::Disabled);
        status->setIcon(statusIcon);
        icon.setIcon(icons.at(static_cast<size_t>(trayIconFor(snapshot.state))));
        const auto tooltip = trayToolTip(snapshot);
        if (icon.toolTip() != tooltip) icon.setToolTip(tooltip);
    }
};

TrayController::TrayController(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    for (size_t i = 0; i < d->icons.size(); ++i)
        d->icons[i] = makeTrayIcon(static_cast<TrayIconKind>(i));
    d->menu.setMinimumWidth(240);
    // Palette roles follow the application palette, including later theme changes.
    d->menu.setStyleSheet(QStringLiteral(
        "QMenu { background: palette(window); color: palette(window-text);"
        " border: 1px solid palette(mid); border-radius: 8px; padding: 4px 0px; }"
        "QMenu::item { min-height: 24px; padding: 4px 12px; margin: 0px 4px; border-radius: 4px; }"
        "QMenu::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
        "QMenu::item:disabled { color: palette(mid); }"
        "QMenu::separator { height: 1px; background: palette(mid); margin: 4px 8px; }"));
    d->toggle = d->menu.addAction(QString(), this, &TrayController::toggleWindowRequested);
    auto font = d->toggle->font();
    font.setBold(true);
    d->toggle->setFont(font);
    d->menu.addSeparator();
    d->status = d->menu.addAction(QString());
    d->status->setEnabled(false);
    d->startStop = d->menu.addAction(QString(), this, &TrayController::startStopRequested);
    d->pause = d->menu.addAction(QString(), this, &TrayController::pauseResumeRequested);
    d->menu.addSeparator();
    d->menu.addAction(QStringLiteral("打开保存文件夹"), this, &TrayController::openOutputFolderRequested);
    d->menu.addAction(QStringLiteral("设置…"), this, &TrayController::openSettingsRequested);
    d->menu.addSeparator();
    d->menu.addAction(QStringLiteral("退出字录"), this, &TrayController::exitRequested);
    d->icon.setContextMenu(&d->menu);
    connect(&d->menu, &QMenu::aboutToShow, this, [this] { d->refresh(); });
    connect(&d->icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) emit toggleWindowRequested();
    });
    connect(&d->icon, &QSystemTrayIcon::messageClicked, this, [this] {
        if (d->lastNotification) emit notificationClicked(*d->lastNotification);
    });
    d->refresh();
}

TrayController::~TrayController() = default;
bool TrayController::systemTrayAvailable() { return QSystemTrayIcon::isSystemTrayAvailable(); }
void TrayController::setEnabled(bool enabled) { d->icon.setVisible(enabled); }
void TrayController::update(const ShellSnapshot &snapshot) { d->snapshot = snapshot; d->refresh(); }
QMenu *TrayController::menu() const { return &d->menu; }

void TrayController::notify(NotifyId id, const QString &title, const QString &body,
                            QSystemTrayIcon::MessageIcon icon)
{
    if (!d->icon.isVisible() || !QSystemTrayIcon::supportsMessages()) return;
    d->lastNotification = id;
    d->icon.showMessage(title, body, icon, 10000);
}

// TODO(H4): menu item 6 and shortcut texts.
void TrayController::setFloatingBarItem(bool, bool) {}
void TrayController::setShortcutTexts(const QString &, const QString &, const QString &) {}

} // namespace subrec::shell
