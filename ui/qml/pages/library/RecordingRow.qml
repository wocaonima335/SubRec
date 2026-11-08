import QtQuick
import QtQuick.Layouts

// Module U6 (Codex). One P2 row, 56 high (§3.3.1 data row, §2.5.11 ListRow).
// FROZEN API (RecordingListModel roles): fileName, filePath, modifiedText, sizeText,
// subtitleState, subtitleProgress, srtPath, isRecording, selected.
// Signals: clicked(), playRequested(), revealRequested(), deleteRequested(),
// contextMenuRequested(x, y). Row actions are hidden until hover/selection and disabled
// with tooltip "录制中" when isRecording (L5).
Rectangle {
    id: root
    property string fileName: ""
    property string filePath: ""
    property string modifiedText: ""
    property string sizeText: ""
    property string subtitleState: "none"
    property int subtitleProgress: 0
    property string srtPath: ""
    property bool isRecording: false
    property bool selected: false
    signal clicked
    signal playRequested
    signal revealRequested
    signal deleteRequested
    signal contextMenuRequested(real x, real y)

    implicitHeight: Theme.listRowHeight
    color: selected || hoverHandler.hovered ? Theme.bgSelected : "transparent"

    // TODO(U6): columns, subtitle chip, hover actions, mouse handling.
    HoverHandler {
        id: hoverHandler
    }
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: root.clicked()
        onDoubleClicked: if (!root.isRecording)
            root.playRequested()
        onPressed: function (mouse) {
            if (mouse.button === Qt.RightButton)
                root.contextMenuRequested(mouse.x, mouse.y);
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.space12
        spacing: Theme.space12

        Text {
            font: Theme.bodyFont
            Layout.fillWidth: true
            text: root.fileName
            elide: Text.ElideMiddle
            color: Theme.textPrimary
        }
        Text {
            font: Theme.captionFont
            Layout.preferredWidth: 150
            text: root.modifiedText
            color: Theme.textSecondary
        }
        Text {
            font: Theme.captionFont
            Layout.preferredWidth: 100
            text: root.sizeText
            color: Theme.textSecondary
        }
        Text {
            font: Theme.captionFont
            Layout.preferredWidth: 90
            text: root.isRecording ? "录制中" : ({
                    none: "无字幕",
                    has: "有字幕",
                    generating: "生成中",
                    failed: "失败"
                })[root.subtitleState] || ""
            color: Theme.textSecondary
        }
        RowLayout {
            Layout.preferredWidth: 200
            opacity: root.selected || hoverHandler.hovered ? 1 : 0
            enabled: opacity > 0
            AppButton {
                small: true
                objectName: "rowPlay"
                text: "播放"
                enabled: !root.isRecording
                disabledReason: "录制中"
                onClicked: root.playRequested()
            }
            AppButton {
                small: true
                objectName: "rowReveal"
                text: "文件夹"
                onClicked: root.revealRequested()
            }
            AppButton {
                small: true
                objectName: "rowDelete"
                text: "删除"
                variant: "danger"
                enabled: !root.isRecording
                disabledReason: "录制中"
                onClicked: root.deleteRequested()
            }
        }
    }
}
