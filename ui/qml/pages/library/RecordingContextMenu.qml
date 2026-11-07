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
    MenuItem {
        text: "播放"
        enabled: !root.isRecording
        onTriggered: DesktopActions.openFile(root.filePath)
    }
    MenuItem {
        text: "在文件夹中显示"
        onTriggered: DesktopActions.revealInFolder(root.filePath)
    }
    MenuItem {
        text: "打开字幕文件"
        visible: root.srtPath.length > 0
        onTriggered: DesktopActions.openFile(root.srtPath)
    }
    MenuItem {
        text: "复制文件路径"
        onTriggered: DesktopActions.copyText(root.filePath)
    }
    MenuSeparator {}
    MenuItem {
        text: "删除…"
        enabled: !root.isRecording
        onTriggered: DialogRequests.deleteRecording(root.filePath, root.srtPath)
    }
}
