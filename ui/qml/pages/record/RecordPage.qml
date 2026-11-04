import QtQuick

// Module U5 (ZCode). P1 录制 (UI_REDESIGN §3.2). Layout at 1280×800 (content area origin
// 224,64): A title row, B PreviewPanel 736×414 (16:9, grows with width), C ControlBar
// 736×104, D SourcePanel 280 wide, E RecentRecordings.
// All recording state comes from RecorderView.uiState (§3.2.3); all subtitle state from
// SubtitleView.uiState (§3.2.4). Nothing is cached in QML.
// FROZEN API: none (page root; navigation via Navigation, dialogs via DialogRequests).
Item {
    id: root

    // TODO(U5): title row (h1 "录制" + resolution/encoder line with tooltip), responsive layout.
    Text {
        x: Theme.pagePadding
        y: Theme.pagePadding
        text: "录制"
        font: Theme.h1Font
        color: Theme.textPrimary
    }

    PreviewPanel {
        id: preview
        x: Theme.pagePadding
        y: 72
        width: 736
        height: 414
    }

    ControlBar {
        x: Theme.pagePadding
        y: preview.y + preview.height + 16
        width: preview.width
        height: 104
    }

    SourcePanel {
        x: preview.x + preview.width + 16
        y: preview.y
        width: Theme.sourcePanelWidth
        height: 534
    }

    RecentRecordings {
        x: Theme.pagePadding
        y: 622
        width: 1032
        height: 104
    }
}
