import QtQuick

// Module U5 (ZCode). P1 area C (Card): StatusPill (RecorderView.uiState), SubtitleBadge
// (x fixed at +116 from the card's left inner edge), timer (RecorderView.elapsedText,
// colour per §3.2.3), RecordPauseButton + RecordMainButton with captions, save area
// (OutputNaming / RecorderView.outputPath, file line = OutputNaming.previewFileName when idle,
// RecorderView.currentOutputFile while capturing), open-folder IconButton.
// Button actions: main -> ShellView.requestStart() (idle/failed) or requestStop()
// (running/paused); pause -> requestPause()/requestResume(). Tooltips append
// HotkeyController.shortcutLabel(...) when non-empty.
// FROZEN API: none.
Card {
    id: root
    padding: 0
    implicitHeight: 104

    readonly property string state: RecorderView.uiState
    readonly property bool capturing: RecorderView.capturing

    // §3.2.2 C: tooltips "开始录制（F9 / Ctrl+Alt+R）" — the global key joins F9/F10 only
    // when a global chord is bound (HotkeyController.shortcutLabel, "" when unset).
    function keyLine(windowKey: string, action: int): string {
        const label = HotkeyController.shortcutLabel(action)
        return label.length > 0 ? windowKey + " / " + label : windowKey
    }

    StatusPill {
        x: Theme.space16
        y: Theme.space16
        uiState: root.state
    }

    SubtitleBadge {
        visible: SubtitleView.serviceAttached
        x: Theme.space16 + 116
        y: Theme.space16
        uiState: SubtitleView.uiState
        mode: SubtitleView.mode
        progressPercent: Math.round(SubtitleView.progress * 100)
        lagSeconds: SubtitleView.lagSeconds
        onClicked: {
            if (SubtitleView.uiState === "done")
                SubtitleView.openLastSrtFolder()
            else
                Navigation.open("settings/subtitle")
        }
    }

    Text {
        // §3.2.3 timer colours per state.
        x: Theme.space16
        y: 44
        text: RecorderView.elapsedText
        font: Theme.timerFont
        color: root.state === "running" ? Theme.textPrimary
             : root.state === "paused" ? Theme.warningFg
             : root.state === "idle" || root.state === "preparing" ? Theme.textSecondary
             : Theme.textTertiary
    }

    RecordPauseButton {
        x: 276
        y: 0
        uiState: root.state
        toolTipText: root.state === "paused"
            ? qsTr("继续（%1）").arg(root.keyLine("F10", 1))
            : qsTr("暂停（%1）").arg(root.keyLine("F10", 1))
        onClicked: {
            if (root.state === "paused")
                ShellView.requestResume()
            else if (root.state === "running")
                ShellView.requestPause()
        }
    }
    Text {
        visible: root.state === "running" || root.state === "paused"
        x: 276 - 18
        y: 78
        width: 80
        horizontalAlignment: Text.AlignHCenter
        text: root.state === "paused" ? qsTr("继续") : qsTr("暂停")
        font: Theme.captionFont
        color: Theme.textSecondary
    }

    RecordMainButton {
        x: 344
        y: -10
        uiState: root.state
        toolTipText: root.state === "noDevice" ? qsTr("没有可用的视频来源")
            : root.capturing && root.state !== "failed"
            ? qsTr("停止并保存（%1）").arg(root.keyLine("F9", 0))
            : qsTr("开始录制（%1）").arg(root.keyLine("F9", 0))
        onClicked: {
            if (root.state === "running" || root.state === "paused")
                ShellView.requestStop()
            else
                ShellView.requestStart()
        }
    }
    Text {
        x: 324
        y: 78
        width: 104
        horizontalAlignment: Text.AlignHCenter
        text: {
            switch (root.state) {
            case "running":
            case "paused":
                return qsTr("停止")
            case "preparing":
                return qsTr("准备中…")
            case "stopping":
                return qsTr("正在保存…")
            case "failed":
                return qsTr("重新开始")
            default:
                return qsTr("开始录制")
            }
        }
        font: Theme.captionFont
        color: Theme.textSecondary
    }

    // Save area (§3.2.2 C): path + file line; the file line switches to the file being
    // written once the session starts.
    Column {
        x: parent.width - 296
        y: 16
        spacing: 4

        Text {
            text: qsTr("保存到")
            font: Theme.captionFont
            color: Theme.textTertiary
        }
        Text {
            width: 224
            text: RecorderView.outputPath
            font: Theme.monoSmallFont
            color: Theme.textSecondary
            elide: Text.ElideMiddle

            HoverHandler {
                id: pathHover
            }
            AppToolTip {
                visible: pathHover.hovered
                text: RecorderView.outputPath
            }
        }
        Text {
            width: 224
            text: {
                if (root.capturing) {
                    const file = RecorderView.currentOutputFile
                    const cut = Math.max(file.lastIndexOf("/"), file.lastIndexOf("\\"))
                    return cut >= 0 ? file.substring(cut + 1) : file
                }
                return OutputNaming.previewFileName
            }
            font: Theme.monoSmallFont
            color: root.capturing ? Theme.textPrimary : Theme.textTertiary
            elide: Text.ElideMiddle
        }
    }

    IconButton {
        anchors.right: parent.right
        anchors.rightMargin: Theme.space12
        anchors.verticalCenter: parent.verticalCenter
        iconName: "folder-open"
        toolTipText: qsTr("打开保存文件夹")
        onClicked: DesktopActions.openFolder(RecorderView.outputPath)
    }
}
