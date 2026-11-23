import QtQuick

// Module U5 (ZCode). P1 area C (Card): StatusPill (RecorderView.uiState), SubtitleBadge
// (x fixed at +116 from the card's left inner edge), timer (RecorderView.elapsedText,
// colour per §3.2.3), RecordPauseButton + RecordMainButton with captions, save area
// (OutputNaming / RecorderView.outputPath, file line = OutputNaming.previewFileName when idle,
// RecorderView.currentOutputFile while capturing), open-folder IconButton.
// Button actions: main -> ShellView.requestStart() (idle/failed) or requestStop()
// (running/paused); pause -> requestPause()/requestResume(). Tooltips append
// HotkeyController.shortcutLabel(...) when non-empty.
// UI 1114 #1: two widths. Wide (>= 720) keeps the §3.2.2 C save column; compact (< 720)
// moves the save area to one line under the buttons so it can never cover them.
// UI 1114 #3: the record buttons follow the card width (centred, clamped between the timer
// and the save column / right edge) and sit inside the card's top edge; below the width
// where timer and buttons fit side by side (only under the 960 window minimum, which PC3
// can still reach) timer, buttons and save line stack and the timer shrinks to the card.
// FROZEN API: none.
Card {
    id: root
    objectName: "controlBar"
    padding: 0
    // 720 = record button right edge 408 + 16 gap + 296 px save area (224 + 72 right).
    readonly property bool compact: width < 720
    // Side-by-side row needs 16 + timer + 16 + buttons + 16; measured from content (not
    // from the laid-out items) so the switch cannot loop through the layout.
    readonly property real timerNaturalWidth: Math.ceil(timerMetrics.advanceWidth)
    readonly property bool stacked: compact
        && width < Theme.space16 * 3 + timerNaturalWidth + pauseSlot.width + Theme.space24 + mainSlot.width
    // Stacked: badge wraps under the pill when it no longer fits beside it.
    readonly property bool badgeWraps: stacked && badge.visible
        && Theme.space16 + 116 + badge.width > width - Theme.space16
    // Wide 104; compact/stacked end 12 px under the 32 px save line.
    implicitHeight: compact ? saveArea.y + 32 + Theme.space12 : 104

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
        id: badge
        visible: SubtitleView.serviceAttached
        x: root.badgeWraps ? Theme.space16 : Theme.space16 + 116
        y: root.badgeWraps ? Theme.space16 + 24 + Theme.space8 : Theme.space16
        uiState: SubtitleView.uiState
        mode: SubtitleView.mode
        progressPercent: Math.round(SubtitleView.progress * 100)
        lagSeconds: SubtitleView.lagSeconds
        degradeLevel: SubtitleView.degradeLevel
        degradeReason: SubtitleView.degradeReason
        onClicked: {
            if (SubtitleView.uiState === "done")
                SubtitleView.openLastSrtFolder()
            else
                Navigation.open("settings/subtitle")
        }
    }

    // F (design-live-v2 §12.2): the current sentence, one line under the status/timer
    // block — confirmed part solid, tentative tail light, newest words kept when it
    // overflows (elide left). Hidden while paused, at L2+ (§6.2), via the
    // asr/showControlBarLine switch, and in the stacked card (no free band left). The
    // 104/148 card heights and every UI 1114 anchor stay untouched.
    Item {
        id: subtitleLineRow
        objectName: "subtitleLineRow"
        visible: root.state === "running" && !root.stacked
            && SubtitleView.showControlBarLine && SubtitleView.degradeLevel < 2
            && SubtitleView.currentConfirmed.length + SubtitleView.currentTentative.length > 0
        x: Theme.space16
        y: Math.min(timer.y + timer.height + Theme.space4,
                    root.compact ? saveArea.y - height - Theme.space4
                                 : root.height - height - Theme.space8)
        width: Math.max(0, Math.min(buttonRow.x - Theme.space16, root.width - Theme.space16)
                        - Theme.space16)
        height: 16

        Text {
            id: lineConfirmed
            objectName: "subtitleLineConfirmed"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(implicitWidth,
                                        subtitleLineRow.width - lineTentative.implicitWidth))
            text: SubtitleView.currentConfirmed
            font: Theme.captionFont
            color: Theme.textPrimary
            elide: Text.ElideLeft
        }
        Text {
            id: lineTentative
            objectName: "subtitleLineTentative"
            anchors.left: lineConfirmed.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, subtitleLineRow.width)
            text: SubtitleView.currentTentative
            font: Theme.captionFont
            color: Theme.textTertiary
            elide: Text.ElideLeft
        }
    }

    TextMetrics {
        id: timerMetrics
        font: Theme.timerFont
        text: RecorderView.elapsedText
    }
    Text {
        id: timer
        objectName: "recordTimer"
        // §3.2.3 timer colours per state. Natural size unless the card is narrower than
        // the text; then it scales down to the card's inner width.
        x: Theme.space16
        y: root.badgeWraps ? badge.y + badge.height + Theme.space4 : 44
        width: Math.max(0, Math.min(root.timerNaturalWidth, root.width - Theme.space16 * 2))
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 12
        text: RecorderView.elapsedText
        font: Theme.timerFont
        color: root.state === "running" ? Theme.textPrimary
             : root.state === "paused" ? Theme.warningFg
             : root.state === "idle" || root.state === "preparing" ? Theme.textSecondary
             : Theme.textTertiary
    }

    // Longest main caption, so the slot (and the buttons) never move when the state changes.
    TextMetrics {
        id: mainCaptionMetrics
        font: Theme.captionFont
        text: qsTr("正在保存…")
    }

    // Pause + main button, captions centred under each. Centred on the card, but never over
    // the timer (left) nor past the save column (wide) / the card's right padding (compact);
    // stacked: own line under the timer, gap shrinking to 8 if the card is that narrow.
    // Plain Item, not Row: a positioner lays out on polish, which needs a window.
    Item {
        id: buttonRow
        objectName: "recordButtonRow"
        readonly property real spacing: root.stacked
            ? Math.max(Theme.space8, Math.min(Theme.space24, root.width - Theme.space16 * 2 - pauseSlot.width - mainSlot.width))
            : Theme.space24
        width: pauseSlot.width + spacing + mainSlot.width
        height: mainSlot.height
        x: root.stacked
            ? Math.max(Theme.space16, (root.width - width) / 2)
            : Math.max(timer.x + timer.width + Theme.space16,
                       Math.min((root.width - width) / 2,
                                (root.compact ? root.width : saveArea.x) - Theme.space16 - width))
        y: root.stacked ? timer.y + timer.height + Theme.space8 : Theme.space8

        Item {
            id: pauseSlot
            width: Math.max(pauseButton.implicitWidth, pauseCaption.implicitWidth)
            height: mainSlot.height

            RecordPauseButton {
                id: pauseButton
                objectName: "recordPauseButton"
                // Centred on the 64 px main button.
                x: (pauseSlot.width - width) / 2
                y: 10
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
                id: pauseCaption
                objectName: "recordPauseCaption"
                visible: root.state === "running" || root.state === "paused"
                y: mainCaption.y
                width: pauseSlot.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: root.state === "paused" ? qsTr("继续") : qsTr("暂停")
                font: Theme.captionFont
                color: Theme.textSecondary
            }
        }

        Item {
            id: mainSlot
            x: pauseSlot.width + buttonRow.spacing
            width: Math.max(mainButton.implicitWidth, Math.ceil(mainCaptionMetrics.advanceWidth))
            height: mainCaption.y + mainCaption.implicitHeight

            RecordMainButton {
                id: mainButton
                objectName: "recordMainButton"
                x: (mainSlot.width - width) / 2
                y: 0
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
                id: mainCaption
                objectName: "recordMainCaption"
                y: mainButton.height + Theme.space8
                width: mainSlot.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
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
        }
    }

    // Save area (§3.2.2 C): path + file line; the file line switches to the file being
    // written once the session starts. Wide: right column, x = W - 296 (never left of 424,
    // 16 right of the record button at the 720 switch), at most 224 wide and always clear
    // of the folder button; the buttons stay 16 left of it. Compact: one line
    // "保存到 <folder/file>" at y 104 under the buttons (stacked: 8 under the button row),
    // ending 8 px before the folder button.
    Item {
        id: saveArea
        objectName: "saveArea"
        x: root.compact ? Theme.space16 : Math.max(424, root.width - 296)
        y: root.stacked ? buttonRow.y + buttonRow.height + Theme.space8 : root.compact ? 104 : 16
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
