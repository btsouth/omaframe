import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic

Window {
    id: badge
    width: 340
    height: 64
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    title: "Omaframe screenshot countdown"
    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        border.width: 2
        border.color: theme.accent
        Column {
            x: 14
            y: 12
            spacing: 4
            Text {
                text: "Screenshot in " + studio.delayRemaining
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: 14
            }
            Text {
                width: 242
                elide: Text.ElideRight
                text: {
                    const keys = [];
                    if (shortcuts.screenshotKey.length) keys.push(shortcuts.screenshotKey);
                    if (shortcuts.delayKey.length) keys.push(shortcuts.delayKey);
                    return keys.length ? keys.join(" or ") + " cancels" : "Cancel to return";
                }
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
            }
        }
        StudioButton {
            objectName: "cancelScreenshotDelay"
            x: 270
            y: 16
            width: 60
            height: 32
            implicitWidth: 60
            implicitHeight: 32
            text: "Cancel"
            focusPolicy: Qt.NoFocus
            tooltipEnabled: false
            Accessible.name: "Cancel screenshot countdown"
            onClicked: studio.cancelDelayedCapture()
        }
    }
}
