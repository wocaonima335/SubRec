import QtQuick

// Module U4 (ZCode). §2.8 logo: rounded square record/default with a white "字".
// FROZEN API: size (20 title bar, 48 About), radius scales (5 at 20, 10 at 48).
Rectangle {
    id: root
    property int size: 20

    width: size
    height: size
    radius: size >= 48 ? 10 : 5
    color: Theme.recordDefault

    Text {
        anchors.centerIn: parent
        text: "字"
        color: Theme.textOnAccent
        font.family: Theme.fontFamily
        font.pixelSize: root.size >= 48 ? 28 : 12
        font.weight: Font.DemiBold
    }
}
