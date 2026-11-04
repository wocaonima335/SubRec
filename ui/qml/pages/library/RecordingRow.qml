import QtQuick

// Module U6 (Codex). One P2 row, 56 high (§3.3.1 data row, §2.5.11 ListRow).
// FROZEN API (RecordingListModel roles): fileName, filePath, modifiedText, sizeText,
// subtitleState, subtitleProgress, srtPath, isRecording, selected.
// Signals: clicked(), playRequested(), revealRequested(), deleteRequested(),
// contextMenuRequested(x, y). Row actions are hidden until hover/selection and disabled
// with tooltip "录制中" when isRecording (L5).
Rectangle {
    id: root
    property string fileName: ""
    property string filePath: ""
    property string modifiedText: ""
    property string sizeText: ""
    property string subtitleState: "none"
    property int subtitleProgress: 0
    property string srtPath: ""
    property bool isRecording: false
    property bool selected: false
    signal clicked()
    signal playRequested()
    signal revealRequested()
    signal deleteRequested()
    signal contextMenuRequested(real x, real y)

    implicitHeight: Theme.listRowHeight
    color: selected ? Theme.bgSelected : "transparent"

    // TODO(U6): columns, subtitle chip, hover actions, mouse handling.
}
