import QtQuick

// Module U5 (ZCode). P1 area E: "最近录制" title, "查看全部 ›" -> Navigation.open("library"),
// three RecordingCards from RecordingLibrary.recent (limit 3, newest first), or the dashed
// empty state "还没有录像。录完后会出现在这里。".
// FROZEN API: none.
Item {
    id: root

    // TODO(U5): title row, Repeater over RecordingLibrary.recent, empty state.
    Text {
        text: "最近录制"
        font: Theme.h2Font
        color: Theme.textPrimary
    }
}
