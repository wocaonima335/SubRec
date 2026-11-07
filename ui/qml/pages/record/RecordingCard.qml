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

    padding: Theme.space16
    implicitWidth: 336
    implicitHeight: 72

    readonly property bool hasSubtitle: subtitleState === "has"
    readonly property bool generating: subtitleState === "generating"

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
        x: 64
        y: 16
        width: 200
        text: root.fileName
        font: Theme.bodyStrongFont
        color: Theme.textPrimary
        elide: Text.ElideRight
    }

    Text {
        x: 64
        y: 38
        width: 200
        text: root.modifiedText + " · " + root.sizeText
        font: Theme.captionFont
        color: Theme.textTertiary
        elide: Text.ElideRight
    }

    // Subtitle chip (§3.2.2 E): "字幕" / "字幕 63 %"; hidden on hover for the actions.
    Rectangle {
        visible: (root.hasSubtitle || root.generating) && !hover.hovered
        x: root.width - 16 - chipText.implicitWidth - 20
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

    // Hover actions: play + reveal (§3.2.2 E).
    Row {
        visible: hover.hovered
        x: root.width - 16 - 32 * 2 - Theme.space8
        y: (root.height - 32) / 2
        spacing: Theme.space8

        IconButton {
            iconName: "play"
            toolTipText: qsTr("播放")
            onClicked: DesktopActions.openFile(root.filePath)
        }
        IconButton {
            iconName: "folder-open"
            toolTipText: qsTr("在文件夹中显示")
            onClicked: DesktopActions.revealInFolder(root.filePath)
        }
    }

    HoverHandler {
        id: hover
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: DesktopActions.openFile(root.filePath)
    }
}
