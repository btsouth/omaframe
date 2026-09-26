import QtQuick
import QtQuick.Controls.Basic

// Omarchy dropdown: control chrome at rest, a framed theme popup when open.
ComboBox {
    id: control
    font.family: theme.fontFamily
    font.pixelSize: 12
    leftPadding: 11
    rightPadding: 28
    implicitHeight: 36
    hoverEnabled: true
    contentItem: Text {
        text: control.displayText
        color: control.enabled ? theme.text : theme.faint
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: theme.radius
        color: control.down ? theme.pressedFill : control.hovered ? theme.hoverFill : theme.controlFill
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? theme.focusBorder : control.hovered ? theme.hoverBorder : theme.controlBorder
    }
    indicator: Glyph {
        x: control.width - width - 8
        y: (control.height - height) / 2
        name: "chevron"
        width: 15
        height: 15
        ink: theme.muted
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 4
        implicitHeight: Math.min(list.contentHeight + 8, 320)
        background: Rectangle {
            color: theme.alpha(theme.background, 1)
            radius: theme.radius
            border.width: 2
            border.color: theme.frame
        }
        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
    }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: control.width - 8
        height: 32
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: typeof option.modelData === "object" && option.modelData !== null && control.textRole ? option.modelData[control.textRole] : option.modelData
            color: control.currentIndex === option.index ? theme.selectedText : theme.text
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: theme.radius
            color: option.highlighted || option.hovered ? theme.hoverFill : control.currentIndex === option.index ? theme.selectedFill : "transparent"
        }
    }
}
