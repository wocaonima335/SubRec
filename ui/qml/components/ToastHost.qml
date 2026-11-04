import QtQuick

// Module U2 (ZCode). Renders Toasts.requested at the bottom-right of its parent:
// 24 from right/bottom, stacked upwards with 8 px gaps, max 3 (oldest dropped).
// FROZEN API: none besides placement (anchors.fill the content area).
Item {
    id: root

    // TODO(U2): ListModel + Repeater of Toast items, timers, hover pause, max 3.
    Connections {
        target: Toasts
        function onRequested(kind: string, title: string, body: string, actions: var, durationMs: int) {
            console.log("[Ui] toast (stub):", kind, title)
        }
    }
}
