import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: window
    visible: false
    flags: Qt.FramelessWindowHint
    color: theme.background
    title: "Omaframe selection"
    Shortcut {sequence: "Escape"; enabled: window.visible; onActivated: studio.cancelSelection()}
    Shortcut {sequence: "Tab"; enabled: window.visible && !window.dragging; onActivated: window.toggleMode()}
    property string monitorName: ""
    property real startX: 0
    property real startY: 0
    property real endX: 0
    property real endY: 0
    property bool dragging: false
    property real sx: Math.min(startX, endX)
    property real sy: Math.min(startY, endY)
    property real sw: Math.abs(endX - startX)
    property real sh: Math.abs(endY - startY)
    // Screenshots use the theme accent; recordings use the color the Omarchy
    // bar gives its own recording indicator.
    readonly property color mark: studio.recordingSelection ? theme.recording : theme.popupFrame
    readonly property color dim: theme.alpha(theme.background, 0.55)
    function toggleMode() {
        if (studio.recordingSelection)
            studio.useScreenshotSelection();
        else
            studio.recordInstead(window.monitorName);
    }
    onVisibleChanged: {
        dragging = false;
        startX = 0;
        startY = 0;
        endX = 0;
        endY = 0;
        if (visible)
            area.forceActiveFocus();
    }
    Image {
        anchors.fill: parent
        source: window.visible ? "image://frames/capture/" + window.monitorName + "?" + studio.revision : ""
        cache: false
        fillMode: Image.Stretch
    }
    Rectangle {
        x: 0
        y: 0
        width: parent.width
        height: window.sy
        color: window.dim
    }
    Rectangle {
        x: 0
        y: window.sy
        width: window.sx
        height: window.sh
        color: window.dim
    }
    Rectangle {
        x: window.sx + window.sw
        y: window.sy
        width: parent.width - x
        height: window.sh
        color: window.dim
    }
    Rectangle {
        x: 0
        y: window.sy + window.sh
        width: parent.width
        height: parent.height - y
        color: window.dim
    }
    Rectangle {
        x: window.sx - border.width
        y: window.sy - border.width
        width: window.sw + border.width * 2
        height: window.sh + border.width * 2
        color: "transparent"
        border.color: window.mark
        border.width: 2
        visible: window.dragging
    }
    // Live size beside the selection, flipped inside the screen near edges.
    Rectangle {
        id: sizeChip
        visible: window.dragging && window.sw > 0 && window.sh > 0
        readonly property real below: window.sy + window.sh + 8
        x: Math.max(4, Math.min(window.sx + window.sw - width, window.width - width - 4))
        y: below + height + 4 < window.height ? below : Math.max(4, window.sy - height - 8)
        width: sizeText.implicitWidth + 16
        height: 24
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        border.width: 1
        border.color: window.mark
        Text {
            id: sizeText
            anchors.centerIn: parent
            text: Math.round(window.sw) + " × " + Math.round(window.sh)
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: 12
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        focus: true
        Keys.onEscapePressed: studio.cancelSelection()
        onPressed: function (mouse) {
            area.forceActiveFocus();
            window.startX = mouse.x;
            window.startY = mouse.y;
            window.endX = mouse.x;
            window.endY = mouse.y;
            window.dragging = true;
        }
        onPositionChanged: function (mouse) {
            if (pressed) {
                window.endX = Math.max(0, Math.min(width, mouse.x));
                window.endY = Math.max(0, Math.min(height, mouse.y));
            }
        }
        onReleased: function (mouse) {
            studio.finishSelection(window.monitorName, window.startX / width, window.startY / height, window.endX / width, window.endY / height);
            window.dragging = false;
        }
    }

    component Keycap: Rectangle {
        property string key
        implicitWidth: Math.max(implicitHeight, keyLabel.implicitWidth + 12)
        implicitHeight: 22
        radius: theme.radius
        color: theme.controlFill
        border.width: 1
        border.color: theme.controlBorder
        Text {
            id: keyLabel
            anchors.centerIn: parent
            text: parent.key
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
    }

    component ModeButton: Rectangle {
        id: mode
        property string label
        property string glyph
        property bool chosen
        property color markColor: theme.selectedText
        signal activated()
        Layout.fillHeight: true
        implicitWidth: modeRow.implicitWidth + 24
        radius: theme.radius
        color: modeMouse.pressed ? theme.pressedFill : chosen ? theme.selectedFill : modeMouse.containsMouse ? theme.hoverFill : "transparent"
        Accessible.role: Accessible.RadioButton
        Accessible.name: label
        Accessible.checked: chosen
        Behavior on color { ColorAnimation { duration: 90 } }
        RowLayout {
            id: modeRow
            anchors.centerIn: parent
            spacing: 8
            Glyph {
                name: mode.glyph
                ink: mode.chosen ? mode.markColor : theme.muted
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
            Text {
                text: mode.label
                color: mode.chosen ? theme.text : theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 13
            }
        }
        MouseArea {
            id: modeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mode.activated()
        }
    }

    // The capture bar: an Omarchy popup, framed in the active-border color.
    Rectangle {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 36
        width: Math.min(barRow.implicitWidth + 16, window.width - 16)
        clip: true
        // Narrow or high-scale displays drop the key hints, then the prompt,
        // so the mode switch always fits on screen.
        readonly property bool showHints: window.width >= 720
        readonly property bool showPrompt: window.width >= 470
        height: 48
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        border.width: 2
        border.color: window.mark
        Behavior on border.color { ColorAnimation { duration: 120 } }
        // Clicks on the bar never start a selection underneath it.
        MouseArea {anchors.fill: parent}
        RowLayout {
            id: barRow
            anchors.fill: parent
            anchors.margins: 7
            spacing: 6
            ModeButton {
                label: "Screenshot"
                glyph: "capture"
                chosen: !studio.recordingSelection
                onActivated: if (studio.recordingSelection) studio.useScreenshotSelection()
            }
            ModeButton {
                label: "Video"
                glyph: "record"
                chosen: studio.recordingSelection
                markColor: theme.recording
                onActivated: if (!studio.recordingSelection) studio.recordInstead(window.monitorName)
            }
            Rectangle {visible: bar.showPrompt; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; Layout.leftMargin: 6; Layout.rightMargin: 6; width: 1; color: theme.separator}
            Text {
                visible: bar.showPrompt
                Layout.minimumWidth: 200
                text: studio.recordingSelection ? "Drag an area to record" : "Drag an area to capture"
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: 13
            }
            Rectangle {visible: bar.showHints; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; Layout.leftMargin: 6; Layout.rightMargin: 6; width: 1; color: theme.separator}
            Keycap {visible: bar.showHints; key: "Tab"}
            Text {visible: bar.showHints; text: "Mode"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 8}
            Keycap {
                visible: bar.showHints
                key: "Esc"
                MouseArea {anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: studio.cancelSelection()}
            }
            Text {visible: bar.showHints; text: "Cancel"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 6}
        }
    }
    onClosing: function (close) {
        if (visible) {
            close.accepted = false;
            studio.cancelSelection();
        }
    }
}
