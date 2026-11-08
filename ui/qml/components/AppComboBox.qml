pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.3 combo box.
// FROZEN API:
//   model: JS array of { text, value, secondaryText?, enabled? } (textRole "text",
//          valueRole "value" are preset); secondaryText -> 52-px two-line option;
//          enabled === false -> disabled option with "不可用" on the right.
//   compact: 32-px trigger (P1 source panel only)
//   locked: §3.2.5 locked state (no arrow, transparent border, tooltip "录制中不能修改")
//   placeholderText: shown when currentIndex < 0 (e.g. "没有找到麦克风")
//   disabledReason: tooltip while disabled
// Select with currentIndex / indexOfValue(); react to `activated(index)` (user action only).
// A user pick does not break an owner's `currentIndex:` binding (ComboBox sets it from C++).
ComboBox {
    id: root
    property bool compact: false
    property bool locked: false
    property string placeholderText: ""
    property string disabledReason: ""

    textRole: "text"
    valueRole: "value"
    implicitHeight: compact ? Theme.compactControlHeight : Theme.controlHeight
    implicitWidth: 240
    font: Theme.bodyFont

    opacity: enabled ? 1.0 : 0.4
    hoverEnabled: enabled && !locked
    spacing: 0
    leftPadding: Theme.space12
    rightPadding: Theme.space12
    topPadding: 0
    bottomPadding: 0

    // §2.5.3: max 8 visible rows (two-line options 52 px, normal 36 px).
    readonly property real __rowHeight: (model !== null && model.length > 0
                                         && model[0].secondaryText !== undefined) ? 52 : 36

    indicator: AppIcon {
        x: root.mirrored ? root.leftPadding : root.width - width - root.rightPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        visible: !root.locked
        name: "chevron-down"
        size: 16
        color: root.hovered ? Theme.textPrimary : Theme.textSecondary
        // §2.5.3: no rotation is specified for the closed state; open state rotates.
        rotation: root.popup.visible ? 180 : 0
        Behavior on rotation { enabled: !Theme.reduceMotion; NumberAnimation { duration: Theme.durationHover } }
    }

    contentItem: Text {
        leftPadding: 0
        rightPadding: root.indicator.width + Theme.space12
        text: root.currentIndex >= 0 ? root.currentText : root.placeholderText
        font: root.font
        color: root.currentIndex >= 0 ? Theme.textPrimary : Theme.textTertiary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface2
        border.width: root.locked ? 0 : 1
        border.color: root.hovered || root.popup.visible ? Theme.borderStrong : Theme.borderDefault
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

    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: ListView.view.width
        height: option.modelData.secondaryText !== undefined ? 52 : 36
        enabled: option.modelData.enabled === undefined || option.modelData.enabled
        hoverEnabled: enabled
        highlighted: root.highlightedIndex === index

        contentItem: Item {
            anchors.fill: parent
            anchors.leftMargin: Theme.space12
            anchors.rightMargin: Theme.space12

            AppIcon {
                id: checkMark
                visible: option.modelData.value === root.currentValue
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: 16
                color: Theme.accentDefault
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 16 + Theme.space4 // 16 px check slot + 4 gap
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Text {
                    width: parent.width > 0 ? parent.width : implicitWidth
                    text: option.modelData.text
                    font: Theme.bodyFont
                    color: option.enabled ? Theme.textPrimary : Theme.textDisabled
                    elide: Text.ElideRight
                }
                Text {
                    visible: option.modelData.secondaryText !== undefined
                    width: parent.width > 0 ? parent.width : implicitWidth
                    text: option.modelData.secondaryText !== undefined ? option.modelData.secondaryText : ""
                    font: Theme.captionFont
                    color: Theme.textTertiary
                    elide: Text.ElideRight
                }
            }

            Text {
                visible: !option.enabled
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("不可用")
                font: Theme.captionFont
                color: Theme.textTertiary
            }
        }

        background: Rectangle {
            radius: Theme.radiusXs
            color: option.highlighted && option.enabled ? Theme.bgHover : "transparent"
        }
        // No onClicked here: ComboBox connects every delegate it creates through
        // delegateModel and, on click, sets currentIndex, emits activated(index) and closes
        // the popup itself. Doing it here as well would emit activated twice and assigning
        // currentIndex from QML would break the owner's currentIndex binding.
    }

    popup: Popup {
        y: root.height + Theme.space4
        width: Math.max(200, root.width)
        padding: Theme.space4
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * padding,
                                  8 * root.__rowHeight + 2 * padding)

        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.bgSurface
            border.width: 1
            border.color: Theme.borderDefault
        }

        // The rows are ComboBox's own delegates, reached through delegateModel (the raw
        // `model` array has no delegate here, so the list used to render nothing). Only
        // populated while open, as in the Qt ComboBox customization example.
        contentItem: ListView {
            objectName: "comboPopupList"
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            spacing: 0
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationDialog }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durationHover }
        }
    }
}
