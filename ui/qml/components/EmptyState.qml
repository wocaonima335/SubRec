import QtQuick

// Module U2 (ZCode). Centred empty/error state used by P1 S0/S6 and P2 L2/L4.
// FROZEN API:
//   iconName, iconColor, title, body (max 2 lines), detail (S6 remediation, hidden if "")
//   primaryText / secondaryText / linkText ("" hides that button; link = Ghost small)
//   primaryEnabled
//   signals primaryClicked(), secondaryClicked(), linkClicked()
Item {
    id: root
    property string iconName: ""
    property color iconColor: Theme.textTertiary
    property string title: ""
    property string body: ""
    property string detail: ""
    property string primaryText: ""
    property string secondaryText: ""
    property string linkText: ""
    property bool primaryEnabled: true
    signal primaryClicked()
    signal secondaryClicked()
    signal linkClicked()

    implicitWidth: 480
    implicitHeight: 200

    Column {
        anchors.centerIn: parent
        spacing: Theme.space8

        Item {
            width: 32
            height: 32
            anchors.horizontalCenter: parent.horizontalCenter
            AppIcon {
                anchors.centerIn: parent
                name: root.iconName
                size: 32
                color: root.iconColor
            }
        }

        Text {
            visible: root.title.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.title
            font: Theme.h2Font
            color: Theme.textPrimary
        }

        Text {
            visible: root.body.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.width
            horizontalAlignment: Text.AlignHCenter
            text: root.body
            font: Theme.bodyFont
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            visible: root.detail.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.width
            horizontalAlignment: Text.AlignHCenter
            text: root.detail
            font: Theme.captionFont
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Row {
            visible: root.primaryText.length > 0 || root.secondaryText.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space8
            topPadding: Theme.space4

            AppButton {
                visible: root.secondaryText.length > 0
                text: root.secondaryText
                onClicked: root.secondaryClicked()
            }
            AppButton {
                visible: root.primaryText.length > 0
                variant: "primary"
                enabled: root.primaryEnabled
                text: root.primaryText
                onClicked: root.primaryClicked()
            }
        }

        AppButton {
            visible: root.linkText.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            variant: "ghost"
            small: true
            text: root.linkText
            onClicked: root.linkClicked()
        }
    }
}
