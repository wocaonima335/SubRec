# Owned by Codex (UI_BATCH_PLAN.md §3). Only Codex edits this list.
# Paths are relative to ui/. Every QML basename must be unique across both lists
# (files are flattened into one module directory).

set(UI_CODEX_QML
    # U6 library
    qml/pages/library/LibraryPage.qml
    qml/pages/library/RecordingTableHeader.qml
    qml/pages/library/RecordingRow.qml
    qml/pages/library/RecordingContextMenu.qml
    qml/dialogs/DeleteRecordingDialog.qml
    # U6/U8 dialog bus and host
    qml/dialogs/DialogRequests.qml
    qml/dialogs/ContentDialogs.qml
    # U7 settings
    qml/pages/settings/SettingsPage.qml
    qml/pages/settings/GeneralSettings.qml
    qml/pages/settings/OutputSettings.qml
    qml/pages/settings/DeviceSettings.qml
    qml/pages/settings/AdvancedSettings.qml
    qml/pages/settings/AboutSettings.qml
    # U8 subtitles
    qml/pages/settings/SubtitleSettings.qml
    qml/pages/settings/ModelRow.qml
    qml/dialogs/ModelDownloadPromptDialog.qml
    qml/dialogs/ModelLicenseDialog.qml
    qml/dialogs/DeleteModelDialog.qml
    # H3 hotkeys
    qml/pages/settings/HotkeySettings.qml
    qml/components/KeySequenceField.qml
    qml/components/KeyCap.qml
)

set(UI_CODEX_SINGLETONS
    qml/dialogs/DialogRequests.qml
)

set(UI_CODEX_SOURCES
    # B1–B4
    backend/recorder_view.h backend/recorder_view.cpp
    backend/shell_view.h backend/shell_view.cpp
    backend/output_naming.h backend/output_naming.cpp
    backend/output_naming_logic.h backend/output_naming_logic.cpp
    backend/audio_level_monitor.h backend/audio_level_monitor.cpp
    # U6
    backend/recording_library.h backend/recording_library.cpp
    backend/desktop_actions.h backend/desktop_actions.cpp
    # U7
    backend/app_info.h backend/app_info.cpp
    # U8
    backend/subtitle_view.h backend/subtitle_view.cpp
    backend/model_catalog.h backend/model_catalog.cpp
    # H3
    backend/hotkey_controller.h backend/hotkey_controller.cpp
)

set(UI_CODEX_RESOURCES
)
