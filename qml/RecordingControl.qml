import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The timer, Pause and Stop buttons, placed outside the recorded area. When
// there is no such place, this only counts down, says how to stop, and is
// gone before the first frame is captured.
Window {
    id: control
    width: 232
    height: 48
    visible: false
    palette.window: theme.alpha(theme.background, 1)
    palette.windowText: theme.text
    palette.base: theme.well
    palette.text: theme.text
    palette.button: theme.controlFill
    palette.buttonText: theme.text
    palette.toolTipBase: theme.alpha(theme.background, 1)
    palette.toolTipText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: theme.onAccent
    palette.placeholderText: theme.faint
    palette.mid: theme.controlBorder
    palette.dark: theme.frame
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    title: "Omaframe recording controls"
    onClosing: function(event) {if(visible && recorder.active) {event.accepted=false;recorder.stop()}}
    readonly property bool live: recorder.state === "recording"
    readonly property bool paused: recorder.state === "paused"
    readonly property bool countdownOnly: recorder.countdownOnly
    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        border.width: 2
        border.color: theme.recording
        RowLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 6
            Rectangle {
                id: dot
                width: 8; height: 8
                radius: theme.radius > 0 ? 4 : 0
                color: control.paused ? theme.muted : theme.recording
                Layout.leftMargin: 4
                // A slow pulse while frames are being written.
                SequentialAnimation on opacity {
                    running: control.live && control.visible
                    loops: Animation.Infinite
                    NumberAnimation {to: 0.35; duration: 700; easing.type: Easing.InOutSine}
                    NumberAnimation {to: 1; duration: 700; easing.type: Easing.InOutSine}
                    onStopped: dot.opacity = 1
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: control.countdownOnly
                          ? "Recording in " + recorder.remaining
                          : recorder.state === "countdown" ? "Starting in " + recorder.remaining
                          : recorder.state === "starting" ? "Starting…"
                          : recorder.state === "stopping" ? "Saving…"
                          : recorder.elapsed
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.family: theme.fontFamily
                    color: theme.text
                    ToolTip.visible: !control.countdownOnly && recorder.status.indexOf("Could not") === 0
                    ToolTip.text: recorder.status
                    ToolTip.delay: 500
                }
                Text {
                    Layout.fillWidth: true
                    visible: control.countdownOnly || control.paused
                    text: control.paused ? "Paused" : recorder.stopKey + " stops" + (recorder.pauseKey.length ? " · " + recorder.pauseKey + " pauses" : "")
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            StudioButton {
                id: pause
                objectName: "recordingPause"
                visible: control.live || control.paused
                enabled: !recorder.pausePending
                hint: recorder.pausePending ? "Waiting for recorder…" : control.paused ? "Continue recording" : "Pause recording"
                implicitWidth: 74
                implicitHeight: 32
                padding: 7
                text: control.paused ? "Resume" : "Pause"
                contentItem: Text {
                    text: pause.text
                    color: pause.ink
                    font: pause.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: recorder.togglePause()
            }
            Button {
                id: stop
                text: recorder.state === "countdown" ? "Cancel" : "Stop"
                enabled: recorder.state !== "stopping"
                implicitWidth: 60; implicitHeight: 32
                hoverEnabled: true
                opacity: enabled ? 1 : 0.45
                onClicked: recorder.stop()
                Accessible.name: recorder.state === "countdown" ? "Cancel recording" : "Stop recording"
                Accessible.description: recorder.stopKey.length ? recorder.stopKey + " also stops" : ""
                readonly property color fill: stop.down ? theme.mix(theme.recording, theme.background, 0.2) : stop.hovered ? theme.mix(theme.recording, theme.text, 0.14) : theme.recording
                background: Rectangle {radius: theme.radius; color: stop.fill}
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {visible: recorder.state !== "countdown"; width: 9; height: 9; anchors.verticalCenter: parent.verticalCenter; color: theme.readableOn(stop.fill)}
                        Text {text: stop.text; color: theme.readableOn(stop.fill); font.family: theme.fontFamily; font.bold: true; font.pixelSize: 13}
                    }
                }
            }
        }
    }
}
