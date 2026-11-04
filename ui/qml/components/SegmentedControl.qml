pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.5 segmented control.
// FROZEN API:
//   model: JS array of { text, value, iconName? }
//   currentValue: value of the selected segment (bind from the backend, never set from inside)
//   locked: §3.2.5; disabledReason
//   signal activated(var value): user picked a segment (owner writes the backend)
Control {
    id: root
    property var model: []
    property var currentValue
    property bool locked: false
    property string disabledReason: ""
    signal activated(var value)

    implicitHeight: Theme.controlHeight
    implicitWidth: 240

    hoverEnabled: enabled

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.bgSurface2
    }

    contentItem: Item {
        id: track

        Repeater {
            model: root.model

            delegate: Rectangle {
                id: segment
                required property var modelData
                required property int index

                readonly property bool selected: segment.modelData.value === root.currentValue

                x: 2 + index * (width + 0)
                y: 2
                width: (root.width - 4) / Math.max(1, root.model.length)
                height: root.height - 4
                radius: Theme.radiusSm
                color: selected ? (Theme.dark ? Theme.bgPressed : Theme.bgSurface)
                     : ma.containsMouse && !root.locked ? Theme.bgHover
                     : "transparent"
                // §2.5.5 light theme: selected segment gets a subtle elevation edge.
                border.width: selected && !Theme.dark ? 1 : 0
                border.color: Qt.rgba(0, 0, 0, 0.10)

                Row {
                    anchors.centerIn: parent
                    spacing: segment.modelData.iconName !== undefined ? Theme.space8 - 2 : 0

                    AppIcon {
                        visible: segment.modelData.iconName !== undefined
                        anchors.verticalCenter: parent.verticalCenter
                        name: segment.modelData.iconName !== undefined ? segment.modelData.iconName : ""
                        size: 16
                        color: segment.selected ? Theme.textPrimary
                             : ma.containsMouse ? Theme.textPrimary : Theme.textSecondary
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: segment.modelData.text
                        font: segment.selected ? Theme.bodyStrongFont : Theme.bodyFont
                        color: segment.selected ? Theme.textPrimary
                             : ma.containsMouse ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: root.enabled && !root.locked
                    cursorShape: root.enabled && !root.locked ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (root.enabled && !root.locked) root.activated(segment.modelData.value)
                }
            }
        }
    }

    HoverHandler {
        id: hover
    }
    AppToolTip {
        visible: root.locked && hover.hovered
        text: qsTr("录制中不能修改")
    }
    AppToolTip {
        visible: !root.enabled && hover.hovered && root.disabledReason.length > 0
        text: root.disabledReason
    }
}
