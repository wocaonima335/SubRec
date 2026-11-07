import QtQuick

// Module U2 (ZCode). §2.5.10 inline alert.
// FROZEN API:
//   kind: "info" | "success" | "warning" | "danger"
//   title, body (max 3 lines), actionText ("" = no button), closable
//   signals actionClicked(), closed()
Rectangle {
    id: root
    property string kind: "info"
    property string title: ""
    property string body: ""
    property string actionText: ""
    property bool closable: false
    signal actionClicked()
    signal closed()

    readonly property color __fg: kind === "danger" ? Theme.dangerFg
                                 : kind === "warning" ? Theme.warningFg
                                 : kind === "success" ? Theme.successFg : Theme.infoFg
    readonly property string __iconName: kind === "danger" ? "circle-alert"
                                       : kind === "warning" ? "triangle-alert"
                                       : kind === "success" ? "circle-check" : "info"

    radius: Theme.radiusMd
    color: kind === "danger" ? Theme.dangerBg
         : kind === "warning" ? Theme.warningBg
         : kind === "success" ? Theme.successBg : Theme.infoBg
    border.width: 1
    border.color: kind === "danger" ? Theme.dangerBorder : Qt.rgba(root.__fg.r, root.__fg.g, root.__fg.b, 0.4)

    implicitHeight: Math.max(56, Theme.space12 * 2 + titleRow.implicitHeight + (bodyText.visible ? Theme.space4 + bodyText.implicitHeight : 0))
    implicitWidth: 560

    Row {
        id: titleRow
        x: Theme.space16
        y: Theme.space12
        width: parent.width - Theme.space16 * 2 - (root.closable ? 40 : 0)
        spacing: Theme.space8

        AppIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.__iconName
            size: 16
            color: root.__fg
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 16 - Theme.space8
            text: root.title
            font: Theme.bodyStrongFont
            color: Theme.textPrimary
            elide: Text.ElideRight
        }
    }

    Text {
        id: bodyText
        visible: root.body.length > 0
        x: Theme.space16 + 16 + Theme.space8
        y: titleRow.y + titleRow.implicitHeight + Theme.space4
        width: parent.width - x - Theme.space16 - (root.closable ? 40 : 0)
        text: root.body
        font: Theme.captionFont
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
        // §2.5.10: at most 3 lines.
        maximumLineCount: 3
        elide: Text.ElideRight
    }

    // §2.5.10: one small button on the right of the body.
    AppButton {
        visible: root.actionText.length > 0
        anchors.right: root.closable ? closeButton.left : parent.right
        anchors.rightMargin: Theme.space8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space12
        small: true
        text: root.actionText
        onClicked: root.actionClicked()
    }

    IconButton {
        id: closeButton
        visible: root.closable
        anchors.right: parent.right
        anchors.rightMargin: Theme.space4
        anchors.top: parent.top
        anchors.topMargin: Theme.space4
        iconName: "x"
        onClicked: root.closed()
    }
}
