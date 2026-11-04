import QtQuick
import QtQuick.Layouts

// Module U7 (Codex). P3 设置 frame (UI_REDESIGN §3.4): title, sub-navigation Card (7 dense
// NavItems, selection = Navigation.settingsPage, click -> Navigation.open("settings/<id>")),
// form Card (padding 24, vertical scroll). While RecorderView.capturing (or
// SubtitleView.uiState === "finishing" on the subtitle page) an info InlineAlert is inserted
// at the top and 🔒 controls go to the locked state. Settings apply immediately; no Save.
// Sub-page ids: general output devices subtitle hotkeys advanced about.
// FROZEN API: none.
Item {
    id: root

    // TODO(U7): title, sub-nav, form card with Flickable, recording alert.
    StackLayout {
        anchors.fill: parent
        anchors.margins: Theme.pagePadding
        currentIndex: ["general", "output", "devices", "subtitle", "hotkeys", "advanced", "about"]
                      .indexOf(Navigation.settingsPage)

        GeneralSettings {}
        OutputSettings {}
        DeviceSettings {}
        SubtitleSettings {}
        HotkeySettings {}
        AdvancedSettings {}
        AboutSettings {}
    }
}
