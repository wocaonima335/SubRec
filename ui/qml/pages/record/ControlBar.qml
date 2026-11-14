import QtQuick

// Module U5 (ZCode). P1 area C (Card): StatusPill (RecorderView.uiState), SubtitleBadge
// (x fixed at +116 from the card's left inner edge), timer (RecorderView.elapsedText,
// colour per §3.2.3), RecordPauseButton + RecordMainButton with captions, save area
// (OutputNaming / RecorderView.outputPath, file line = OutputNaming.previewFileName when idle,
// RecorderView.currentOutputFile while capturing), open-folder IconButton.
// Button actions: main -> ShellView.requestStart() (idle/failed) or requestStop()
// (running/paused); pause -> requestPause()/requestResume(). Tooltips append
// HotkeyController.shortcutLabel(...) when non-empty.
// UI 1114 #1: two widths. Wide (>= 720) keeps the §3.2.2 C coordinates; compact (< 720)
// moves the save area to one line under the buttons so it can never cover them.
// FROZEN API: none.
Card {
    id: root
    objectName: "controlBar"
    padding: 0
    // 720 = record button right edge 408 + 16 gap + 296 px save area (224 + 72 right).
    readonly property bool compact: width < 720
    implicitHeight: compact ? 148 : 104

    readonly property string state: RecorderView.uiState
    readonly property bool capturing: RecorderView.capturing
    // File line: the file being written while capturing, else the next file name.
    readonly property string fileName: {
        if (root.capturing) {
            const file = RecorderView.currentOutputFile
            const cut = Math.max(file.lastIndexOf("/"), file.lastIndexOf("\\"))
            return cut >= 0 ? file.substring(cut + 1) : file
        }
        return OutputNaming.previewFileName
    }
    // Folder + file name, for the compact line and its tooltip.
    readonly property string fullPath: {
        if (root.capturing && RecorderView.currentOutputFile.length > 0)
            return RecorderView.currentOutputFile
        const dir = RecorderView.outputPath
        if (dir.length === 0 || dir.endsWith("/") || dir.endsWith("\\"))
            return dir + root.fileName
        return dir + (dir.indexOf("\\") >= 0 ? "\\" : "/") + root.fileName
    }

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
        objectName: "recordPauseButton"
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
        objectName: "recordMainButton"
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
    // written once the session starts. Wide: right column, x = W - 296 (never left of 424,
    // the record caption's right edge + 16), at most 224 wide and always clear of the
    // folder button. Compact: one line "保存到 <folder/file>" at y 104 under the buttons,
    // ending 8 px before the folder button.
    Item {
        id: saveArea
        objectName: "saveArea"
        x: root.compact ? Theme.space16 : Math.max(424, root.width - 296)
        y: root.compact ? 104 : 16
        width: root.compact ? Math.max(0, root.width - Theme.space16 - Theme.space8 - 32 - Theme.space12)
                            : Math.max(0, Math.min(224, root.width - saveArea.x - 56))
        height: root.compact ? 32 : wideSave.implicitHeight

        Column {
            id: wideSave
            visible: !root.compact
            width: saveArea.width
            spacing: 4

            Text {
                text: qsTr("保存到")
                font: Theme.captionFont
                color: Theme.textTertiary
            }
            Text {
                width: wideSave.width
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
                width: wideSave.width
                text: root.fileName
                font: Theme.monoSmallFont
                color: root.capturing ? Theme.textPrimary : Theme.textTertiary
                elide: Text.ElideMiddle
            }
        }

        Row {
            visible: root.compact
            anchors.verticalCenter: saveArea.verticalCenter
            spacing: Theme.space8

            Text {
                id: compactLabel
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("保存到")
                font: Theme.captionFont
                color: Theme.textTertiary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, saveArea.width - compactLabel.width - Theme.space8)
                text: root.fullPath
                font: Theme.monoSmallFont
                color: root.capturing ? Theme.textPrimary : Theme.textSecondary
                elide: Text.ElideMiddle

                HoverHandler {
                    id: fullPathHover
                }
                AppToolTip {
                    visible: fullPathHover.hovered
                    text: root.fullPath
                }
            }
        }
    }

    IconButton {
        objectName: "openOutputFolderButton"
        anchors.right: parent.right
        anchors.rightMargin: Theme.space12
        // Compact: level with the save line; wide: centred on the card as before.
        anchors.verticalCenter: root.compact ? saveArea.verticalCenter : parent.verticalCenter
        iconName: "folder-open"
        toolTipText: qsTr("打开保存文件夹")
        onClicked: DesktopActions.openFolder(RecorderView.outputPath)
    }
}
