#include "shell/shell_settings.h"
#include "test_support.h"
#include <QCoreApplication>
using namespace subrec::shell;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        IsolatedSettings isolated;
        const QString mode = QString::fromLocal8Bit(argv[1]);
        if (mode == "defaults") {
            ShellSettings s(true);
            CHECK(s.trayEnabled()); CHECK(!s.minimizeToTray()); CHECK(!s.startMinimized());
            CHECK(s.closeActionText() == "ask" && s.closeAction() == CloseAction::Ask);
            CHECK(s.excludeText() == "recording" && s.excludeMode() == ExcludeMode::RecordingOnly);
            CHECK(!s.hideOnRecordStart()); CHECK(s.confirmExit()); CHECK(!s.trayHintShown()); CHECK(s.excludeSupported());
            QSettings raw(QSettings::IniFormat, QSettings::UserScope, "SubRec", "SubRec");
            CHECK(raw.allKeys().isEmpty()); // Reading defaults need not write preferences.
        } else if (mode == "roundtrip") {
            {
                ShellSettings s(true);
                s.setTrayEnabled(false); s.setMinimizeToTray(true); s.setStartMinimized(true);
                s.setCloseActionText("tray"); s.setExcludeText("always"); s.setHideOnRecordStart(true);
                s.setConfirmExit(false); s.setTrayHintShown(true);
            }
            ShellSettings s(true);
            CHECK(!s.trayEnabled() && s.minimizeToTray() && s.startMinimized());
            CHECK(s.closeAction() == CloseAction::Tray && s.excludeMode() == ExcludeMode::Always);
            CHECK(s.hideOnRecordStart() && !s.confirmExit() && s.trayHintShown());
            QSettings raw(QSettings::IniFormat, QSettings::UserScope, "SubRec", "SubRec");
            CHECK(raw.fileName().startsWith(isolated.directory.path()));
            CHECK(raw.value("ui/trayEnabled").toBool() == false);
            CHECK(raw.value("ui/minimizeToTray").toBool()); CHECK(raw.value("ui/startMinimized").toBool());
            CHECK(raw.value("ui/closeAction") == "tray"); CHECK(raw.value("ui/excludeFromCapture") == "always");
            CHECK(raw.value("ui/hideOnRecordStart").toBool()); CHECK(!raw.value("ui/confirmExitWhileRecording").toBool());
            CHECK(raw.value("ui/trayHintShown").toBool());
            s.setCloseActionText("exit"); CHECK(s.closeAction() == CloseAction::Exit);
            s.setExcludeText("never"); CHECK(s.excludeMode() == ExcludeMode::Never);
        } else if (mode == "signals") {
            ShellSettings s(true); int changes = 0;
            QObject::connect(&s, &ShellSettings::changed, [&] { ++changes; });
            struct BoolSetting { bool (ShellSettings::*get)() const; void (ShellSettings::*set)(bool); };
            const BoolSetting rows[] = {
                {&ShellSettings::trayEnabled, &ShellSettings::setTrayEnabled},
                {&ShellSettings::minimizeToTray, &ShellSettings::setMinimizeToTray},
                {&ShellSettings::startMinimized, &ShellSettings::setStartMinimized},
                {&ShellSettings::hideOnRecordStart, &ShellSettings::setHideOnRecordStart},
                {&ShellSettings::confirmExit, &ShellSettings::setConfirmExit},
                {&ShellSettings::trayHintShown, &ShellSettings::setTrayHintShown}
            };
            for (const auto &row : rows) {
                const auto before = changes; const bool initial = (s.*row.get)();
                (s.*row.set)(initial); CHECK(changes == before);
                (s.*row.set)(!initial); CHECK(changes == before + 1);
                (s.*row.set)(!initial); CHECK(changes == before + 1);
                (s.*row.set)(initial); CHECK(changes == before + 2);
            }
            const auto before = changes;
            s.setCloseActionText("ask"); s.setExcludeText("recording"); CHECK(changes == before);
            s.setCloseActionText("exit"); s.setExcludeText("always"); CHECK(changes == before + 2);
            s.setCloseActionText("exit"); s.setExcludeText("always"); CHECK(changes == before + 2);
        } else if (mode == "invalid") {
            QSettings raw(QSettings::IniFormat, QSettings::UserScope, "SubRec", "SubRec");
            raw.setValue("ui/closeAction", "bogus"); raw.setValue("ui/excludeFromCapture", "bogus"); raw.sync();
            ShellSettings s(true); int changes = 0;
            QObject::connect(&s, &ShellSettings::changed, [&] { ++changes; });
            CHECK(s.closeAction() == CloseAction::Ask && s.excludeMode() == ExcludeMode::RecordingOnly);
            s.setCloseActionText("INVALID"); s.setExcludeText(""); CHECK(changes == 0);
            s.setCloseActionText("tray"); s.setExcludeText("never"); CHECK(changes == 2);
            s.setCloseActionText("Tray"); s.setExcludeText("Always"); CHECK(changes == 2);
            CHECK(s.closeAction() == CloseAction::Tray && s.excludeMode() == ExcludeMode::Never);
        } else if (mode == "unsupported") {
            ShellSettings s(false); CHECK(!s.excludeSupported());
            for (auto value : {"recording", "always", "never"}) {
                s.setExcludeText(QString::fromLatin1(value));
                CHECK(s.excludeText() == QLatin1String(value)); CHECK(s.excludeMode() == ExcludeMode::Never);
            }
            s.setExcludeText("always");
            ShellSettings supported(true); CHECK(supported.excludeMode() == ExcludeMode::Always);
        } else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
