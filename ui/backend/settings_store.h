#ifndef SUBREC_UI_SETTINGS_STORE_H
#define SUBREC_UI_SETTINGS_STORE_H

#include <QSettings>
#include <QString>
#include <memory>

namespace subrec::ui {

// FROZEN. The single place where SubRec.Ui backend classes open persistent settings.
//
// Default store: QSettings("SubRec", "SubRec") (same as ShellSettings and
// capture/screenBackend). main.cpp calls setUiSettingsIniPath() once, before any backend
// object is constructed, when it needs isolation: AutoTest passes an INI inside its
// QTemporaryDir (same rule as review M2), tests pass their own file. Empty = default store.
// Keys used by SubRec.Ui (owner module in brackets, see UI_BATCH_PLAN.md):
//   ui/theme [U1], ui/afterRecord ui/windowGeometry ui/windowMaximized [U4],
//   output/dir output/fileNameTemplate [B3], hotkey/* [H3], asr/* [U8, owned by the
//   subtitle settings store once it exists; SubtitleView only writes through it].
void setUiSettingsIniPath(const QString &iniPath);
QString uiSettingsIniPath();
std::unique_ptr<QSettings> openUiSettings();

} // namespace subrec::ui

#endif // SUBREC_UI_SETTINGS_STORE_H
