import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A themed confirmation for actions that remove something. The confirm
// button names the action instead of saying OK.
Popup {
    id: dialog
    property string title: ""
    property string message: ""
    property string confirmText: "Delete"
    property bool destructive: true
    signal confirmed()
    anchors.centerIn: Overlay.overlay
    width: Math.min(440, parent ? parent.width - 48 : 440)
    padding: 22
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: cancelButton.forceActiveFocus()
    Overlay.modal: Rectangle { color: theme.scrim }
    background: Rectangle {
        color: theme.alpha(theme.background, 1)
        radius: theme.radius
        border.width: 2
        border.color: theme.frame
    }
    contentItem: ColumnLayout {
        spacing: 14
        Text {
            Layout.fillWidth: true
            text: dialog.title
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: 16
            font.weight: Font.Medium
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: dialog.message
            color: theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 12
            wrapMode: Text.Wrap
            lineHeight: 1.25
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            Item { Layout.fillWidth: true }
            StudioButton {
                id: cancelButton
                text: "Cancel"
                quiet: true
                onClicked: dialog.close()
            }
            StudioButton {
                text: dialog.confirmText
                primary: !dialog.destructive
                danger: dialog.destructive
                onClicked: {
                    dialog.close();
                    dialog.confirmed();
                }
            }
        }
    }
}
