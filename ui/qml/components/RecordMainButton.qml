import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.7 RecordMain, 64×64 circle.
// FROZEN API:
//   uiState: RecorderView.uiState. idle/failed -> white dot (start); running/paused -> white
//            rounded square (stop); preparing/stopping -> spinner, not clickable; noDevice ->
//            disabled 40 %.
//   toolTipText: owner supplies, e.g. "开始录制（F9 / Ctrl+Alt+R）"
//   signal clicked(): owner calls ShellView.requestStart() or requestStop() by state.
AbstractButton {
    id: root
    property string uiState: "idle"
    property string toolTipText: ""

    readonly property bool __busy: uiState === "preparing" || uiState === "stopping"

    implicitWidth: 64
    implicitHeight: 64
    hoverEnabled: true
    enabled: uiState !== "noDevice" && !__busy

    // §2.5.7: 40 % when not recordable (no device), 60 % background while busy.
    opacity: uiState === "noDevice" ? 0.4 : 1.0

    background: Rectangle {
        radius: width / 2
        color: root.pressed ? Theme.recordPressed : root.hovered ? Theme.recordHover : Theme.recordDefault
        opacity: root.__busy ? 0.6 : 1.0
    }

    contentItem: Item {
        // idle/failed: 22×22 white solid circle (start). running/paused: 20×20 rounded
        // square r4 (stop). busy: 24 px spinner. noDevice keeps the start glyph.
        Rectangle {
            anchors.centerIn: parent
            visible: !root.__busy
            width: root.uiState === "running" || root.uiState === "paused" ? 20 : 22
            height: width
            radius: root.uiState === "running" || root.uiState === "paused" ? 4 : width / 2
            color: "#FFFFFF"

            Behavior on width {
                enabled: !Theme.reduceMotion
                NumberAnimation { duration: Theme.durationToggle; easing.type: Easing.OutCubic }
            }
            Behavior on radius {
                enabled: !Theme.reduceMotion
                NumberAnimation { duration: Theme.durationToggle; easing.type: Easing.OutCubic }
            }
        }

        Spinner {
            anchors.centerIn: parent
            visible: root.__busy
            size: 24
            color: "#FFFFFF"
            running: root.__busy
        }
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: hover.hovered && root.toolTipText.length > 0
        text: root.toolTipText
    }

    // §2.5.7: keyboard focus ring 3 px outside.
    Rectangle {
        visible: root.activeFocus && root.focusReason === Qt.TabFocusReason
        anchors.fill: parent
        anchors.margins: -3
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: Theme.focusRing
        z: -1
    }
}
