import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.7 RecordPause, 44×44 circle; visible only in running/paused
// (150 ms fade, keeps its layout slot so the main button never moves).
// FROZEN API: uiState (RecorderView.uiState), toolTipText; signal clicked() -> owner calls
// ShellView.requestPause() / requestResume().
AbstractButton {
    id: root
    property string uiState: "idle"
    property string toolTipText: ""
    readonly property bool paused: uiState === "paused"

    implicitWidth: 44
    implicitHeight: 44
    hoverEnabled: true
    // Keeps the layout slot: opacity 0 instead of visible false (§3.2.2 C).
    opacity: uiState === "running" || uiState === "paused" ? 1 : 0
    enabled: opacity > 0

    Behavior on opacity {
        enabled: !Theme.reduceMotion
        NumberAnimation { duration: Theme.durationToggle }
    }

    background: Rectangle {
        radius: width / 2
        color: root.paused ? Theme.warningBg
             : root.pressed ? Theme.bgPressed
             : root.hovered ? Theme.bgHover : Theme.bgSurface2
        border.width: 1
        border.color: root.paused ? Theme.warningFg : Theme.borderDefault
    }

    contentItem: Item {
        // Running: two 4×16 bars, 4 px apart. Paused: 16 px play triangle in warning/fg.
        Item {
            visible: !root.paused
            anchors.centerIn: parent
            width: 12
            height: 16

            Rectangle {
                x: 0; y: 0
                width: 4; height: 16
                radius: 1
                color: Theme.textPrimary
            }
            Rectangle {
                x: 8; y: 0
                width: 4; height: 16
                radius: 1
                color: Theme.textPrimary
            }
        }

        Canvas {
            visible: root.paused
            anchors.centerIn: parent
            width: 16
            height: 16
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = Theme.warningFg
                ctx.beginPath()
                ctx.moveTo(3, 1)
                ctx.lineTo(15, 8)
                ctx.lineTo(3, 15)
                ctx.closePath()
                ctx.fill()
            }
        }
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: hover.hovered && root.toolTipText.length > 0
        text: root.toolTipText
    }
}
