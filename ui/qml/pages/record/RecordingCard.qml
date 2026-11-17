import QtQuick

// Module U5 (ZCode). One recent-recording card, 336×72 (§3.2.2 E).
// FROZEN API (filled from RecordingListModel roles): fileName, filePath, modifiedText,
// sizeText, subtitleState, subtitleProgress. Hover shows play (DesktopActions.openFile) and
// reveal (DesktopActions.revealInFolder) instead of the subtitle chip; double-click plays.
Card {
    id: root
    property string fileName: ""
    property string filePath: ""
    property string modifiedText: ""
    property string sizeText: ""
    property string subtitleState: "none"
    property int subtitleProgress: 0

    // 0: the children below use card coordinates (§3.2.2 E: tile x 16, texts x 64). With
    // Card's 16 they sat 16 px further right and down (tile 2 px above the bottom edge, hover
    // actions flush with the right edge, 200-px texts running under the actions).
    padding: 0
    implicitWidth: 336
    implicitHeight: 72

    readonly property bool hasSubtitle: subtitleState === "has"
    readonly property bool generating: subtitleState === "generating"
    // UI 1114: the right-hand slot holds the subtitle chip, or on hover the play / reveal
    // actions. The texts always end 8 px before the wider of the two, so the actions never
    // cover them and hovering never moves them; long names elide.
    readonly property real __slotWidth: Math.max(actions.width, chip.width)
    readonly property real __textWidth: Math.max(0, width - 64 - Theme.space8 - __slotWidth - Theme.space16)

    // Icon tile: 36×36 surface-2 with the video glyph.
    Rectangle {
        x: Theme.space16
        y: (root.height - 36) / 2
        width: 36
        height: 36
        radius: Theme.radiusSm
        color: Theme.bgSurface2

        AppIcon {
            anchors.centerIn: parent
            name: "video"
            size: 20
            color: Theme.textSecondary
        }
    }

    Text {
        objectName: "recordingCardName"
        x: 64
        y: 16
        width: root.__textWidth
        text: root.fileName
        font: Theme.bodyStrongFont
        color: Theme.textPrimary
        elide: Text.ElideRight
    }

    Text {
        objectName: "recordingCardInfo"
        x: 64
        y: 38
        width: root.__textWidth
        text: root.modifiedText + " · " + root.sizeText
        font: Theme.captionFont
        color: Theme.textTertiary
        elide: Text.ElideRight
    }

    // Subtitle chip (§3.2.2 E): "字幕" / "字幕 63 %"; hidden on hover for the actions.
    Rectangle {
        id: chip
        visible: (root.hasSubtitle || root.generating) && !hover.hovered
        x: root.width - 16 - width
        y: (root.height - 24) / 2
        width: chipText.implicitWidth + 20
        height: 24
        radius: 12
        color: root.generating ? Theme.infoBg : Theme.successBg

        Text {
            id: chipText
            anchors.centerIn: parent
            text: root.generating ? qsTr("字幕 %1%").arg(root.subtitleProgress) : qsTr("字幕")
            font: Theme.badgeFont
            color: root.generating ? Theme.infoFg : Theme.successFg
        }
    }

    // Double-click plays. Declared before the actions so the buttons stack above it: declared
    // after them, it took their press and play / reveal never clicked (as RecordingRow).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: DesktopActions.openFile(root.filePath)
    }

    // Hover actions: play + reveal (§3.2.2 E). Fixed slots, not a Row: same geometry whether
    // shown or not.
    Item {
        id: actions
        objectName: "recordingCardActions"
        visible: hover.hovered
        x: root.width - 16 - width
        y: (root.height - 32) / 2
        width: 32 * 2 + Theme.space8
        height: 32

        IconButton {
            objectName: "recordingCardPlay"
            x: 0
            iconName: "play"
            toolTipText: qsTr("播放")
            onClicked: DesktopActions.openFile(root.filePath)
        }
        IconButton {
            objectName: "recordingCardReveal"
            x: 32 + Theme.space8
            iconName: "folder-open"
            toolTipText: qsTr("在文件夹中显示")
            onClicked: DesktopActions.revealInFolder(root.filePath)
        }
    }

    HoverHandler {
        id: hover
    }
}
