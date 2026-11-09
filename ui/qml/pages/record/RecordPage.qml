import QtQuick

// Module U5 (ZCode). P1 录制 (UI_REDESIGN §3.2). Layout at 1280×800 (content area origin
// 224,64): A title row, B PreviewPanel 736×414 (16:9, grows with width), C ControlBar
// 736×104, D SourcePanel 280 wide, E RecentRecordings.
// All recording state comes from RecorderView.uiState (§3.2.3); all subtitle state from
// SubtitleView.uiState (§3.2.4). Nothing is cached in QML.
// FROZEN API: none (page root; navigation via Navigation, dialogs via DialogRequests).
Flickable {
    id: root
    contentWidth: width
    contentHeight: layout.height
    clip: false

    // §2.7: the source panel keeps its 280 px at narrow widths (decision 8); the preview
    // absorbs the remaining width. Its height follows the source frame's aspect ratio once a
    // frame is known (screens of different sizes/ratios no longer leave large bars in a fixed
    // 16:9 box), else 16:9; clamped between 32:9 and 1:1 so a portrait or very wide source
    // cannot break the layout (the GL view letterboxes whatever remains, keepRatio).
    readonly property int sideMargin: Theme.pagePadding
    readonly property int gap: Theme.space16
    readonly property int sourcePanelWidth: Theme.sourcePanelWidth
    readonly property int previewWidth: width - sideMargin * 2 - gap - sourcePanelWidth
    readonly property real previewRatio: previewPanel.frameWidth > 0 && previewPanel.frameHeight > 0
                                         ? previewPanel.frameHeight / previewPanel.frameWidth
                                         : 9 / 16
    readonly property int previewHeight: Math.round(previewWidth * Math.min(1, Math.max(9 / 32, previewRatio)))

    Column {
        id: layout
        x: root.sideMargin
        width: root.width - root.sideMargin * 2
        spacing: 0

        // A: title row.
        Item {
            width: parent.width
            height: 40
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space4
                text: qsTr("录制")
                font: Theme.h1Font
                color: Theme.textPrimary
            }
            Item {
                // §3.2.2 A: "{resolution} · {encoder}（硬件/软件）", encoder detail tooltip.
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space4
                width: encoderText.implicitWidth
                height: 16

                Text {
                    id: encoderText
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignRight
                    text: {
                        const encoder = RecorderView.selectedVideoEncoder
                        if (encoder.length === 0)
                            return qsTr("编码器：待定")
                        const tag = RecorderView.selectedVideoEncoderIsHardware ? qsTr("硬件") : qsTr("软件")
                        return RecorderView.videoResolution + " · " + encoder + "（" + tag + "）"
                    }
                    font: Theme.monoSmallFont
                    color: RecorderView.selectedVideoEncoder.length === 0 ? Theme.textTertiary
                         : RecorderView.selectedVideoEncoderIsHardware ? Theme.successFg
                         : Theme.textTertiary

                    HoverHandler {
                        id: encoderHover
                    }
                    AppToolTip {
                        visible: encoderHover.hovered
                             && RecorderView.selectedVideoEncoderDetail.length > 0
                        text: RecorderView.selectedVideoEncoderDetail
                    }
                }
            }
        }

        Item { width: 1; height: Theme.space16 }

        // B + D: preview column and source panel side by side.
        Row {
            spacing: root.gap

            Column {
                spacing: Theme.space16
                PreviewPanel {
                    id: previewPanel
                    width: root.previewWidth
                    height: root.previewHeight
                }
                ControlBar {
                    width: root.previewWidth
                }
            }

            SourcePanel {
                width: root.sourcePanelWidth
                height: Math.max(root.previewHeight + Theme.space16 + 104, 534)
            }
        }

        // E: recent recordings.
        Item { width: 1; height: Theme.space16 }
        RecentRecordings {
            width: parent.width
        }
        Item { width: 1; height: Theme.space24 }
    }
}
