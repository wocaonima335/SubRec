import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.10 tooltip: 12 px white on Theme.tooltipBg, radius 4,
// padding 6/8, max width 280, 500 ms delay, 6 px from the target.
// FROZEN API: as ToolTip (text, visible). Attach as a child: AppToolTip { visible: ma.containsMouse; text: … }
ToolTip {
    id: root
    delay: 500
    font: Theme.captionFont

    padding: 6
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    // §2.5.10: 6 px below the target (ToolTip centres above its parent by default).
    y: 6

    background: Rectangle {
        radius: Theme.radiusXs
        color: Theme.tooltipBg
    }

    contentItem: Text {
        // §2.5.10: max width 280, wrap beyond that.
        width: Math.min(root.implicitContentWidth, 280 - root.leftPadding - root.rightPadding)
        text: root.text
        font: root.font
        color: "#FFFFFF"
        wrapMode: Text.Wrap
    }
}
