import QtQuick
import QtQuick.Layouts

// Module U6 (Codex). P2 table header (40 high, bg/surface-2, top corners radius 8):
// 名称 / 录制时间 (default sort, descending) / 大小 (right aligned) / 字幕 (not sortable).
// FROZEN API: sortKey, sortDescending (bind from the model); signal sortRequested(key)
// (owner toggles direction when the same key is clicked again).
Rectangle {
    id: root
    property string sortKey: "modified"
    property bool sortDescending: true
    signal sortRequested(string key)

    implicitHeight: 40
    color: Theme.bgSurface2

    // TODO(U6): column labels and sort arrows.
    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.space12
        spacing: Theme.space12

        SortHeader {
            Layout.fillWidth: true
            objectName: "sortName"
            text: "名称"
            active: root.sortKey === "name"
            descending: root.sortDescending
            onClicked: root.sortRequested("name")
        }
        SortHeader {
            Layout.preferredWidth: 150
            objectName: "sortModified"
            text: "录制时间"
            active: root.sortKey === "modified"
            descending: root.sortDescending
            onClicked: root.sortRequested("modified")
        }
        SortHeader {
            Layout.preferredWidth: 100
            objectName: "sortSize"
            text: "大小"
            active: root.sortKey === "size"
            descending: root.sortDescending
            onClicked: root.sortRequested("size")
        }
        Text {
            font: Theme.captionFont
            Layout.preferredWidth: 90
            objectName: "subtitleColumn"
            text: "字幕"
            color: Theme.textSecondary
        }
        Item {
            Layout.preferredWidth: 200
        }
    }

    component SortHeader: Item {
        id: sortHeader
        property string text: ""
        property bool active: false
        property bool descending: true
        signal clicked
        implicitHeight: 32

        Row {
            anchors.fill: parent
            spacing: Theme.space4
            Text {
                font: Theme.captionFont
                anchors.verticalCenter: parent.verticalCenter
                text: sortHeader.text
                color: Theme.textSecondary
            }
            Text {
                font: Theme.captionFont
                anchors.verticalCenter: parent.verticalCenter
                visible: sortHeader.active
                text: sortHeader.descending ? "↓" : "↑"
                color: Theme.textSecondary
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: sortHeader.clicked()
        }
    }
}
