pragma Singleton
import QtQuick

// Module U2 (ZCode). Toast bus. Any file raises a toast with Toasts.show(...); ToastHost
// (placed once by AppWindow) renders them (§2.5.10, §3.7 T1–T7).
// FROZEN API:
//   show(kind, title, body, actions, durationMs)
//     kind: "info" | "success" | "warning" | "danger"
//     actions: array of { id, text, ghost? } (max 2), [] for none
//     durationMs: 0 = default (5 s, 8 s with actions, T7 3 s); hover pauses the timer
//   signal requested(...)       — consumed by ToastHost only
//   signal actionTriggered(id)  — the raiser listens for its own action ids
//     (ids are namespaced by the raiser: "t1.openFolder", "t4.download", ...)
QtObject {
    signal requested(string kind, string title, string body, var actions, int durationMs)
    signal actionTriggered(string actionId)

    function show(kind: string, title: string, body: string, actions: var, durationMs: int) {
        requested(kind, title, body, actions, durationMs)
    }
}
