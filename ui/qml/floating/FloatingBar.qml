pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

// Module FB2 (ZCode). Mini floating bar, 264×40 capsule in its own top-level window
// (MINIMIZE_DESIGN §3.6, §4.5, §6.3). The window grows to 136 high while the FB-H hint shows.
// FROZEN contract (see FloatingBarView): created once by AppWindow with visible:false;
// Component.onCompleted calls FloatingBarView.attachWindow(root) BEFORE any show;
// `visible` follows FloatingBarView.shown (position first, then show); position from
// FloatingBarView.preferredPosition; drag via startSystemMove, release ->
// FloatingBarView.savePosition. Buttons: pause/resume -> FloatingBarView.pauseResume();
// stop -> stop(); open main window -> openMainWindow(); FB-6 close -> dismiss(). Tooltips
// use HotkeyController.shortcutLabel(...). 60 % opacity 3 s after the mouse leaves (FB-D).
Window {
    id: root

    readonly property string state: FloatingBarView.barState

    width: 264
    height: FloatingBarView.hintVisible ? 136 : 40
    visible: false
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    title: qsTr("字录 悬浮条")

    Component.onCompleted: FloatingBarView.attachWindow(root)

    // Position FIRST, then flip visibility (the exclusion was registered at attach time).
    Connections {
        target: FloatingBarView
        function onShownChanged() {
            if (FloatingBarView.shown) {
                const pos = FloatingBarView.preferredPosition(root.width, root.height)
                root.x = pos.x
                root.y = pos.y
                root.visible = true
            } else {
                root.visible = false
            }
        }
    }

    function keyLine(action: int): string {
        return HotkeyController.shortcutLabel(action)
    }

    function stopToolTip(): string {
        if (root.state === "failed")
            return FloatingBarView.captureError.length > 0 ? FloatingBarView.captureError
                                                           : qsTr("关闭悬浮条")
        const key = root.keyLine(0)
        return key.length > 0 ? qsTr("停止并保存（F9 / %1）").arg(key)
                              : qsTr("停止并保存（F9）")
    }

    // ---- the 264×40 capsule ----
    Rectangle {
        id: capsule
        width: 264
        height: 40
        radius: 20
        color: Theme.bgSurface
        opacity: 0.96
        border.width: 1
        border.color: Theme.borderDefault

        // §6.3: empty space drags the bar; release remembers the position.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            onPressed: (mouse) => {
                root.startSystemMove()
                mouse.accepted = true
            }
        }

        // Drag handle: 2×3 dot grid.
        Repeater {
            model: 6
            Rectangle {
                required property int index
                x: 10 + (index % 2) * 4
                y: 12 + Math.floor(index / 2) * 6
                width: 2
                height: 2
                radius: 1
                color: Theme.textTertiary
            }
        }

        // Status indicator (§6.3): FB-1/4 spinner, FB-2 blinking dot, FB-3 steady dot,
        // FB-5 check, FB-6 alert.
        Loader {
            id: indicator
            x: 22
            y: 14
            sourceComponent: {
                if (root.state === "preparing" || root.state === "stopping")
                    return spinnerComp
                if (root.state === "saved")
                    return savedComp
                if (root.state === "failed")
                    return failedComp
                return dotComp
            }

            Component {
                id: dotComp
                Item {
                    width: 12
                    height: 12
                    Rectangle {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: root.state === "paused" ? Theme.warningFg : Theme.recordDefault
                        // FB-2: 1 s blink while recording; steady when reduce-motion.
                        SequentialAnimation on opacity {
                            running: root.state === "running" && !Theme.reduceMotion
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.3; duration: Theme.blinkPeriod / 2; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.3; to: 1.0; duration: Theme.blinkPeriod / 2; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }
            Component {
                id: spinnerComp
                Spinner {
                    size: 12
                    color: Theme.infoFg
                    running: true
                }
            }
            Component {
                id: savedComp
                AppIcon {
                    name: "circle-check"
                    size: 12
                    color: Theme.successFg
                }
            }
            Component {
                id: failedComp
                AppIcon {
                    name: "circle-alert"
                    size: 12
                    color: Theme.dangerFg
                }
            }
        }

        // Timer / status text (§6.3): mono SemiBold timer, regular status text.
        Text {
            x: 40
            y: 10
            width: 80
            height: 20
            verticalAlignment: Text.AlignVCenter
            text: root.state === "stopping" ? qsTr("保存中…")
                 : root.state === "saved" ? qsTr("已保存")
                 : root.state === "failed" ? qsTr("录制出错")
                 : FloatingBarView.elapsedText
            font.family: Theme.monoFamily
            font.pixelSize: 14
            font.weight: root.state === "running" || root.state === "paused"
                          || root.state === "stopping" ? Font.DemiBold : Font.Normal
            color: root.state === "saved" ? Theme.successFg
                 : root.state === "failed" ? Theme.dangerFg
                 : root.state === "paused" ? Theme.warningFg
                 : root.state === "running" ? Theme.textPrimary
                 : Theme.textSecondary
        }

        // Subtitle mini icon: live -> info, lagging -> warning; hidden otherwise.
        AppIcon {
            x: 124
            y: 12
            visible: FloatingBarView.subtitleIndicator.length > 0
            name: "captions"
            size: 16
            color: FloatingBarView.subtitleIndicator === "lagging" ? Theme.warningFg
                 : Theme.infoFg
        }

        Rectangle {
            x: 148
            y: Theme.space8
            width: 1
            height: 24
            color: Theme.borderDefault
        }

        // Pause/resume (FB-1/4 disabled, FB-2 pause, FB-3 resume; hidden in FB-5/6).
        Item {
            x: 156
            y: 4
            width: 32
            height: 32
            visible: root.state !== "saved" && root.state !== "failed"
            opacity: (root.state === "preparing" || root.state === "stopping") ? 0.4 : 1.0

            Rectangle {
                anchors.fill: parent
                radius: 16
                color: pauseMa.pressed ? Theme.bgPressed
                     : pauseMa.containsMouse ? (root.state === "paused" ? Theme.warningBg : Theme.bgHover)
                     : root.state === "paused" ? Theme.warningBg : "transparent"
            }

            AppIcon {
                anchors.centerIn: parent
                name: root.state === "paused" ? "play" : "pause"
                size: 16
                color: root.state === "paused" ? Theme.warningFg : Theme.textPrimary
            }

            MouseArea {
                id: pauseMa
                anchors.fill: parent
                hoverEnabled: root.state === "running" || root.state === "paused"
                enabled: root.state === "running" || root.state === "paused"
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: FloatingBarView.pauseResume()
            }

            AppToolTip {
                visible: pauseMa.containsMouse && pauseMa.enabled
                text: {
                    const key = root.keyLine(1)
                    return key.length > 0 ? qsTr("暂停/继续（F10 / %1）").arg(key)
                                          : qsTr("暂停/继续（F10）")
                }
            }
        }

        // Stop (FB-1/2/3/4) or close (FB-6); hidden in FB-5.
        Item {
            x: 192
            y: 4
            width: 32
            height: 32
            visible: root.state !== "saved"

            Rectangle {
                anchors.fill: parent
                radius: 16
                color: stopMa.pressed ? Theme.bgPressed
                     : stopMa.containsMouse ? (root.state === "failed" ? Theme.bgHover : Theme.dangerBg)
                     : "transparent"
            }

            // Stop glyph: 12×12 rounded solid square in record/default; FB-6 shows an x.
            Rectangle {
                visible: root.state !== "failed"
                anchors.centerIn: parent
                width: 12
                height: 12
                radius: 2
                color: Theme.recordDefault
            }
            AppIcon {
                visible: root.state === "failed"
                anchors.centerIn: parent
                name: "x"
                size: 16
                color: Theme.textSecondary
            }

            MouseArea {
                id: stopMa
                anchors.fill: parent
                hoverEnabled: root.state !== "preparing" && root.state !== "stopping"
                enabled: root.state !== "preparing" && root.state !== "stopping"
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (root.state === "failed")
                        FloatingBarView.dismiss()
                    else
                        FloatingBarView.stop()
                }
            }

            AppToolTip {
                visible: stopMa.containsMouse && stopMa.enabled
                text: root.stopToolTip()
            }
        }

        // Open main window.
        Item {
            x: 228
            y: 4
            width: 32
            height: 32

            Rectangle {
                anchors.fill: parent
                radius: 16
                color: openMa.pressed ? Theme.bgPressed : openMa.containsMouse ? Theme.bgHover : "transparent"
            }
            AppIcon {
                anchors.centerIn: parent
                name: "app-window"
                size: 16
                color: Theme.textSecondary
            }
            MouseArea {
                id: openMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: FloatingBarView.openMainWindow()
            }
            AppToolTip {
                visible: openMa.containsMouse
                text: qsTr("打开主窗口")
            }
        }

        // ---- FB-D: 60 % opacity 3 s after the mouse leaves, hover restores ----
        HoverHandler {
            id: capsuleHover
        }
        Timer {
            id: fadeTimer
            interval: 3000
            running: root.visible && !capsuleHover.hovered
            onTriggered: capsule.opacity = 0.6
        }
        Connections {
            target: capsuleHover
            function onHoveredChanged() {
                if (capsuleHover.hovered)
                    capsule.opacity = 0.96
            }
        }
        Behavior on opacity {
            NumberAnimation { duration: Theme.durationToggle }
        }
    }

    // ---- FB-H first-hide hint bubble (window is 136 high while shown) ----
    Rectangle {
        id: hintBubble
        visible: FloatingBarView.hintVisible
        y: 48
        width: 264
        height: 88
        radius: Theme.radiusMd
        color: Theme.tooltipBg

        // Pointer triangle aimed at the open-main-window button.
        Canvas {
            x: 232
            y: -6
            width: 12
            height: 6
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = Theme.tooltipBg
                ctx.beginPath()
                ctx.moveTo(0, 6)
                ctx.lineTo(6, 0)
                ctx.lineTo(12, 6)
                ctx.closePath()
                ctx.fill()
            }
            Component.onCompleted: requestPaint()
        }

        Text {
            x: Theme.space12
            y: Theme.space12
            width: 240
            height: 32
            text: qsTr("字录在后台继续录制。点右边的按钮打开主窗口；托盘图标在任务栏右下角。")
            font: Theme.captionFont
            color: "#FFFFFF"
            wrapMode: Text.WordWrap
        }

        AppButton {
            x: 196
            y: 52
            width: 56
            height: 24
            variant: "ghost"
            small: true
            text: qsTr("知道了")
            onClicked: FloatingBarView.acknowledgeHint()
        }
    }
}
