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
    // The lower 48 px are reserved for passive hints outside the video.
    height: 96
    visible: false
    onVisibleChanged: audioLevels.setSurface("control", "control", visible)
    Component.onDestruction: audioLevels.setSurface("control", "control", false)
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
    readonly property bool pauseError: recorder.status.indexOf("Could not") === 0
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 48
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
                          : recorder.canForceStop ? "Still finishing…"
                          : recorder.state === "stopping" ? "Saving…"
                          : recorder.elapsed
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.family: theme.fontFamily
                    color: theme.text
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
                tooltipEnabled: false
                Accessible.description: hint
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
                text: recorder.canForceStop ? "Force" : recorder.state === "countdown" ? "Cancel" : "Stop"
                enabled: recorder.state !== "stopping" || recorder.canForceStop
                implicitWidth: 60; implicitHeight: 32
                hoverEnabled: true
                opacity: enabled ? 1 : 0.45
                onClicked: recorder.stop()
                Accessible.name: recorder.canForceStop ? "Force-stop recording" : recorder.state === "countdown" ? "Cancel recording" : "Stop recording"
                Accessible.description: recorder.canForceStop ? "The file may be incomplete" : recorder.stopKey.length ? recorder.stopKey + " also stops" : ""
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
    Column {
        visible: control.visible && control.live && lowerHint.text.length === 0
        x: 8; y: 52; width: parent.width - 16; spacing: 0
        AudioMeter { width: parent.width; channel: audioLevels.microphone }
        AudioMeter { width: parent.width; label: "Computer sound"; channel: audioLevels.sound }
    }
    BarHint {
        id: lowerHint
        anchors.horizontalCenter: parent.horizontalCenter
        y: 54
        maximumWidth: control.width
        text: !control.visible || control.countdownOnly ? ""
            : recorder.canForceStop ? "Force-stop may leave an incomplete file"
            : recorder.status.indexOf("Recording · camera unavailable") === 0 ? "Camera unavailable · screen-only"
            : control.pauseError ? recorder.status
            : recorder.pausePending ? "Waiting for recorder…"
            : pause.hovered && !pause.down ? pause.hint : ""
    }
}
