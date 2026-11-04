# Owned by ZCode (UI_BATCH_PLAN.md §3). Only ZCode edits this list.
# Paths are relative to ui/. Every QML basename must be unique across both lists
# (files are flattened into one module directory).

set(UI_ZCODE_QML
    # U1 theme
    qml/theme/Theme.qml
    qml/components/AppIcon.qml
    # U2 components
    qml/components/Spinner.qml
    qml/components/AppButton.qml
    qml/components/IconButton.qml
    qml/components/AppTextField.qml
    qml/components/AppComboBox.qml
    qml/components/AppSwitch.qml
    qml/components/SegmentedControl.qml
    qml/components/AppCheckBox.qml
    qml/components/AppRadioButton.qml
    qml/components/AppProgressBar.qml
    qml/components/AppToolTip.qml
    qml/components/Card.qml
    qml/components/NavItem.qml
    qml/components/InlineAlert.qml
    qml/components/StatusPill.qml
    qml/components/SubtitleBadge.qml
    qml/components/LevelMeter.qml
    qml/components/RecordMainButton.qml
    qml/components/RecordPauseButton.qml
    qml/components/EmptyState.qml
    qml/components/FormGroupTitle.qml
    qml/components/FormRow.qml
    qml/components/AppDialog.qml
    qml/components/Toasts.qml
    qml/components/ToastHost.qml
    qml/components/Toast.qml
    # U4 shell
    qml/app/AppWindow.qml
    qml/app/Navigation.qml
    qml/app/TitleBar.qml
    qml/app/NavRail.qml
    qml/app/WindowResizeFrame.qml
    qml/app/BrandLogo.qml
    qml/dialogs/ShellDialogs.qml
    qml/dialogs/CloseWindowDialog.qml
    qml/dialogs/QuitConfirmDialog.qml
    # U5 record page
    qml/pages/record/RecordPage.qml
    qml/pages/record/PreviewPanel.qml
    qml/pages/record/PreviewSurface.qml
    qml/pages/record/ControlBar.qml
    qml/pages/record/SourcePanel.qml
    qml/pages/record/RecentRecordings.qml
    qml/pages/record/RecordingCard.qml
    # FB2 floating bar
    qml/floating/FloatingBar.qml
)

set(UI_ZCODE_SINGLETONS
    qml/theme/Theme.qml
    qml/components/Toasts.qml
    qml/app/Navigation.qml
)

set(UI_ZCODE_SOURCES
    # U1
    backend/theme_controller.h backend/theme_controller.cpp
    # U4
    backend/app_preferences.h backend/app_preferences.cpp
    # FB2
    backend/floating_bar_view.h backend/floating_bar_view.cpp
)

# U1: Lucide SVGs go here (ui/icons/<name>.svg), resource path :/qt/qml/SubRec/Ui/icons/.
set(UI_ZCODE_RESOURCES
)
