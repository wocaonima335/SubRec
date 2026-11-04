import QtQuick

// Module U4 (ZCode). §2.7 resize handles: 6 px on the four edges and corners, calling
// Window.startSystemResize(edges). Disabled while maximized. Place as the topmost child
// of the window content (anchors.fill: parent); it only catches presses on the borders.
// FROZEN API: window, edgeSize (6).
Item {
    id: root
    property Window window
    property int edgeSize: 6

    // TODO(U4): 8 MouseArea/DragHandler zones with cursor shapes.
}
