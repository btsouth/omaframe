import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Quick colors first; a visual picker and hex entry are revealed on request.
ColumnLayout {
    id: choice
    property string title: "Color"
    property color value: "#ffffff"
    property bool custom: false
    property real hue: Math.max(0, value.hsvHue)
    property real saturation: value.hsvSaturation
    property real brightness: value.hsvValue
    readonly property color candidate: Qt.hsva(hue, saturation, brightness, 1)
    readonly property color preview: squareMouse.pressed || hueSlider.pressed ? candidate : value
    signal chosen(color color)
    spacing: 7
    function restore() {
        hue = Math.max(0, value.hsvHue);
        saturation = value.hsvSaturation;
        brightness = value.hsvValue;
    }
    onValueChanged: restore()
    Text { text: choice.title; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 12 }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [
                { color: "#ffffff", name: "White" }, { color: "#151a20", name: "Dark" },
                { color: "#e75439", name: "Red" }, { color: "#eab841", name: "Gold" },
                { color: "#459ec7", name: "Blue" }, { color: "#4ca782", name: "Green" }
            ]
            Button {
                id: swatch
                required property var modelData
                Layout.preferredWidth: 24
                Layout.preferredHeight: 26
                hoverEnabled: true
                Accessible.name: choice.title + ": " + modelData.name
                readonly property bool picked: Qt.colorEqual(choice.value, modelData.color)
                onClicked: choice.chosen(modelData.color)
                background: Rectangle {
                    radius: theme.radius > 0 ? 4 : 0
                    color: swatch.modelData.color
                    border.width: swatch.activeFocus || swatch.picked ? 2 : 1
                    border.color: swatch.activeFocus ? theme.focusBorder : swatch.picked ? theme.accent : theme.controlBorder
                }
                contentItem: Glyph {
                    name: "check"
                    visible: swatch.picked
                    ink: theme.readableOn(swatch.modelData.color)
                }
                ToolTip.visible: hovered && !down
                ToolTip.delay: 600
                ToolTip.text: modelData.name
            }
        }
        Item { Layout.fillWidth: true }
        StudioButton {
            text: "Custom"
            implicitHeight: 28
            padding: 7
            font.pixelSize: 11
            selected: choice.custom
            quiet: !selected
            onClicked: { choice.restore(); choice.custom = !choice.custom; }
        }
    }
    ColumnLayout {
        visible: choice.custom
        Layout.fillWidth: true
        spacing: 8
        Rectangle {
            id: square
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            clip: true
            color: Qt.hsva(choice.hue, 1, 1, 1)
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#ffffff" }
                    GradientStop { position: 1; color: "#00ffffff" }
                }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00000000" }
                    GradientStop { position: 1; color: "#000000" }
                }
            }
            Rectangle {
                x: Math.max(0, Math.min(square.width - width, choice.saturation * square.width - width/2))
                y: Math.max(0, Math.min(square.height - height, (1-choice.brightness) * square.height - height/2))
                width: 12; height: 12; radius: 6
                color: "transparent"
                border.width: 2; border.color: "#ffffff"
                Rectangle { anchors.fill: parent; anchors.margins: 2; color: "transparent"; radius: 4; border.width: 1; border.color: "#151a20" }
            }
            MouseArea {
                id: squareMouse
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                function pick(mouse) {
                    choice.saturation = Math.max(0, Math.min(1, mouse.x/width));
                    choice.brightness = 1-Math.max(0, Math.min(1, mouse.y/height));
                }
                onPressed: mouse => pick(mouse)
                onPositionChanged: mouse => { if (pressed) pick(mouse); }
                onReleased: mouse => { pick(mouse); choice.chosen(choice.candidate); }
                onCanceled: choice.restore()
            }
        }
        ThemedSlider {
            id: hueSlider
            Layout.fillWidth: true
            Accessible.name: choice.title + " hue"
            from: 0; to: 1; stepSize: 0.01
            value: choice.hue
            onMoved: choice.hue = value
            onCommitted: value => { choice.hue = value; choice.chosen(choice.candidate); }
            background: Rectangle {
                x: hueSlider.leftPadding
                y: hueSlider.topPadding + hueSlider.availableHeight/2 - height/2
                width: hueSlider.availableWidth; height: 8
                radius: theme.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.5; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1; color: "#ff0000" }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Hex color"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 11 }
            TextField {
                id: hex
                Layout.preferredWidth: 102
                Layout.preferredHeight: 30
                Accessible.name: choice.title + " hex color"
                text: choice.value.toString()
                maximumLength: 7
                selectByMouse: true
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: 12
                readonly property bool validHex: /^#[0-9a-fA-F]{6}$/.test(text)
                onEditingFinished: {
                    if (validHex) choice.chosen(text);
                    text = Qt.binding(() => choice.value.toString());
                }
                Keys.onEscapePressed: { text = Qt.binding(() => choice.value.toString()); focus = false; }
                background: Rectangle {
                    color: theme.well
                    radius: theme.radius
                    border.width: hex.activeFocus ? 2 : 1
                    border.color: !hex.validHex ? theme.urgent : hex.activeFocus ? theme.focusBorder : theme.controlBorder
                }
            }
        }
    }
}
