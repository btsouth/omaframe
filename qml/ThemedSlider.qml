import QtQuick
import QtQuick.Controls.Basic

// Omarchy slider: a thin theme track filled with the accent, and a small
// accent handle. `committed` fires once when a drag or key press ends.
Slider {
    id: control
    signal committed(real value)
    implicitHeight: 24
    hoverEnabled: true
    onPressedChanged: if (!pressed) committed(value)
    Keys.onReleased: function (event) {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown || event.key === Qt.Key_Home || event.key === Qt.Key_End)
            committed(value);
    }
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 3
        radius: theme.radius > 0 ? 2 : 0
        color: theme.controlBorder
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: theme.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: theme.radius > 0 ? 7 : 0
        color: control.pressed ? theme.mix(theme.accent, theme.text, 0.2) : theme.accent
        border.width: control.activeFocus || control.hovered ? 2 : 0
        border.color: theme.focusBorder
    }
}
