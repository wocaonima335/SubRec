import QtQuick
import QtQuick.Effects
import QtQuick.Window

// Module U1 (ZCode). Single-colour Lucide icon (UI_REDESIGN §2.8), tinted by `color`.
// FROZEN API: name, size, color. Names are Lucide names ("monitor", "folder-open", ...);
// the SVGs live in ui/icons/ (registered as module resources); unknown name -> empty.
// Read-only helpers: devicePixelRatio, pixelSize (the rasterisation size in device pixels).
Item {
    id: root
    property string name: ""
    property int size: 16
    property color color: Theme.textSecondary

    // Rasterise in device pixels: at 125 % / 150 % a 16-px icon is drawn from a 20 / 24-px
    // bitmap instead of a 16-px one scaled up (which blurred the 1.5-px Lucide strokes).
    // Screen.devicePixelRatio is 0 until the item is in a window, hence the floor of 1.
    readonly property real devicePixelRatio: Math.max(1, Screen.devicePixelRatio)
    readonly property int pixelSize: Math.ceil(size * devicePixelRatio)

    implicitWidth: size
    implicitHeight: size

    readonly property bool valid: name.length > 0

    Image {
        id: source
        objectName: "appIconImage"
        anchors.fill: parent
        visible: false
        fillMode: Image.PreserveAspectFit
        source: root.valid ? "icons/" + root.name + ".svg" : ""
        sourceSize: Qt.size(root.pixelSize, root.pixelSize)
        // Down-sampling from the device-pixel bitmap to the item stays filtered; no mipmaps
        // needed for a 1:1 (or near) ratio.
        smooth: true
        mipmap: false
    }

    // Lucide strokes are currentColor (black when rasterised); full colorization replaces
    // every opaque pixel with `color`, so both themes tint correctly. The Image is the
    // texture provider, so the effect samples the device-pixel bitmap directly (no
    // intermediate layer at logical size).
    MultiEffect {
        anchors.fill: parent
        visible: source.status === Image.Ready
        source: source
        colorizationColor: root.color
        colorization: 1.0
    }
}
