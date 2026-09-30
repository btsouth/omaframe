import QtQuick

// Passive help below a bar. Unlike a Popup, this never grabs mouse input,
// takes focus or moves back over a button when a window is short.
Rectangle {
    id: hint
    property string text: ""
    property bool ready: false
    property real maximumWidth: 640
    readonly property real maximumHeight: 40
    width: Math.min(maximumWidth, Math.ceil(metrics.width) + 24)
    height: Math.min(maximumHeight, label.implicitHeight + 12)
    visible: ready && text.length > 0
    radius: theme.radius
    color: theme.alpha(theme.background, 1)
    border.width: 1
    border.color: theme.controlBorder
    onTextChanged: {
        delay.stop();
        ready = false;
        if (text.length > 0) delay.start();
    }
    Timer { id: delay; interval: 500; onTriggered: hint.ready = true }
    TextMetrics { id: metrics; text: hint.text; font: label.font }
    Text {
        id: label
        anchors.fill: parent
        anchors.margins: 6
        text: hint.text
        color: theme.muted
        font.family: theme.fontFamily
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }
}
