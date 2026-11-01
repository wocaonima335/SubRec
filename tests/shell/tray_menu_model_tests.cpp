#include "shell/shell_logic.h"
#include "shell/tray_icon_factory.h"
#include "shell/tray_controller.h"
#include "test_support.h"
#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QImage>
using namespace subrec::shell;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        const QString mode = QString::fromLocal8Bit(argv[1]);
        ShellSnapshot s;
        s.timeText = "00:12:34";
        if (mode == "model") {
            struct Row { RecState state; const char *status; bool stop, enabled, resume, pause; };
            const Row rows[] = {
                {RecState::Idle, "就绪", false, true, false, false},
                {RecState::NoDevice, "没有可用的视频来源", false, false, false, false},
                {RecState::Preparing, "准备中…", false, false, false, false},
                {RecState::Running, "录制中 00:12:34", true, true, false, true},
                {RecState::Paused, "已暂停 00:12:34", true, true, true, true},
                {RecState::Stopping, "正在保存…", true, false, false, false},
                {RecState::Failed, "出错：测试错误", false, true, false, false}
            };
            s.captureError = QStringLiteral("测试错误");
            for (const auto &row : rows) for (auto w : {WindowVisibility::Shown, WindowVisibility::Minimized, WindowVisibility::HiddenToTray}) {
                s.state = row.state; s.window = w;
                const auto m = buildTrayMenuModel(s);
                CHECK(m.statusText == QString::fromUtf8(row.status));
                CHECK(m.toggleWindowText == (w == WindowVisibility::Shown ? QStringLiteral("隐藏主窗口") : QStringLiteral("显示主窗口")));
                CHECK(m.startStopText == (row.stop ? QStringLiteral("停止录制") : QStringLiteral("开始录制")));
                CHECK(m.startStopEnabled == row.enabled);
                CHECK(m.pauseText == (row.resume ? QStringLiteral("继续录制") : QStringLiteral("暂停录制")));
                CHECK(m.pauseEnabled == row.pause);
                CHECK(m.statusDot == trayIconFor(s.state));
            }
            s.captureError = QString(100, QChar(0x9519));
            CHECK(buildTrayMenuModel(s).statusText == QStringLiteral("出错：") + s.captureError.left(24));
        } else if (mode == "tooltip") {
            const RecState states[] = {RecState::Idle, RecState::NoDevice, RecState::Preparing, RecState::Running,
                RecState::Paused, RecState::Stopping, RecState::Failed};
            const char *texts[] = {"就绪", "就绪", "准备中…", "录制中 00:12:34", "已暂停 00:12:34", "正在保存…", "录制出错：错误"};
            s.captureError = QStringLiteral("错误");
            for (int i = 0; i < 7; ++i) {
                s.state = states[i]; s.subtitleFinishing = false;
                const auto base = QStringLiteral("字录 SubRec — ") + QString::fromUtf8(texts[i]);
                CHECK(trayToolTip(s) == base);
                s.subtitleFinishing = true; s.subtitleProgress = 63;
                CHECK(trayToolTip(s) == base + (isCapturing(s.state) ? QString() : QStringLiteral(" · 字幕生成中 63%")));
            }
            s.subtitleFinishing = false; s.captureError = QString(100, QChar(0x9519));
            CHECK(trayToolTip(s) == QStringLiteral("字录 SubRec — 录制出错：") + s.captureError.left(40));
            CHECK(trayToolTip(s).size() < 127);
        } else if (mode == "icons") {
            for (auto kind : {TrayIconKind::Idle, TrayIconKind::Transition, TrayIconKind::Recording, TrayIconKind::Paused, TrayIconKind::Error}) {
                const auto icon = makeTrayIcon(kind);
                CHECK(icon.availableSizes().size() == 6);
                for (int size : {16, 20, 24, 32, 40, 48}) {
                    CHECK(icon.availableSizes().contains(QSize(size, size)));
                    const auto pixels = icon.pixmap(size, size).toImage();
                    CHECK(pixels.pixelColor(0, 0).alpha() == 0);
                    // Sample the block's top row at the center: inside the rounded rect but
                    // above the glyph, whose width depends on the installed font.
                    const int inset = size <= 20 ? 1 : size <= 40 ? 2 : 3;
                    const auto color = pixels.pixelColor(size / 2, inset);
                    CHECK(color == QColor(kind == TrayIconKind::Recording ? "#EF4444" : kind == TrayIconKind::Paused ? "#F59E0B" : "#5B6475"));
                }
            }
        } else if (mode == "tray") {
            TrayController tray;
            int starts = 0, pauses = 0, toggles = 0;
            QObject::connect(&tray, &TrayController::startStopRequested, [&] { ++starts; });
            QObject::connect(&tray, &TrayController::pauseResumeRequested, [&] { ++pauses; });
            QObject::connect(&tray, &TrayController::toggleWindowRequested, [&] { ++toggles; });
            const auto actions = tray.menu()->actions();
            CHECK(actions.size() == 10); // Seven items, three separators; no floating bar/hotkeys.
            CHECK(actions[0]->font().bold()); CHECK(!actions[2]->isEnabled());
            tray.update(s); CHECK(actions[3]->isEnabled()); CHECK(!actions[4]->isEnabled());
            actions[0]->trigger(); actions[3]->trigger();
            s.state = RecState::Paused; tray.update(s);
            CHECK(actions[4]->text() == QStringLiteral("继续录制")); actions[4]->trigger();
            s.state = RecState::Stopping; tray.update(s);
            CHECK(!actions[3]->isEnabled()); CHECK(!actions[4]->isEnabled());
            CHECK(starts == 1 && pauses == 1 && toggles == 1);
            QMetaObject::invokeMethod(tray.menu(), "aboutToShow", Qt::DirectConnection);
            CHECK(actions[2]->text() == QStringLiteral("正在保存…"));
        } else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
