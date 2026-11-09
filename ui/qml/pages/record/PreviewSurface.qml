// GLPreview is registered at runtime by main.cpp (qmlRegisterType<FFGLItem>), so qmllint
// cannot resolve it outside the full Windows build. These three categories are disabled for
// this file only; everything else in it is still linted.
// qmllint disable import unresolved-type missing-property
import QtQuick
import GLPreview 1.0

// Module U5 (ZCode). The only GLView of the new UI. FROZEN: objectName "previewView"
// (main.cpp binds PreviewBridge to it by that name) and keepRatio (letterbox, §3.2.2 B).
Item {
    id: root
    // Size of the frames being previewed (0 until the first frame, or with the test double
    // that has no such properties); RecordPage sizes the preview area from it.
    readonly property int frameWidth: glView.frameWidth !== undefined ? glView.frameWidth : 0
    readonly property int frameHeight: glView.frameHeight !== undefined ? glView.frameHeight : 0

    GLView {
        id: glView
        objectName: "previewView"
        width: root.width   // not anchors: grouped properties of an unresolved type do not lint
        height: root.height
        keepRatio: true
    }
}
