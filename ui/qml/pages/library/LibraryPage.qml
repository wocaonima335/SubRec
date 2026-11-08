pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// Module U6 (Codex). P2 录像库 (UI_REDESIGN §3.3): title, search AppTextField
// (-> RecordingLibrary.all.filterText), refresh IconButton (RecordingLibrary.refresh),
// "打开保存文件夹" (DesktopActions.openFolder(RecordingLibrary.folder)), summary line
// (RecordingLibrary.summaryText), list Card with RecordingTableHeader + ListView of
// RecordingRow over RecordingLibrary.all.
// States: L1 rows; L2 empty (EmptyState: "去录制" -> Navigation.open("record"),
// "更改保存位置" -> Navigation.open("settings/output")); L3 folderStatus != "ok" (danger
// InlineAlert); L4 filter matched nothing; L5 the active recording row (isRecording).
// Keys: Enter = play, Delete = DialogRequests.deleteRecording(...).
// FROZEN API: none.
Item {
    id: root

    RecordingContextMenu {
        id: contextMenu
    }

    // TODO(U6): full page.
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pagePadding
        spacing: Theme.space16

        Text {
            text: "录像库"
            font: Theme.h1Font
            color: Theme.textPrimary
        }

        RowLayout {
            Layout.fillWidth: true

            AppTextField {
                Layout.fillWidth: true
                placeholderText: "搜索录像"
                text: RecordingLibrary.all.filterText
                onEditingFinished: RecordingLibrary.all.filterText = text
            }
            AppButton {
                text: "刷新"
                onClicked: RecordingLibrary.refresh()
            }
            AppButton {
                text: "打开保存文件夹"
                onClicked: DesktopActions.openFolder(RecordingLibrary.folder)
            }
        }

        InlineAlert {
            Layout.fillWidth: true
            visible: RecordingLibrary.folderStatus !== "ok"
            kind: "danger"
            title: "保存文件夹不可用"
            body: RecordingLibrary.folderStatus
        }

        Text {
            font: Theme.captionFont
            text: RecordingLibrary.summaryText
            color: Theme.textSecondary
        }

        RecordingTableHeader {
            Layout.fillWidth: true
            sortKey: RecordingLibrary.all.sortKey
            sortDescending: RecordingLibrary.all.sortDescending
            onSortRequested: function (key) {
                if (RecordingLibrary.all.sortKey === key)
                    RecordingLibrary.all.sortDescending = !RecordingLibrary.all.sortDescending;
                else
                    RecordingLibrary.all.sortKey = key;
            }
        }

        EmptyState {
            Layout.fillWidth: true
            visible: RecordingLibrary.all.count === 0
            title: RecordingLibrary.all.filterText.length > 0 ? "没有匹配的录像" : "还没有录像"
            body: "可以去录制页面开始录制，或更改保存位置。"
        }
        AppButton {
            visible: RecordingLibrary.totalCount === 0
            text: "去录制"
            onClicked: Navigation.open("record")
        }
        AppButton {
            visible: RecordingLibrary.totalCount === 0 || RecordingLibrary.folderStatus !== "ok"
            text: "更改保存位置"
            onClicked: Navigation.open("settings/output")
        }
        ListView {
            id: recordingList
            focus: true
            Keys.onReturnPressed: {
                const row = RecordingLibrary.all.get(currentIndex);
                if (row.filePath && !row.isRecording)
                    DesktopActions.openFile(row.filePath);
            }
            Keys.onDeletePressed: {
                const row = RecordingLibrary.all.get(currentIndex);
                if (row.filePath && !row.isRecording)
                    DialogRequests.deleteRecording(row.filePath, row.srtPath);
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: RecordingLibrary.all
            delegate: RecordingRow {
                id: recordingRow
                required property var model
                required property int index
                selected: ListView.isCurrentItem
                onClicked: recordingList.currentIndex = recordingRow.index
                onContextMenuRequested: function (x, y) {
                    recordingList.currentIndex = recordingRow.index;
                    contextMenu.filePath = recordingRow.filePath;
                    contextMenu.srtPath = recordingRow.srtPath;
                    contextMenu.isRecording = recordingRow.isRecording;
                    const point = recordingRow.mapToItem(root, x, y);
                    contextMenu.popup(point.x, point.y);
                }
                width: ListView.view.width
                fileName: recordingRow.model.fileName
                filePath: recordingRow.model.filePath
                modifiedText: recordingRow.model.modifiedText
                sizeText: recordingRow.model.sizeText
                subtitleState: recordingRow.model.subtitleState
                subtitleProgress: recordingRow.model.subtitleProgress
                srtPath: recordingRow.model.srtPath
                isRecording: recordingRow.model.isRecording
                onPlayRequested: DesktopActions.openFile(filePath)
                onRevealRequested: DesktopActions.revealInFolder(filePath)
                onDeleteRequested: DialogRequests.deleteRecording(filePath, srtPath)
            }
        }
    }
}
