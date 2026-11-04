import QtQuick

// Module U6 (Codex). D5 delete recording, 440×212 (184 without .srt) (§3.6 D5).
// FROZEN API: openFor(filePath, srtPath). Moves to the recycle bin via
// RecordingLibrary.moveToTrash; failure -> T3-style danger toast "无法删除".
AppDialog {
    id: root
    property string filePath: ""
    property string srtPath: ""

    title: "删除这个录像？"
    primaryText: "删除"
    primaryKind: "danger"

    function openFor(path: string, srt: string) {
        root.filePath = path
        root.srtPath = srt
        open()
    }

    // TODO(U6): body with elided file name, "同时删除字幕文件" checkbox (default checked).
    onAccepted: {
        close()
        const error = RecordingLibrary.moveToTrash(filePath, srtPath.length > 0)
        if (error.length > 0)
            Toasts.show("danger", "无法删除", error, [], 0)
    }
    onRejected: close()
}
