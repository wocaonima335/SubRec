pragma ComponentBehavior: Bound
import QtQuick

// Module U2 (ZCode). Renders Toasts.requested at the bottom-right of its parent:
// 24 from right/bottom, stacked upwards with 8 px gaps, max 3 (oldest dropped).
// FROZEN API: none besides placement (anchors.fill the content area).
Item {
    id: root

    // Plain value list: toasts enter at the end (bottom of the stack) and the oldest
    // entry is dropped beyond three (§2.5.10).
    property var toasts: []

    Connections {
        target: Toasts
        function onRequested(kind: string, title: string, body: string, actions: var, durationMs: int) {
            const next = root.toasts.slice()
            next.push({
                kind: kind,
                title: title,
                body: body,
                actions: actions,
                durationMs: durationMs
            })
            root.toasts = next.slice(Math.max(0, next.length - 3))
        }
    }

    function dismiss(toast: var) {
        root.toasts = root.toasts.filter(function(t) { return t !== toast })
    }

    Column {
        anchors.right: parent.right
        anchors.rightMargin: Theme.space24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space24
        spacing: Theme.space8

        Repeater {
            model: root.toasts

            delegate: Toast {
                id: toastDelegate
                required property var modelData

                kind: toastDelegate.modelData.kind
                title: toastDelegate.modelData.title
                body: toastDelegate.modelData.body
                actions: toastDelegate.modelData.actions
                durationMs: toastDelegate.modelData.durationMs

                onActionClicked: (actionId) => Toasts.actionTriggered(actionId)
                onDismissed: root.dismiss(toastDelegate.modelData)
            }
        }
    }
}
