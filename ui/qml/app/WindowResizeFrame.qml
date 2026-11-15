import QtQuick

// Module U4 (ZCode). §2.7 resize handles: 6 px on the four edges and corners, calling
// Window.startSystemResize(edges). Disabled while maximized. Place as the topmost child
// of the window content (anchors.fill: parent); it only catches presses on the borders.
// FROZEN API: window, edgeSize (6).
Item {
    id: root
    property Window window
    property int edgeSize: 6

    // Windows uses ordinary mouse events and geometry updates to avoid the native
    // modal resize loop (PC3 Win10). Other platforms retain startSystemResize.
    // The original module comment above describes that non-Windows path.
    readonly property bool resizing: resizeHandler.active
    enabled: window && window.visibility === Window.Windowed
    onEnabledChanged: {
        if (!enabled)
            resizeHandler.finish()
    }
    onWindowChanged: resizeHandler.finish()

    WindowResizeHandler {
        id: resizeHandler
        objectName: "windowResizeHandler"
    }

    function startResize(area, mouse, edges) {
        mouse.accepted = resizeHandler.start(root.window, edges,
                                             area.mapToGlobal(mouse.x, mouse.y))
    }

    MouseArea {
        x: 0
        y: root.edgeSize
        width: root.edgeSize
        height: parent.height - 2 * root.edgeSize
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.LeftEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        anchors.right: parent.right
        y: root.edgeSize
        width: root.edgeSize
        height: parent.height - 2 * root.edgeSize
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.RightEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        y: 0
        x: root.edgeSize
        width: parent.width - 2 * root.edgeSize
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.TopEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        anchors.bottom: parent.bottom
        x: root.edgeSize
        width: parent.width - 2 * root.edgeSize
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.BottomEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        x: 0; y: 0
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeFDiagCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.TopEdge | Qt.LeftEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        anchors.right: parent.right
        y: 0
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.TopEdge | Qt.RightEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        x: 0
        anchors.bottom: parent.bottom
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.BottomEdge | Qt.LeftEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeFDiagCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: (mouse) => root.startResize(this, mouse, Qt.BottomEdge | Qt.RightEdge)
        onReleased: resizeHandler.finish()
        onCanceled: resizeHandler.finish()
    }
}
