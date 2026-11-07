// Overlay (the modal scrim attached property) is registered by the controls plugin at
// runtime; qmllint cannot resolve it from the module-resource mapping. Only these two
// categories are disabled for this file; everything else in it is still linted.
// qmllint disable unqualified unresolved-type
import QtQuick
import QtQuick.Controls.Basic

// Module U2 (ZCode). §2.5.11 modal dialog inside the main window (never a separate window,
// so capture exclusion covers it automatically).
// FROZEN API:
//   title, dialogWidth (440 confirm / 560 licence)
//   primaryText, primaryKind ("primary" | "danger"), primaryEnabled
//   secondaryText ("" hides), showCloseButton (top-right 32×32, same as cancel)
//   default property `content`: body between title (+12) and buttons (+24)
//   signals accepted() (primary), rejected() (secondary / close icon / Esc)
//   Enter triggers primary unless primaryKind === "danger".
// Open/close with open()/close() as any Popup.
Popup {
    id: root
    property string title: ""
    property int dialogWidth: 440
    property string primaryText: "确定"
    property string primaryKind: "primary"
    property bool primaryEnabled: true
    property string secondaryText: "取消"
    property bool showCloseButton: true
    default property alias content: body.data
    signal accepted()
    signal rejected()

    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: dialogWidth
    height: Math.min(dialogColumn.implicitHeight + topPadding + bottomPadding,
                     parent ? parent.height - 48 : 800)
    // Hosts (ShellDialogs / ContentDialogs) fill the window, so `parent` centres on it.
    anchors.centerIn: parent
    padding: Theme.space24

    Overlay.modal: Rectangle {
        color: Theme.bgScrim
    }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.bgSurface
        border.width: 1
        border.color: Theme.borderDefault
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durationDialog; easing.type: Easing.OutCubic }
            ScaleAnimator { from: 0.96; to: 1.0; duration: Theme.durationDialog; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durationDialog; easing.type: Easing.OutCubic }
            ScaleAnimator { from: 1.0; to: 0.96; duration: Theme.durationDialog; easing.type: Easing.OutCubic }
        }
    }

    function accept() {
        if (primaryEnabled) {
            close()
            accepted()
        }
    }
    function reject() {
        close()
        rejected()
    }

    contentItem: Column {
        id: dialogColumn
        spacing: Theme.space12

        // Esc cancels (§2.5.11). Danger primaries ignore Enter. Popup is not an Item,
        // so key handling lives on the focused content item.
        Keys.onEscapePressed: root.reject()
        Keys.onReturnPressed: if (root.primaryKind !== "danger") root.accept()
        Keys.onEnterPressed: if (root.primaryKind !== "danger") root.accept()

        Item {
            width: parent.width
            height: 24

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - (root.showCloseButton ? 44 : 0)
                text: root.title
                font: Theme.h2Font
                color: Theme.textPrimary
                elide: Text.ElideRight
            }

            IconButton {
                visible: root.showCloseButton
                anchors.right: parent.right
                anchors.rightMargin: -Theme.space12
                anchors.verticalCenter: parent.verticalCenter
                iconName: "x"
                onClicked: root.reject()
            }
        }

        // Body: the owner's content sets its own sizes; height follows the children.
        Item {
            id: body
            width: parent.width
            height: childrenRect.height
        }

        // §2.5.11: buttons right-aligned, primary outermost, 8 px apart, +24 under the body.
        Item {
            width: parent.width
            height: Theme.space24 + Theme.controlHeight

            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: Theme.space8

                AppButton {
                    visible: root.secondaryText.length > 0
                    text: root.secondaryText
                    onClicked: root.reject()
                }
                AppButton {
                    variant: root.primaryKind === "danger" ? "danger" : "primary"
                    enabled: root.primaryEnabled
                    text: root.primaryText
                    onClicked: root.accept()
                }
            }
        }
    }
}
