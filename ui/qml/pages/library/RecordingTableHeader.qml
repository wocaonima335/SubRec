import QtQuick

// Module U6 (Codex). P2 table header (40 high, bg/surface-2, top corners radius 8):
// 名称 / 录制时间 (default sort, descending) / 大小 (right aligned) / 字幕 (not sortable).
// FROZEN API: sortKey, sortDescending (bind from the model); signal sortRequested(key)
// (owner toggles direction when the same key is clicked again).
Rectangle {
    id: root
    property string sortKey: "modified"
    property bool sortDescending: true
    signal sortRequested(string key)

    implicitHeight: 40
    color: Theme.bgSurface2

    // TODO(U6): column labels and sort arrows.
}
