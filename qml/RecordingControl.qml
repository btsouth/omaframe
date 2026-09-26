import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: control
    width: 280
    height: 56
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    title: "Omaframe recording controls"
    onClosing: function(event) {if(visible && recorder.active) {event.accepted=false;recorder.stop()}}
    readonly property bool live: recorder.state === "recording"
    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        border.width: 2
        border.color: theme.recording
        RowLayout {
            anchors.fill: parent
            anchors.margins: 9
            spacing: 10
            Rectangle {
                id: dot
                width: 10; height: 10
                radius: theme.radius > 0 ? 5 : 0
                color: theme.recording
                Layout.leftMargin: 6
                // A slow pulse while frames are being written.
                SequentialAnimation on opacity {
                    running: control.live && control.visible
                    loops: Animation.Infinite
                    NumberAnimation {to: 0.35; duration: 700; easing.type: Easing.InOutSine}
                    NumberAnimation {to: 1; duration: 700; easing.type: Easing.InOutSine}
                    onStopped: dot.opacity = 1
                }
            }
            Text {
                Layout.fillWidth: true
                text: recorder.state==="countdown" ? "Starting in "+recorder.remaining : recorder.state==="starting" ? "Starting…" : recorder.state==="stopping" ? "Saving…" : recorder.elapsed
                font.pixelSize: 14
                font.family: theme.fontFamily
                color: theme.text
            }
            Button {
                id: stop
                text: recorder.state==="countdown" ? "Cancel" : "Stop"
                enabled: recorder.state!=="stopping"
                implicitWidth: 96; implicitHeight: 36
                hoverEnabled: true
                onClicked: recorder.stop()
                Accessible.name: text
                readonly property color fill: stop.down ? theme.mix(theme.recording, theme.background, 0.2) : stop.hovered ? theme.mix(theme.recording, theme.text, 0.14) : theme.recording
                background: Rectangle {radius: theme.radius; color: stop.fill}
                contentItem: RowLayout {
                    spacing: 7
                    Item {Layout.fillWidth: true}
                    Rectangle {visible: recorder.state!=="countdown"; width: 9; height: 9; color: theme.readableOn(stop.fill)}
                    Text {text: stop.text; color: theme.readableOn(stop.fill); font.family: theme.fontFamily; font.bold: true; font.pixelSize: 13}
                    Item {Layout.fillWidth: true}
                }
            }
        }
    }
}
