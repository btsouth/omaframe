import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Omarchy's button: flat, theme-rounded, 1px control border, and fills from
// the theme's [controls] states. `primary` is the one accent-filled action
// per surface; `danger` fills a confirmed destructive action with the urgent
// color; `selected` is the persistent chosen state; `quiet` drops the idle
// chrome for secondary icon actions.
Button {
    id: control
    property string glyph: ""
    property bool primary: false
    property bool danger: false
    property bool quiet: false
    property bool selected: false
    property string hint: ""
    property bool tooltipEnabled: true
    implicitHeight: 38
    implicitWidth: content.implicitWidth + 26
    hoverEnabled: true
    padding: 11
    font.family: theme.fontFamily
    font.pixelSize: 13
    Accessible.name: text.length ? text : hint
    ToolTip.visible: tooltipEnabled && hovered && !down && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 600
    opacity: enabled ? 1 : 0.38
    readonly property color ink: danger ? theme.readableOn(theme.urgent) : primary ? theme.onAccent : selected ? theme.selectedText : quiet && !hovered ? theme.muted : theme.text
    background: Rectangle {
        radius: theme.radius
        color: control.danger ? (control.down ? theme.mix(theme.urgent, theme.background, 0.18) : control.hovered ? theme.mix(theme.urgent, theme.text, 0.14) : theme.urgent)
             : control.primary ? (control.down ? theme.mix(theme.accent, theme.background, 0.18) : control.hovered ? theme.mix(theme.accent, theme.text, 0.14) : theme.accent)
             : control.down ? theme.pressedFill : control.selected ? theme.selectedFill : control.hovered ? theme.hoverFill : control.quiet ? "transparent" : theme.controlFill
        border.width: control.visualFocus ? 2 : control.primary || control.danger || control.selected || (control.quiet && !control.hovered) ? 0 : 1
        border.color: control.visualFocus ? theme.focusBorder : control.hovered ? theme.hoverBorder : theme.controlBorder
        Behavior on color { ColorAnimation { duration: 90 } }
    }
    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 8
            Glyph {
                visible: control.glyph.length > 0
                name: control.glyph
                ink: control.ink
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
            }
            Text {
                visible: control.text.length > 0
                text: control.text
                color: control.ink
                font.family: control.font.family
                font.pixelSize: control.font.pixelSize
                font.weight: control.primary || control.danger ? Font.DemiBold : Font.Normal
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
