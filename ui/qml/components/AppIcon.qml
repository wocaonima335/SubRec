import QtQuick
import QtQuick.Effects

// Module U1 (ZCode). Single-colour Lucide icon (UI_REDESIGN §2.8), tinted by `color`.
// FROZEN API: name, size, color. Names are Lucide names ("monitor", "folder-open", ...);
// the SVGs live in ui/icons/ (registered as module resources); unknown name -> empty.
Item {
    id: root
    property string name: ""
    property int size: 16
    property color color: Theme.textSecondary

    implicitWidth: size
    implicitHeight: size

    readonly property bool valid: name.length > 0

    Image {
        id: source
        anchors.fill: parent
        visible: false
        fillMode: Image.PreserveAspectFit
        source: root.valid ? "icons/" + root.name + ".svg" : ""
        // Re-rasterise at the display size so 24-px strokes stay crisp at 16/20/32 px.
        sourceSize: Qt.size(root.size, root.size)
    }

    // Lucide strokes are currentColor (black when rasterised); full colorization replaces
    // every opaque pixel with `color`, so both themes tint correctly.
    MultiEffect {
        anchors.fill: parent
        visible: source.status === Image.Ready
        source: source
        colorizationColor: root.color
        colorization: 1.0
    }
}
