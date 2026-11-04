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

    // TODO(U2): column layout per §3.2.3 S0/S6 and §3.3.2 L2.
}
