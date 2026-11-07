import QtQuick

// Module U4 (ZCode). §2.7 resize handles: 6 px on the four edges and corners, calling
// Window.startSystemResize(edges). Disabled while maximized. Place as the topmost child
// of the window content (anchors.fill: parent); it only catches presses on the borders.
// FROZEN API: window, edgeSize (6).
Item {
    id: root
    property Window window
    property int edgeSize: 6

    enabled: window && window.visibility !== Window.Maximized

    MouseArea {
        x: 0
        y: root.edgeSize
        width: root.edgeSize
        height: parent.height - 2 * root.edgeSize
        cursorShape: Qt.SizeHorCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.LeftEdge); mouse.accepted = true }
    }
    MouseArea {
        anchors.right: parent.right
        y: root.edgeSize
        width: root.edgeSize
        height: parent.height - 2 * root.edgeSize
        cursorShape: Qt.SizeHorCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.RightEdge); mouse.accepted = true }
    }
    MouseArea {
        y: 0
        x: root.edgeSize
        width: parent.width - 2 * root.edgeSize
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.TopEdge); mouse.accepted = true }
    }
    MouseArea {
        anchors.bottom: parent.bottom
        x: root.edgeSize
        width: parent.width - 2 * root.edgeSize
        height: root.edgeSize
        cursorShape: Qt.SizeVerCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.BottomEdge); mouse.accepted = true }
    }
    MouseArea {
        x: 0; y: 0
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeFDiagCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.TopEdge | Qt.LeftEdge); mouse.accepted = true }
    }
    MouseArea {
        anchors.right: parent.right
        y: 0
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeBDiagCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.TopEdge | Qt.RightEdge); mouse.accepted = true }
    }
    MouseArea {
        x: 0
        anchors.bottom: parent.bottom
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeBDiagCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.BottomEdge | Qt.LeftEdge); mouse.accepted = true }
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.edgeSize * 2
        height: root.edgeSize * 2
        cursorShape: Qt.SizeFDiagCursor
        onPressed: (mouse) => { root.window.startSystemResize(Qt.BottomEdge | Qt.RightEdge); mouse.accepted = true }
    }
}
