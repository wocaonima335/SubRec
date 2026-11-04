import QtQuick

// Module U1 (ZCode). Single-colour Lucide icon (UI_REDESIGN §2.8), tinted by `color`.
// FROZEN API: name, size, color. Names are Lucide names ("monitor", "folder-open", ...);
// the SVGs live in ui/icons/ (U1 adds them to ui/files_zcode.cmake RESOURCES).
// TODO(U1): Image + MultiEffect colourisation (or pre-tinted SVG); unknown name -> empty.
Item {
    id: root
    property string name: ""
    property int size: 16
    property color color: Theme.textSecondary

    implicitWidth: size
    implicitHeight: size
}
