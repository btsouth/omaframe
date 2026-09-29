import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: chooser
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
    title: "Omaframe finishes"
    property bool captureError: studio.quickState === "capture-error"
    property bool accepting: studio.quickState === "saving"
    property bool ready: !studio.busy && !captureError
    function choose(index) { if (ready) studio.chooseFinish(index) }
    onVisibleChanged: if (visible) keyboard.forceActiveFocus()
    onClosing: function(close) {
        if (visible) {close.accepted = false; studio.dismissQuick()}
    }

    Rectangle {anchors.fill: parent; color: theme.scrim}
    MouseArea {anchors.fill: parent; enabled: !studio.busy; onClicked: studio.dismissQuick()}
    Item {
        id: keyboard
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {chooser.choose(event.key - Qt.Key_1); event.accepted = true}
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {chooser.choose(studio.style); event.accepted = true}
            else if (event.key === Qt.Key_E) {if(chooser.ready) studio.openEditor(); event.accepted = true}
            else if (event.key === Qt.Key_R) {if(!studio.busy) studio.capture(true); event.accepted = true}
            else if (event.key === Qt.Key_H) {if(chooser.ready) studio.hideSecrets(); event.accepted = true}
            else if (event.key === Qt.Key_T) {if(!chooser.captureError) studio.copyText(); event.accepted = true}
            else if (event.key === Qt.Key_Escape) {studio.dismissQuick(); event.accepted = true}
        }
        Rectangle {
            id: panel
            anchors.centerIn: parent
            width: Math.min(1010, chooser.width - 48)
            height: chooser.captureError ? 270 : Math.min(728, chooser.height - 48)
            radius: theme.radius
            color: theme.alpha(theme.background, 1)
            border.width: 2
            border.color: theme.frame
            // Keep clicks in panel whitespace from dismissing the capture.
            MouseArea {anchors.fill: parent}
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 15
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {width: 35; height: 35; radius: theme.radius; color: theme.accent; Glyph {anchors.centerIn: parent; name: "capture"; ink: theme.onAccent; width: 22; height: 22}}
                    ColumnLayout {
                        spacing: 3
                        Layout.fillWidth: true
                    Text {text: chooser.captureError ? "Capture needs attention" : chooser.accepting ? "Finishing your screenshot…" : studio.recoveryAction.length ? "Screenshot saved" : "Choose a finish"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 20; font.weight: Font.Medium; Layout.fillWidth: true; elide: Text.ElideRight}
                    Text {text: chooser.captureError ? "Your clipboard is unchanged." : studio.recoveryAction.length ? "Retry the unfinished step, or choose another finish." : studio.textNote.length ? studio.textNote : studio.dimensions + "   ·   Click a card or press 1–9. Press E to mark it up first."; color: studio.textNote.length && !chooser.captureError && !studio.recoveryAction.length ? theme.text : theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight}
                    }
                    // Only there when OCR found something. Never "all secrets":
                    // OCR misses things.
                    StudioButton {visible: studio.secretCount > 0 && !chooser.captureError; text: "Hide " + studio.secretCount + (studio.secretCount === 1 ? " possible secret" : " possible secrets"); glyph: "redact"; primary: true; enabled: chooser.ready; hint: "Redact what looks like keys, tokens, emails and card numbers · H"; onClicked: studio.hideSecrets()}
                    Text {visible: studio.secretCount > 0 && !chooser.captureError; text: "H"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 10}
                    StudioButton {text: "Edit"; glyph: "crop"; enabled: chooser.ready; hint: "Crop, annotate, redact · E"; onClicked: studio.openEditor()}
                    Text {text: "E"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 10}
                    StudioButton {glyph: "close"; quiet: true; enabled: !studio.busy; hint: "Cancel · Esc"; onClicked: studio.dismissQuick()}
                }
                GridLayout {
                    visible: !chooser.captureError
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 12
                    Repeater {
                        model: 9
                        Button {
                            id: tile
                            required property int index
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 280
                            Layout.preferredHeight: 170
                            hoverEnabled: true
                            enabled: chooser.ready
                            Accessible.name: (index+1) + ". " + studio.styles[index] + ". " + (studio.recoveryAction.length && studio.style === index ? studio.recoveryAction : "Copy and save") + "."
                            onClicked: chooser.choose(index)
                            // Numbers/E/Esc still work when a card holds focus.
                            Keys.forwardTo: [keyboard]
                            background: Rectangle {
                                radius: theme.radius
                                color: tile.down ? theme.pressedFill : tile.hovered || tile.activeFocus ? theme.hoverFill : theme.controlFill
                                border.color: tile.activeFocus ? theme.focusBorder : studio.style === tile.index ? theme.selectedText : tile.hovered ? theme.hoverBorder : theme.controlBorder
                                border.width: studio.style === tile.index || tile.activeFocus ? 2 : 1
                                Behavior on color {ColorAnimation {duration: 90}}
                            }
                            contentItem: Item {
                                Image {
                                    anchors {left: parent.left; right: parent.right; top: parent.top; bottom: labelRow.top; margins: 9; bottomMargin: 7}
                                    source: studio.rendering ? "" : "image://frames/style"+tile.index+"?"+studio.revision
                                    visible: !studio.rendering
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.PreserveAspectFit
                                }
                                Text {anchors.centerIn: parent; visible: studio.rendering; text: "Preparing…"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 12}
                                RowLayout {
                                    id: labelRow
                                    anchors {left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 11; rightMargin: 11; bottomMargin: 9}
                                    spacing: 9
                                    Rectangle {width: 22; height: 22; radius: theme.radius; color: studio.style === tile.index ? theme.selectedFill : "transparent"; border.width: 1; border.color: theme.controlBorder; Text {anchors.centerIn: parent; text: tile.index+1; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 11}}
                                    Text {text: studio.styles[tile.index]; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13; Layout.fillWidth: true}
                                    Text {text: tile.index===8 ? "No border" : studio.style===tile.index ? "Last used" : ""; color: studio.style===tile.index ? theme.selectedText : theme.muted; font.family: theme.fontFamily; font.pixelSize: 11}
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: studio.quickState === "failed" || chooser.captureError
                    Layout.fillWidth: true
                    text: studio.status
                    color: theme.urgent
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Item {visible: chooser.captureError; Layout.fillHeight: true}
                RowLayout {
                    Layout.fillWidth: true
                    Text {text: chooser.accepting ? "Saving and copying. One moment…" : studio.recoveryAction.length ? "The finished PNG is already saved" : "A finish is copied and saved to " + studio.outputDirectory.replace(/^\/home\/[^/]+/, "~"); color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideMiddle}
                    Text {text: "T  Copy text"; visible: studio.canReadText && !chooser.captureError; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 11; Layout.rightMargin: 12}
                    Text {text: "↵  Last finish"; visible: !chooser.captureError; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 11; Layout.rightMargin: 12}
                    StudioButton {visible: studio.recoveryAction.length > 0; text: studio.recoveryAction; glyph: "copy"; primary: true; enabled: !studio.busy; implicitHeight: 32; onClicked: studio.retryOutput()}
                    StudioButton {text: chooser.captureError ? "Try capture again" : "Retake"; glyph: "capture"; quiet: !chooser.captureError; enabled: !studio.busy; hint: "Select a new region · R"; implicitHeight: 32; onClicked: studio.capture(true)}
                }
            }
        }
    }
}
