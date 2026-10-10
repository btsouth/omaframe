import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects

// The pinned screenshots on one display. The window covers the display but
// only takes the pointer over its pins (PinBoard sets the mask), so
// everything else reaches the desktop below. Pins are placed in the desktop
// layout: one across two displays is drawn by both of their overlays.
Window {
    id: overlay
    required property string screenName
    // This display's top left in the desktop layout.
    property real originX: 0
    property real originY: 0
    function global(item, x, y) { const p = item.mapToItem(null, x, y); return Qt.point(p.x + originX, p.y + originY); }
    color: "transparent"
    flags: Qt.FramelessWindowHint
    title: "Omaframe pins"
    palette.window: theme.alpha(theme.background, 1)
    palette.windowText: theme.text
    palette.toolTipBase: theme.alpha(theme.background, 1)
    palette.toolTipText: theme.text
    // The pin under the pointer, else the one last clicked: keys act on it.
    property int hoveredPin: -1
    property int activePin: -1
    readonly property int targetPin: hoveredPin >= 0 ? hoveredPin : activePin
    // Clicking elsewhere hands the keyboard back for good, until a pin is
    // clicked again.
    onActiveChanged: if (!active && !menu.opened) pins.setKeyboard(screenName, false)
    function pinItem(id) {
        for (let i = 0; i < pinRepeater.count; ++i) {
            const item = pinRepeater.itemAt(i);
            if (item && item.pinId === id && item.visible) return item;
        }
        return null;
    }

    Item {
        id: keyboard
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            const pin = overlay.pinItem(overlay.targetPin);
            if (!pin) return;
            const ctrl = event.modifiers & Qt.ControlModifier;
            const step = event.modifiers & Qt.ShiftModifier ? 10 : 1;
            // Closing one hands the keyboard back to the window being used.
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) { pin.dismiss(); pins.setKeyboard(overlay.screenName, false); }
            else if (ctrl && event.key === Qt.Key_C) pins.copy(pin.pinId);
            else if (ctrl && event.key === Qt.Key_S) pins.save(pin.pinId);
            else if (ctrl && event.key === Qt.Key_0) pins.actualSize(pin.pinId);
            else if (ctrl && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) pin.zoomCentred(1.1);
            else if (ctrl && event.key === Qt.Key_Minus) pin.zoomCentred(1 / 1.1);
            else if (event.key === Qt.Key_T && !ctrl) pins.setClickThrough(pin.pinId, true);
            else if (event.key === Qt.Key_Left) pins.move(pin.pinId, pin.pinX - step, pin.pinY);
            else if (event.key === Qt.Key_Right) pins.move(pin.pinId, pin.pinX + step, pin.pinY);
            else if (event.key === Qt.Key_Up) pins.move(pin.pinId, pin.pinX, pin.pinY - step);
            else if (event.key === Qt.Key_Down) pins.move(pin.pinId, pin.pinX, pin.pinY + step);
            else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) menu.showFor(pin, pin.width / 2, pin.height / 2);
            else return;
            event.accepted = true;
        }
    }

    Repeater {
        id: pinRepeater
        model: pins
        delegate: Item {
            id: pin
            required property int pinId
            required property string screen
            required property real pinX
            required property real pinY
            required property real pinWidth
            required property real pinHeight
            required property real pinOpacity
            required property bool clickThrough
            required property int stack
            required property string source
            required property int zoomPercent
            required property real created
            required property rect origin
            readonly property bool active: overlay.targetPin === pinId
            readonly property bool interacting: body.pressed || corners.resizing
            property real appear: 0
            property bool closing: false
            readonly property bool held: body.pressed || corners.resizing
            // A pin being dragged stays here until it is let go, even off
            // this display: the drag belongs to this overlay.
            visible: held || pinX < overlay.originX + overlay.width && pinX + pinWidth > overlay.originX
                     && pinY < overlay.originY + overlay.height && pinY + pinHeight > overlay.originY
            // A new pin starts over the place it was captured from and lifts
            // off to its corner, so it is plain what was pinned and where it
            // went. `flight` runs from 0 there to 1 at rest.
            property real flight: 1
            function between(from, to) { return from + (to - from) * flight; }
            x: between(origin.x, pinX) - overlay.originX
            y: between(origin.y, pinY) - overlay.originY
            width: between(origin.width, pinWidth)
            height: between(origin.height, pinHeight)
            z: stack
            opacity: pinOpacity * appear
            scale: flight < 1 ? 1 : 0.97 + 0.03 * appear
            // Only a new pin arrives like this, not one dragged onto this
            // display.
            Component.onCompleted: {
                if (Date.now() - created >= 500) appear = 1;
                else if (origin.width > 0 && origin.height > 0) { appear = 1; flight = 0; lifting.start(); }
                else appearing.start();
            }
            NumberAnimation on appear { id: appearing; running: false; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
            SequentialAnimation {
                id: lifting
                PauseAnimation { duration: 90 }
                NumberAnimation { target: pin; property: "flight"; to: 1; duration: 380; easing.type: Easing.InOutCubic }
            }
            NumberAnimation { id: leaving; target: pin; property: "appear"; to: 0; duration: 120; easing.type: Easing.InCubic; onFinished: pins.close(pin.pinId) }
            function dismiss() { if (!closing) { closing = true; if (overlay.hoveredPin === pinId) overlay.hoveredPin = -1; leaving.start(); } }
            function zoomCentred(factor) { pins.zoomBy(pinId, factor, pinX + pinWidth / 2, pinY + pinHeight / 2); hud.show(zoomPercent + "%"); }
            function activate() { lifting.stop(); flight = 1; overlay.activePin = pinId; pins.raise(pinId); pins.setKeyboard(overlay.screenName, true); keyboard.forceActiveFocus(); }

            RectangularShadow {
                anchors.fill: parent
                radius: frame.radius
                blur: 22
                offset.y: 6
                spread: 0
                color: Qt.rgba(0, 0, 0, pin.active || pin.interacting || pin.flight < 1 ? 0.42 : 0.3)
            }
            Item {
                id: picture
                anchors.fill: parent
                layer.enabled: frame.radius > 0
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: roundMask
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1
                }
                Image {
                    anchors.fill: parent
                    source: pin.visible ? pin.source : ""
                    smooth: true
                    mipmap: true
                    cache: false
                    fillMode: Image.Stretch
                }
            }
            Rectangle {
                id: roundMask
                anchors.fill: parent
                radius: frame.radius
                visible: false
                layer.enabled: true
                layer.smooth: true
            }
            // A thin edge tells a pin apart from the screen it may copy
            // exactly; the accent shows which one keys act on.
            Rectangle {
                id: frame
                anchors.fill: parent
                radius: Math.min(theme.radius, 8, pin.width / 4, pin.height / 4)
                color: "transparent"
                border.width: 1
                border.color: pin.active && !pin.clickThrough ? theme.accent : theme.alpha(theme.accent, 0.5)
                Behavior on border.color { ColorAnimation { duration: 90 } }
            }

            MouseArea {
                id: body
                anchors.fill: parent
                enabled: !pin.clickThrough && !pin.closing
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point grab
                property point start
                onContainsMouseChanged: {
                    if (containsMouse) overlay.hoveredPin = pin.pinId;
                    else if (overlay.hoveredPin === pin.pinId) overlay.hoveredPin = -1;
                }
                onPressed: function(mouse) {
                    pin.activate();
                    if (mouse.button === Qt.LeftButton) pins.hold(overlay.screenName);
                    grab = overlay.global(this, mouse.x, mouse.y);
                    start = Qt.point(pin.pinX, pin.pinY);
                    if (mouse.button === Qt.RightButton) menu.showFor(pin, mouse.x, mouse.y);
                    else if (mouse.button === Qt.MiddleButton) pin.dismiss();
                }
                onPositionChanged: function(mouse) {
                    if (!(pressedButtons & Qt.LeftButton)) return;
                    const at = overlay.global(this, mouse.x, mouse.y);
                    pins.move(pin.pinId, start.x + at.x - grab.x, start.y + at.y - grab.y);
                }
                onReleased: function(mouse) {
                    if (mouse.button === Qt.LeftButton) pins.dropped(pin.pinId);
                }
                onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton) pin.dismiss(); }
                onWheel: function(wheel) {
                    const notches = (wheel.angleDelta.y || wheel.angleDelta.x) / 120;
                    if (!notches) return;
                    if (wheel.modifiers & Qt.ControlModifier) {
                        pins.setOpacity(pin.pinId, pin.pinOpacity + notches * 0.05);
                        hud.show("Opacity " + Math.round(Math.max(0.15, Math.min(1, pin.pinOpacity + notches * 0.05)) * 100) + "%");
                    } else {
                        const at = overlay.global(this, wheel.x, wheel.y);
                        pins.zoomBy(pin.pinId, Math.pow(1.1, notches), at.x, at.y);
                        pins.dropped(pin.pinId);
                        hud.show(pin.zoomPercent + "%");
                    }
                }
            }
            // Corners resize and keep the shape.
            Item {
                id: corners
                anchors.fill: parent
                property bool resizing: false
                visible: !pin.clickThrough
                Repeater {
                    model: [ { right: false, bottom: false }, { right: true, bottom: false }, { right: false, bottom: true }, { right: true, bottom: true } ]
                    MouseArea {
                        required property var modelData
                        width: Math.min(18, pin.width / 3)
                        height: Math.min(18, pin.height / 3)
                        x: modelData.right ? pin.width - width : 0
                        y: modelData.bottom ? pin.height - height : 0
                        hoverEnabled: true
                        cursorShape: modelData.right === modelData.bottom ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        property point fixed
                        onContainsMouseChanged: if (containsMouse) overlay.hoveredPin = pin.pinId
                        onPressed: {
                            pin.activate();
                            pins.hold(overlay.screenName);
                            corners.resizing = true;
                            fixed = Qt.point(modelData.right ? pin.pinX : pin.pinX + pin.pinWidth, modelData.bottom ? pin.pinY : pin.pinY + pin.pinHeight);
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return;
                            const at = overlay.global(this, mouse.x, mouse.y);
                            pins.resizeTo(pin.pinId, fixed.x, fixed.y, at.x, at.y);
                            hud.show(pin.zoomPercent + "%");
                        }
                        onReleased: { corners.resizing = false; pins.dropped(pin.pinId); }
                    }
                }
            }
            // Click-through leaves only this badge taking the pointer: click
            // it to hold the pin again.
            Rectangle {
                id: badge
                visible: pin.clickThrough
                x: pin.width - 34
                y: 6
                width: 28
                height: 28
                radius: theme.radius > 0 ? 14 : 0
                color: badgeMouse.containsMouse ? theme.alpha(theme.background, 1) : theme.alpha(theme.background, 0.82)
                border.width: 1
                border.color: badgeMouse.containsMouse ? theme.accent : theme.frame
                opacity: badgeMouse.containsMouse ? 1 : 0.85
                Glyph { anchors.centerIn: parent; width: 16; height: 16; name: "pin"; ink: badgeMouse.containsMouse ? theme.accent : theme.text }
                MouseArea {
                    id: badgeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { pins.setClickThrough(pin.pinId, false); pin.activate(); pin.hudShow("Click-through off"); }
                }
                ToolTip.visible: badgeMouse.containsMouse
                ToolTip.delay: 500
                ToolTip.text: "Clicks pass through this pin. Click to hold it again."
            }
            function hudShow(text) { hud.show(text); }
            Connections {
                target: pins
                function onNotice(id, text) { if (id === pin.pinId) hud.show(text); }
            }
            // A short note on the pin: its size while zooming, Copied, Saved.
            Rectangle {
                id: hud
                anchors.centerIn: parent
                width: Math.min(hudText.implicitWidth + 24, pin.width - 8)
                height: hudText.implicitHeight + 12
                radius: theme.radius
                color: theme.alpha(theme.background, 0.92)
                border.width: 1
                border.color: theme.frame
                opacity: 0
                visible: opacity > 0 && pin.width > 60 && pin.height > 30
                function show(text) { hudText.text = text; fade.stop(); opacity = 1; fade.start(); }
                Text { id: hudText; anchors.centerIn: parent; width: Math.min(implicitWidth, hud.width - 16); elide: Text.ElideMiddle; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 12 }
                SequentialAnimation {
                    id: fade
                    PauseAnimation { duration: 900 }
                    NumberAnimation { target: hud; property: "opacity"; to: 0; duration: 220 }
                }
            }
        }
    }

    Popup {
        id: menu
        property var pin: null
        width: 248
        padding: 6
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        function showFor(item, px, py) {
            pin = item;
            const at = item.mapToItem(overlay.contentItem, px, py);
            x = Math.max(4, Math.min(overlay.width - width - 4, at.x));
            y = Math.max(4, Math.min(overlay.height - implicitHeight - 4, at.y));
            pins.setMenuOpen(overlay.screenName, true);
            open();
        }
        function act(f) { const p = pin; close(); if (p) f(p); }
        onClosed: { pins.setMenuOpen(overlay.screenName, false); keyboard.forceActiveFocus(); }
        background: Rectangle {
            color: theme.alpha(theme.background, 1)
            radius: theme.radius
            border.width: 2
            border.color: theme.frame
        }
        contentItem: ColumnLayout {
            spacing: 2
            component Row: ItemDelegate {
                id: row
                property string glyph: ""
                property string keys: ""
                Layout.fillWidth: true
                implicitHeight: 32
                leftPadding: 10
                rightPadding: 10
                hoverEnabled: true
                background: Rectangle { radius: theme.radius; color: row.down ? theme.pressedFill : row.hovered || row.visualFocus ? theme.hoverFill : "transparent" }
                contentItem: RowLayout {
                    spacing: 10
                    Glyph { Layout.preferredWidth: 16; Layout.preferredHeight: 16; name: row.glyph; ink: theme.text; visible: row.glyph.length > 0 }
                    Item { Layout.preferredWidth: 16; visible: row.glyph.length === 0 }
                    Text { Layout.fillWidth: true; text: row.text; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13; elide: Text.ElideRight }
                    Text { text: row.keys; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 11 }
                }
            }
            component Rule: Rectangle { Layout.fillWidth: true; Layout.topMargin: 3; Layout.bottomMargin: 3; implicitHeight: 1; color: theme.separator }
            Row { text: "Copy"; glyph: "copy"; keys: "Ctrl+C"; onClicked: menu.act(p => pins.copy(p.pinId)) }
            Row { text: "Save"; glyph: "folder"; keys: "Ctrl+S"; onClicked: menu.act(p => pins.save(p.pinId)) }
            Rule {}
            Row { text: "Actual size"; glyph: "capture"; keys: "Ctrl+0"; onClicked: menu.act(p => pins.actualSize(p.pinId)) }
            Row { text: "Click through"; glyph: "cursor"; keys: "T"; onClicked: menu.act(p => { pins.setClickThrough(p.pinId, true); p.hudShow("Click the pin badge to hold it again"); }) }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 36
                Layout.rightMargin: 10
                implicitHeight: 34
                spacing: 10
                Text { text: "Opacity"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13 }
                ThemedSlider {
                    Layout.fillWidth: true
                    from: 0.15; to: 1; stepSize: 0.05
                    value: menu.pin ? menu.pin.pinOpacity : 1
                    onMoved: if (menu.pin) pins.setOpacity(menu.pin.pinId, value)
                }
                Text { text: Math.round((menu.pin ? menu.pin.pinOpacity : 1) * 100) + "%"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 11; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignRight }
            }
            Rule {}
            Row { text: "Close"; glyph: "close"; keys: "Esc"; onClicked: menu.act(p => p.dismiss()) }
            Row { text: "Close all pins"; visible: pins.count > 1; onClicked: menu.act(p => pins.closeAll()) }
        }
    }
}
