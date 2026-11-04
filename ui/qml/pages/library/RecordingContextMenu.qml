import QtQuick
import QtQuick.Controls.Basic

// Module U6 (Codex). P2 right-click menu, 200 wide, dropdown-popup style:
// 播放 / 在文件夹中显示 / 打开字幕文件 (only with srtPath) / 复制文件路径 / — / 删除… (danger).
// FROZEN API: filePath, srtPath, isRecording (disables play/delete); popup(x, y) as Menu.
Menu {
    id: root
    property string filePath: ""
    property string srtPath: ""
    property bool isRecording: false

    // TODO(U6): items wired to DesktopActions / DialogRequests.deleteRecording.
}
