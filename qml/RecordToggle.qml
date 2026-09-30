import QtQuick
import QtQuick.Controls.Basic

// Omarchy's switch: selected fill when on, control border when off, pill when
// Hyprland corners are rounded and square when they are sharp.
CheckBox {
    id: control
    implicitHeight: Math.max(36, label.implicitHeight)
    implicitWidth: label.implicitWidth + 6
    padding: 0
    rightPadding: 6
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    font.family: theme.fontFamily
    font.pixelSize: 13
    indicator: Rectangle {
        id: track
        x: 0
        y: (control.height - height) / 2
        width: 40
        height: 22
        radius: theme.radius > 0 ? height / 2 : 0
        color: control.checked ? theme.selectedFill : control.hovered ? theme.hoverFill : theme.controlFill
        border.width: control.activeFocus ? 2 : control.checked ? 0 : 1
        border.color: control.activeFocus ? theme.focusBorder : control.hovered ? theme.hoverBorder : theme.controlBorder
        Behavior on color { ColorAnimation { duration: 120 } }
        Rectangle {
            width: 16
            height: 16
            radius: theme.radius > 0 ? height / 2 : 0
            x: control.checked ? track.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            color: control.checked ? theme.selectedText : theme.faint
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }
    contentItem: Text {
        id: label
        leftPadding: 52
        text: control.text
        wrapMode: Text.Wrap
        verticalAlignment: Text.AlignVCenter
        color: theme.text
        font: control.font
    }
}
