import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A themed whole-number input with step buttons. `committed` fires with a
// clamped value when a step button is pressed or typing is finished.
RowLayout {
    id: field
    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1
    property string suffix: ""
    property alias inputFocus: input.activeFocus
    signal committed(int value)
    spacing: 0
    function commit(next) {
        const clamped = Math.max(from, Math.min(to, Math.round(next)));
        committed(clamped);
        input.text = Qt.binding(() => field.value + "");
    }
    component StepButton: Button {
        id: stepButton
        property string sign
        implicitWidth: 30
        implicitHeight: 30
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        autoRepeat: true
        Accessible.name: sign === "+" ? "Increase" : "Decrease"
        background: Rectangle {
            radius: theme.radius
            color: stepButton.down ? theme.pressedFill : stepButton.hovered ? theme.hoverFill : theme.controlFill
            border.width: 1
            border.color: stepButton.hovered ? theme.hoverBorder : theme.controlBorder
        }
        contentItem: Text {
            text: stepButton.sign === "+" ? "+" : "−"
            color: stepButton.enabled ? theme.text : theme.faint
            font.family: theme.fontFamily
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    StepButton {
        sign: "−"
        enabled: field.enabled && field.value > field.from
        onClicked: field.commit(field.value - field.step)
    }
    TextField {
        id: input
        Layout.preferredWidth: 64
        Layout.preferredHeight: 30
        text: field.value + ""
        horizontalAlignment: TextInput.AlignHCenter
        validator: IntValidator { bottom: field.from; top: field.to }
        selectByMouse: true
        color: theme.text
        selectionColor: theme.alpha(theme.accent, 0.4)
        selectedTextColor: theme.text
        font.family: theme.fontFamily
        font.pixelSize: 12
        rightPadding: field.suffix.length ? 22 : 6
        onActiveFocusChanged: if (activeFocus) selectAll()
        onEditingFinished: field.commit(text.trim().length && Number.isFinite(Number(text)) ? Number(text) : field.value)
        Keys.onEscapePressed: { field.commit(field.value); focus = false; }
        Keys.onUpPressed: field.commit(field.value + field.step)
        Keys.onDownPressed: field.commit(field.value - field.step)
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            text: field.suffix
            visible: field.suffix.length > 0
            color: theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
        background: Rectangle {
            color: theme.well
            border.width: input.activeFocus ? 2 : 1
            border.color: input.activeFocus ? theme.focusBorder : theme.controlBorder
        }
    }
    StepButton {
        sign: "+"
        enabled: field.enabled && field.value < field.to
        onClicked: field.commit(field.value + field.step)
    }
}
