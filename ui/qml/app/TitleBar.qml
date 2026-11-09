import QtQuick

// Module U4 (ZCode). §3.1 title bar, 40 high, whole bar drags the window
// (Window.startSystemMove), double-click toggles maximize.
// FROZEN API:
//   window: the ApplicationWindow (for startSystemMove / visibility)
//   compact: hide the app name (narrow window)
//   signals minimizeClicked(), maximizeToggled(), closeClicked()
// Mini recording indicator (shown while RecorderView.capturing and Navigation.page !==
// "record") and the C2/C3-dependent tooltips (§6.6) are internal.
Rectangle {
    id: root
    property Window window
    property bool compact: false
    signal minimizeClicked()
    signal maximizeToggled()
    signal closeClicked()

    implicitHeight: Theme.titleBarHeight
    color: Theme.bgApp

    // The whole bar drags; buttons sit above and consume their own clicks.
    MouseArea {
        anchors.fill: parent
        onPressed: (mouse) => {
            if (root.window)
                root.window.startSystemMove()
            mouse.accepted = true
        }
        onDoubleClicked: root.maximizeToggled()
    }

    BrandLogo {
        x: Theme.space16
        anchors.verticalCenter: parent.verticalCenter
    }

    Item {
        visible: !root.compact
        x: 44
        width: appNameRow.implicitWidth
        height: 20
        anchors.verticalCenter: parent.verticalCenter

        Row {
            id: appNameRow
            spacing: Theme.space4

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("字录")
                font: Theme.bodyStrongFont
                color: Theme.textPrimary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("SubRec")
                font: Theme.bodyFont
                color: Theme.textTertiary
            }
        }
    }

    // §3.1 mini indicator: only while capturing and not on the record page; click -> P1.
    StatusPill {
        anchors.centerIn: parent
        visible: RecorderView.capturing && Navigation.page !== "record"
        uiState: RecorderView.uiState === "paused" ? "paused" : "running"
        text: (RecorderView.uiState === "paused" ? qsTr("已暂停 ") : qsTr("录制中 "))
              + RecorderView.elapsedText
        clickable: true
        onClicked: Navigation.open("record")
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height
        spacing: 0

        // Caption glyphs (CaptionGlyph: strokes on the device-pixel grid), not SVG icons.
        IconButton {
            objectName: "titleBarMinimize"
            windowButton: true
            captionKind: "minimize"
            anchors.verticalCenter: parent.verticalCenter
            toolTipText: ShellView.minimizeToTray ? qsTr("最小化到托盘") : qsTr("最小化")
            onClicked: root.minimizeClicked()
        }
        IconButton {
            objectName: "titleBarMaximize"
            windowButton: true
            // Single box / two boxes.
            captionKind: root.window && root.window.visibility === Window.Maximized
                         ? "restore" : "maximize"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.maximizeToggled()
        }
        IconButton {
            objectName: "titleBarClose"
            windowButton: true
            captionKind: "close"
            hoverColor: Theme.closeButtonHover
            anchors.verticalCenter: parent.verticalCenter
            toolTipText: ShellView.closeAction === "tray" ? qsTr("关闭（最小化到托盘）")
                        : ShellView.closeAction === "exit" ? qsTr("退出字录") : qsTr("关闭")
            onClicked: root.closeClicked()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.borderDefault
    }
}
