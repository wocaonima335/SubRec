import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.10 tooltip: 12 px white on Theme.tooltipBg, radius 4,
// padding 6/8, max width 280, 500 ms delay, 6 px from the target.
// FROZEN API: as ToolTip (text, visible). Attach as a child: AppToolTip { visible: ma.containsMouse; text: … }
ToolTip {
    id: root
    delay: 500
    font: Theme.captionFont
    // TODO(U2): background/contentItem, max width 280.
}
