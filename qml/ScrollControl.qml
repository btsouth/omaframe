import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The progress control for a scrolling capture. Studio and main.cpp place it
// outside the captured area when a display had room, or over the top of it,
// where Plan.coverTop keeps its rows out of the stitch (they come from the
// first frame, taken before this appears). Done or Enter keeps everything
// captured so far; Escape also keeps it. Cancel throws it away.
Window {
    id: control
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
    flags: Qt.FramelessWindowHint
    title: "Omaframe scrolling capture"
    readonly property var capture: studio.scrollCapture
    readonly property bool live: capture !== null && capture.active
    readonly property string headline: !live ? "Done"
        : capture.mode === "starting" ? "Starting…"
        : capture.mode === "finishing" ? "Finishing…"
        : capture.mode === "manual" ? "Scroll by hand" : "Scrolling"
    readonly property string progress: live && capture.length > 0 ? capture.length + " px so far" : ""
    readonly property string note: !live ? "The capture has ended."
        : capture.hint.length > 0 ? capture.hint
        : capture.mode === "manual" ? "Keep scrolling, then Done."
        : "Hold still while the page scrolls."
    onVisibleChanged: if (visible) keys.forceActiveFocus()
    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        border.width: 2
        border.color: control.capture !== null && control.capture.warning ? theme.urgent : theme.frame
        // Clicks inside the control never reach the page beneath it.
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 11
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: 9
                    height: 9
                    radius: width / 2
                    color: control.capture !== null && control.capture.warning ? theme.urgent : theme.accent
                    SequentialAnimation on opacity {
                        running: control.visible && control.live
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: 700; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                    }
                }
                Text {
                    text: control.headline
                    color: theme.text
                    font.family: theme.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Text {
                    Layout.fillWidth: true
                    text: control.progress
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                StudioButton {
                    text: "Done"
                    glyph: "check"
                    primary: true
                    implicitHeight: 28
                    hint: "Keep what has been captured · Enter"
                    Keys.forwardTo: [keys]
                    Keys.onReturnPressed: studio.scrollCapture.finish()
                    Keys.onEnterPressed: studio.scrollCapture.finish()
                    onClicked: studio.scrollCapture.finish()
                }
                StudioButton {
                    text: "Cancel"
                    glyph: "close"
                    quiet: true
                    implicitHeight: 28
                    hint: "Throw the capture away"
                    Keys.forwardTo: [keys]
                    Keys.onReturnPressed: studio.scrollCapture.cancel()
                    Keys.onEnterPressed: studio.scrollCapture.cancel()
                    onClicked: studio.scrollCapture.cancel()
                }
            }
            Text {
                Layout.fillWidth: true
                text: control.note
                color: control.capture !== null && control.capture.warning ? theme.urgent : theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onReturnPressed: function (event) {
            if (keys.activeFocus) studio.scrollCapture.finish();
            else event.accepted = false;
        }
        Keys.onEnterPressed: function (event) {
            if (keys.activeFocus) studio.scrollCapture.finish();
            else event.accepted = false;
        }
        Keys.onEscapePressed: studio.scrollCapture.finish()
        Keys.onSpacePressed: function (event) {
            if (keys.activeFocus) studio.scrollCapture.finish();
            else event.accepted = false;
        }
    }
    onClosing: function (event) {
        if (visible) {
            event.accepted = false;
            studio.scrollCapture.cancel();
        }
    }
}
