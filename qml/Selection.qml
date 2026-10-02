import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: window
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
    flags: Qt.FramelessWindowHint
    color: theme.background
    title: "Omaframe selection"
    Shortcut {sequence: "Escape"; enabled: window.visible; onActivated: studio.cancelSelection()}
    Shortcut {sequence: "Tab"; enabled: window.visible && !window.dragging; onActivated: window.toggleMode()}
    Shortcut {sequence: "S"; enabled: window.visible && !window.dragging; onActivated: window.toggleScroll()}
    Shortcut {sequence: "F"; enabled: window.visible && !window.dragging && !studio.scrollSelection; onActivated: window.wholeDisplay()}
    Shortcut {sequence: "D"; enabled: window.visible && !window.dragging && studio.recordingSelection; onActivated: recorder.desktopAudio = !recorder.desktopAudio}
    Shortcut {sequence: "M"; enabled: window.visible && !window.dragging && studio.recordingSelection; onActivated: recorder.micAudio = !recorder.micAudio}
    property string monitorName: ""
    property real startX: 0
    property real startY: 0
    property real endX: 0
    property real endY: 0
    property bool dragging: false
    property var hoveredTarget: null
    property var hintOwner: null
    property real sx: dragging || !hoveredTarget ? Math.min(startX, endX) : hoveredTarget.x * width
    property real sy: dragging || !hoveredTarget ? Math.min(startY, endY) : hoveredTarget.y * height
    property real sw: dragging || !hoveredTarget ? Math.abs(endX - startX) : hoveredTarget.w * width
    property real sh: dragging || !hoveredTarget ? Math.abs(endY - startY) : hoveredTarget.h * height
    function targetAt(px, py) {
        const x = px / width;
        const y = py / height;
        for (const target of studio.windowTargets) {
            if (target.monitor === monitorName && x >= target.x && y >= target.y &&
                    x < target.x + target.w && y < target.y + target.h)
                return target;
        }
        return null;
    }
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
    function toggleScroll() {
        if (studio.scrollSelection)
            studio.useScreenshotSelection();
        else
            studio.scrollInstead(window.monitorName);
    }
    // Scrolling captures only take a clicked window. A drawn area or a whole
    // display gives the stitcher too little to follow, or the wrong thing to
    // scroll.
    function finishScroll(target, clickX, clickY) {
        studio.finishScrollSelection(window.monitorName, target.x, target.y, target.x + target.w, target.y + target.h, clickX, clickY);
    }
    // Each display has its own selector, but only one gets the keyboard.
    // F records or captures the display the pointer is on.
    function wholeDisplay() {
        studio.finishSelection(studio.pointerMonitor.length ? studio.pointerMonitor : window.monitorName, 0, 0, 1, 1);
    }
    function cycleCountdown() {
        recorder.countdown = recorder.countdown === 0 ? 3 : recorder.countdown === 3 ? 5 : 0;
    }
    onVisibleChanged: {
        dragging = false;
        startX = 0;
        startY = 0;
        endX = 0;
        endY = 0;
        hoveredTarget = null;
        hintOwner = null;
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
        visible: window.dragging || window.hoveredTarget !== null
    }
    // Live size beside the selection, flipped inside the screen near edges.
    Rectangle {
        id: sizeChip
        visible: (window.dragging || window.hoveredTarget !== null) && window.sw > 0 && window.sh > 0
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
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        focus: true
        Keys.onEscapePressed: studio.cancelSelection()
        onPressed: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                studio.cancelSelection();
                return;
            }
            area.forceActiveFocus();
            if (studio.scrollSelection)
                return;
            window.startX = mouse.x;
            window.startY = mouse.y;
            window.endX = mouse.x;
            window.endY = mouse.y;
            window.dragging = true;
        }
        onEntered: studio.pointerMonitor = window.monitorName
        onPositionChanged: function (mouse) {
            if (studio.pointerMonitor !== window.monitorName)
                studio.pointerMonitor = window.monitorName;
            if (pressed && window.dragging) {
                window.endX = Math.max(0, Math.min(width, mouse.x));
                window.endY = Math.max(0, Math.min(height, mouse.y));
            } else
                window.hoveredTarget = window.targetAt(mouse.x, mouse.y);
        }
        onExited: if (!pressed) window.hoveredTarget = null
        onReleased: function (mouse) {
            if (mouse.button !== Qt.LeftButton)
                return;
            const dx = mouse.x - window.startX;
            const dy = mouse.y - window.startY;
            const target = window.targetAt(mouse.x, mouse.y);
            const cx = Math.max(0, Math.min(1, mouse.x / width));
            const cy = Math.max(0, Math.min(1, mouse.y / height));
            if (studio.scrollSelection) {
                if (target)
                    window.finishScroll(target, cx, cy);
                window.hoveredTarget = target;
                return;
            }
            if (dx * dx + dy * dy < 36) {
                if (target)
                    studio.finishSelection(window.monitorName, target.x, target.y, target.x + target.w, target.y + target.h);
                else
                    studio.finishSelection(window.monitorName, 0, 0, 1, 1);
            } else
                studio.finishSelection(window.monitorName, window.startX / width, window.startY / height, window.endX / width, window.endY / height);
            window.dragging = false;
            window.hoveredTarget = target;
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
        property bool compact: window.width < 560
        readonly property string hint: compact ? label : ""
        property color markColor: theme.selectedText
        signal activated()
        Layout.fillHeight: true
        implicitWidth: modeRow.implicitWidth + 24
        radius: theme.radius
        color: modeMouse.pressed ? theme.pressedFill : chosen ? theme.selectedFill : modeMouse.containsMouse ? theme.hoverFill : "transparent"
        Accessible.role: Accessible.RadioButton
        Accessible.name: label
        Accessible.checked: chosen
        Accessible.onPressAction: activated()
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
                visible: !mode.compact
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
            onEntered: window.hintOwner = mode
            onExited: if (window.hintOwner === mode) window.hintOwner = null
            onPressed: window.hintOwner = null
            onClicked: mode.activated()
        }
    }

    component BarToggle: Rectangle {
        id: toggle
        property string label
        property string glyph
        property bool on: false
        property bool checkable: true
        property string hint
        signal activated()
        Layout.fillHeight: true
        implicitWidth: toggleRow.implicitWidth + 18
        radius: theme.radius
        color: toggleMouse.pressed ? theme.pressedFill : on ? theme.selectedFill : toggleMouse.containsMouse ? theme.hoverFill : "transparent"
        border.width: on ? 0 : 1
        border.color: theme.controlBorder
        Accessible.role: checkable ? Accessible.CheckBox : Accessible.Button
        Accessible.name: label.length ? label : hint
        Accessible.checked: on
        Accessible.onPressAction: activated()
        RowLayout {
            id: toggleRow
            anchors.centerIn: parent
            spacing: 6
            Glyph { name: toggle.glyph; ink: toggle.on ? theme.selectedText : theme.muted; Layout.preferredWidth: 15; Layout.preferredHeight: 15 }
            Text { visible: toggle.label.length > 0; text: toggle.label; color: toggle.on ? theme.text : theme.muted; font.family: theme.fontFamily; font.pixelSize: 12 }
        }
        MouseArea {
            id: toggleMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: window.hintOwner = toggle
            onExited: if (window.hintOwner === toggle) window.hintOwner = null
            onPressed: window.hintOwner = null
            onClicked: toggle.activated()
        }
    }

    // The capture bar: an Omarchy popup, framed in the active-border color.
    // Video mode keeps the same selection and adds the recording options, so
    // a drag starts recording straight away.
    Rectangle {
        id: bar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 36
        width: Math.min(barRow.implicitWidth + 16, window.width - 16)
        clip: true
        // Narrow or high-scale displays drop the key hints, then the prompt,
        // so the mode switch always fits on screen.
        readonly property bool showHints: window.width >= (studio.recordingSelection ? 1500 : 1000)
        readonly property bool showPrompt: window.width >= (studio.recordingSelection ? 1240 : 720)
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
                chosen: !studio.recordingSelection && !studio.scrollSelection
                onActivated: if (studio.recordingSelection || studio.scrollSelection) studio.useScreenshotSelection()
            }
            ModeButton {
                label: "Video"
                glyph: "record"
                chosen: studio.recordingSelection
                markColor: theme.recording
                onActivated: if (!studio.recordingSelection) studio.recordInstead(window.monitorName)
            }
            ModeButton {
                label: "Scroll"
                glyph: "image"
                chosen: studio.scrollSelection
                onActivated: if (!studio.scrollSelection) studio.scrollInstead(window.monitorName)
            }
            Rectangle {visible: bar.showPrompt; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; Layout.leftMargin: 4; Layout.rightMargin: 4; width: 1; color: theme.separator}
            Text {
                visible: bar.showPrompt
                text: studio.recordingSelection ? "Click a window or drag an area to record"
                    : studio.scrollSelection ? "Click a window to scroll and stitch it"
                    : "Click a window or drag an area"
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: 13
            }
            Rectangle {visible: !studio.scrollSelection; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; Layout.leftMargin: 4; Layout.rightMargin: 4; width: 1; color: theme.separator}
            BarToggle {
                visible: !studio.scrollSelection
                label: window.width < 760 ? "" : "Whole display"
                glyph: "display"
                checkable: false
                hint: (studio.recordingSelection ? "Record" : "Capture") + " this entire display · F"
                onActivated: studio.finishSelection(window.monitorName, 0, 0, 1, 1)
            }
            BarToggle {
                visible: studio.recordingSelection
                label: window.width < 760 ? "" : "Sound"
                glyph: recorder.desktopAudio ? "volume" : "mute"
                on: recorder.desktopAudio
                hint: "Record what your computer plays · D"
                onActivated: recorder.desktopAudio = !recorder.desktopAudio
            }
            BarToggle {
                visible: studio.recordingSelection
                label: window.width < 760 ? "" : "Mic"
                glyph: "mic"
                on: recorder.micAudio
                hint: (recorder.micAudio && recorder.microphone >= 0 ? "Recording from " + recorder.microphones[recorder.microphone].label : "Record your microphone") + " · M"
                onActivated: recorder.micAudio = !recorder.micAudio
            }
            BarToggle {
                visible: studio.recordingSelection
                label: recorder.countdown === 0 ? "No delay" : recorder.countdown + " s"
                glyph: "timer"
                checkable: false
                on: recorder.countdown > 0
                hint: "Countdown before recording starts"
                onActivated: window.cycleCountdown()
            }
            BarToggle {
                visible: studio.recordingSelection
                label: window.width >= 900 ? "Options" : ""
                glyph: "settings"
                implicitWidth: window.width >= 900 ? 90 : 34
                checkable: false
                hint: "More recording options"
                onActivated: studio.recordingOptions(window.monitorName)
            }
            Rectangle {visible: bar.showHints; Layout.fillHeight: true; Layout.topMargin: 6; Layout.bottomMargin: 6; Layout.leftMargin: 4; Layout.rightMargin: 4; width: 1; color: theme.separator}
            Keycap {visible: bar.showHints; key: "Tab"}
            Text {visible: bar.showHints; text: studio.recordingSelection ? "Screenshot" : "Video"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 6}
            Keycap {visible: bar.showHints; key: "S"}
            Text {visible: bar.showHints; text: studio.scrollSelection ? "Screenshot" : "Scroll"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 6}
            Keycap {
                key: "Esc"
                Accessible.role: Accessible.Button
                Accessible.name: "Cancel capture"
                Accessible.onPressAction: studio.cancelSelection()
                MouseArea {anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: studio.cancelSelection()}
            }
            Text {visible: bar.showHints; text: "Cancel"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 6}
        }
    }
    BarHint {
        id: hoverHint
        anchors.horizontalCenter: bar.horizontalCenter
        anchors.top: bar.bottom
        anchors.topMargin: 8
        maximumWidth: Math.min(640, window.width - 32)
        text: window.visible && !window.dragging && window.hintOwner ? window.hintOwner.hint : ""
    }
    Rectangle {
        visible: !hoverHint.visible && !bar.showPrompt && !(studio.recordingSelection && recorder.state === "loading")
        anchors.horizontalCenter: bar.horizontalCenter
        anchors.top: bar.bottom
        anchors.topMargin: 8
        width: Math.min(window.width - 32, selectionHint.implicitWidth + 24)
        height: 30
        radius: theme.radius
        color: theme.alpha(theme.background, 1)
        Text {
            id: selectionHint
            anchors.fill: parent
            anchors.margins: 6
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: studio.recordingSelection ? "Click a window or drag an area to record"
                : studio.scrollSelection ? "Click a window to scroll and stitch it"
                : "Click a window or drag an area"
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: 12
        }
    }
    // Recording options load in the background; say so if they are slow.
    Rectangle {
        visible: studio.recordingSelection && recorder.state === "loading"
        anchors.horizontalCenter: bar.horizontalCenter
        anchors.top: bar.bottom
        anchors.topMargin: 8
        width: loadingText.implicitWidth + 20
        height: 26
        radius: theme.radius
        color: theme.alpha(theme.background, 0.94)
        Text {
            id: loadingText
            anchors.centerIn: parent
            text: "Checking audio and displays…"
            color: theme.muted
            font.family: theme.fontFamily
            font.pixelSize: 11
        }
    }
    onClosing: function (close) {
        if (visible) {
            close.accepted = false;
            studio.cancelSelection();
        }
    }
}
