pragma ComponentBehavior: Bound
import QtQuick

// Module U5 (ZCode). P1 area E: "最近录制" title, "查看全部 ›" -> Navigation.open("library"),
// three RecordingCards from RecordingLibrary.recent (limit 3, newest first), or the dashed
// empty state "还没有录像。录完后会出现在这里。".
// FROZEN API: none.
Column {
    id: root

    Item {
        width: parent.width
        height: 28

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("最近录制")
            font: Theme.h2Font
            color: Theme.textPrimary
        }
        AppButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            variant: "ghost"
            small: true
            text: qsTr("查看全部 ›")
            onClicked: Navigation.open("library")
        }
    }

    Item { width: 1; height: Theme.space8 }

    // Empty state until the first recording exists (also while U6 is not merged yet).
    Rectangle {
        visible: RecordingLibrary.recent.count === 0
        width: root.width
        height: 72
        radius: Theme.radiusMd
        color: "transparent"
        border.width: 1
        border.color: Theme.borderDefault

        Text {
            anchors.centerIn: parent
            text: qsTr("还没有录像。录完后会出现在这里。")
            font: Theme.bodyFont
            color: Theme.textTertiary
        }
    }

    Row {
        visible: RecordingLibrary.recent.count > 0
        spacing: Theme.space16

        Repeater {
            model: RecordingLibrary.recent

            delegate: Item {
                id: row
                required property string fileName
                required property string filePath
                required property string modifiedText
                required property string sizeText
                required property string subtitleState
                required property int subtitleProgress

                width: (root.width - Theme.space16 * 2) / 3
                height: 72

                RecordingCard {
                    anchors.fill: row
                    fileName: row.fileName
                    filePath: row.filePath
                    modifiedText: row.modifiedText
                    sizeText: row.sizeText
                    subtitleState: row.subtitleState
                    subtitleProgress: row.subtitleProgress
                }
            }
        }
    }
}
