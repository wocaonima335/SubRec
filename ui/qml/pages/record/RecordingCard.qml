import QtQuick

// Module U5 (ZCode). One recent-recording card, 336×72 (§3.2.2 E).
// FROZEN API (filled from RecordingListModel roles): fileName, filePath, modifiedText,
// sizeText, subtitleState, subtitleProgress. Hover shows play (DesktopActions.openFile) and
// reveal (DesktopActions.revealInFolder) instead of the subtitle chip; double-click plays.
Card {
    id: root
    property string fileName: ""
    property string filePath: ""
    property string modifiedText: ""
    property string sizeText: ""
    property string subtitleState: "none"
    property int subtitleProgress: 0

    implicitWidth: 336
    implicitHeight: 72

    // TODO(U5): icon tile, texts, chip, hover actions, double-click.
}
