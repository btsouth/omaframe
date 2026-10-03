import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

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
    palette.highlightedText: saveFolder.visible ? theme.text : theme.onAccent
    palette.placeholderText: theme.faint
    palette.light: theme.alpha(theme.mix(theme.background, theme.controlFill, theme.controlFill.a), 1)
    palette.midlight: theme.alpha(theme.mix(theme.background, theme.hoverFill, theme.hoverFill.a), 1)
    palette.mid: theme.controlBorder
    palette.dark: theme.frame
    color: "transparent"
    flags: Qt.FramelessWindowHint
    title: "Omaframe finishes"
    property bool captureError: studio.quickState === "capture-error"
    property bool accepting: studio.quickState === "saving" || copying
    property bool copying: studio.quickState === "copying"
    property bool ready: !studio.busy && !captureError
    readonly property bool inlineFolderDialog: "popupType" in saveFolder
    function choose(index) { if (ready) studio.style = index }
    function save() { if (ready) studio.chooseFinish(studio.style) }
    function changeFolder() {
        // A popup inside the overlay stays above the capture. Older Qt
        // versions can change the folder from the regular editor window.
        if (inlineFolderDialog) {
            saveFolder.popupType = Popup.Item
            saveFolder.open()
        } else studio.openEditor()
    }
    onVisibleChanged: if (visible) keyboard.forceActiveFocus()
    onClosing: function(close) {
        if (visible) {close.accepted = false; studio.dismissQuick()}
    }
    FolderDialog {
        id: saveFolder
        title: "Choose screenshot save folder"
        currentFolder: "file://" + studio.outputDirectory
        onAccepted: {
            studio.setOutputDirectory(selectedFolder)
            keyboard.forceActiveFocus()
        }
        onRejected: keyboard.forceActiveFocus()
    }

    Rectangle {anchors.fill: parent; color: theme.scrim}
    MouseArea {anchors.fill: parent; enabled: !studio.busy; onClicked: studio.dismissQuick()}
    Item {
        id: keyboard
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            if (saveFolder.visible) return
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {chooser.choose(event.key - Qt.Key_1); event.accepted = true}
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && keyboard.activeFocus) {chooser.save(); event.accepted = true}
            else if (event.key === Qt.Key_E) {if(chooser.ready) studio.openEditor(); event.accepted = true}
            else if (event.key === Qt.Key_R) {if(!studio.busy) studio.capture(true); event.accepted = true}
            else if (event.key === Qt.Key_H) {if(chooser.ready) studio.hideSecrets(); event.accepted = true}
            else if (event.key === Qt.Key_T) {if(chooser.ready) studio.copyText(); event.accepted = true}
            else if (event.key === Qt.Key_Escape) {
                if (chooser.captureError || !studio.hasImage) studio.dismissQuick()
                else studio.copyQuick()
                event.accepted = true
            }
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
                    Text {text: chooser.captureError ? "Your clipboard is unchanged." : studio.recoveryAction.length ? "Retry the unfinished step, or choose another finish." : studio.dimensions + "   ·   Pick a finish with 1–9. Press E to edit first."; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight}
                    }
                    StudioButton {text: "Edit"; glyph: "crop"; enabled: chooser.ready; hint: "Crop, annotate, redact · E"; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.openEditor(); Keys.onEnterPressed: studio.openEditor(); onClicked: studio.openEditor()}
                    Text {text: "E"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 10}
                    StudioButton {glyph: "close"; quiet: true; enabled: !studio.busy; hint: "Cancel"; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.dismissQuick(); Keys.onEnterPressed: studio.dismissQuick(); onClicked: studio.dismissQuick()}
                }
                RowLayout {
                    visible: studio.secretCount > 0 && !chooser.captureError
                    Layout.fillWidth: true
                    spacing: 12
                    StudioButton {text: "Hide " + studio.secretCount + (studio.secretCount === 1 ? " possible secret" : " possible secrets"); glyph: "redact"; primary: true; enabled: chooser.ready; hint: "Redact possible keys, tokens, emails and card numbers · H"; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.hideSecrets(); Keys.onEnterPressed: studio.hideSecrets(); onClicked: studio.hideSecrets()}
                    Text {Layout.fillWidth: true; text: "Check the screenshot for anything else before sharing."; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap}
                }
                GridLayout {
                    visible: !chooser.captureError
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 12
                    Repeater {
                        id: styleCards
                        model: 9
                        Button {
                            id: tile
                            required property int index
                            objectName: "finish" + index
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 280
                            Layout.preferredHeight: 170
                            hoverEnabled: true
                            enabled: chooser.ready
                            Accessible.name: (index+1) + ". " + studio.styles[index] + ". " + (studio.recoveryAction.length && studio.style === index ? studio.recoveryAction : "Choose") + "."
                            onClicked: chooser.choose(index)
                            // Numbers/E/Esc still work when a card holds focus.
                            Keys.forwardTo: [keyboard]
                            Keys.onReturnPressed: {chooser.choose(index); chooser.save()}
                            Keys.onEnterPressed: {chooser.choose(index); chooser.save()}
                            Keys.onLeftPressed: if (index > 0) styleCards.itemAt(index - 1).forceActiveFocus()
                            Keys.onRightPressed: if (index < 8) styleCards.itemAt(index + 1).forceActiveFocus()
                            Keys.onUpPressed: if (index >= 3) styleCards.itemAt(index - 3).forceActiveFocus()
                            Keys.onDownPressed: if (index < 6) styleCards.itemAt(index + 3).forceActiveFocus()
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
                                    // A tall page fits the card as a readable band from
                                    // its top; a sliver would show nothing.
                                    fillMode: studio.tallImage ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                                    verticalAlignment: studio.tallImage ? Image.AlignTop : Image.AlignVCenter
                                }
                                Text {anchors.centerIn: parent; visible: studio.rendering; text: "Preparing…"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 12}
                                RowLayout {
                                    id: labelRow
                                    anchors {left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 11; rightMargin: 11; bottomMargin: 9}
                                    spacing: 9
                                    Rectangle {width: 22; height: 22; radius: theme.radius; color: studio.style === tile.index ? theme.selectedFill : "transparent"; border.width: 1; border.color: theme.controlBorder; Text {anchors.centerIn: parent; text: tile.index+1; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 11}}
                                    Text {text: studio.styles[tile.index]; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight}
                                    Text {visible: tile.width >= 220; text: tile.index===8 ? "No border" : studio.style===tile.index ? "Selected" : ""; color: studio.style===tile.index ? theme.selectedText : theme.muted; font.family: theme.fontFamily; font.pixelSize: 11}
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: studio.quickState === "failed" || studio.quickState === "copy-failed" || chooser.captureError
                    Layout.fillWidth: true
                    text: studio.status
                    color: theme.urgent
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                Item {visible: chooser.captureError; Layout.fillHeight: true}
                Text {
                    visible: studio.textNote.length > 0 && !chooser.captureError
                    Layout.fillWidth: true
                    text: studio.textNote
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {text: chooser.copying ? "Copying. One moment…" : chooser.accepting ? "Saving and copying. One moment…" : studio.recoveryAction.length ? "The finished PNG is already saved" : "Saves to " + studio.outputDirectory.replace(/^\/home\/[^/]+/, "~"); color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideMiddle}
                    StudioButton {text: "Copy text"; glyph: "text"; visible: studio.canReadText && !chooser.captureError; quiet: true; enabled: chooser.ready; hint: "Copy text recognized in this screenshot · T"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.copyText(); Keys.onEnterPressed: studio.copyText(); onClicked: studio.copyText()}
                    StudioButton {objectName: "clipboardButton"; visible: !chooser.captureError; text: "Clipboard"; glyph: "copy"; quiet: true; enabled: chooser.ready; hint: "Copy to the clipboard without saving · Esc"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.copyQuick(); Keys.onEnterPressed: studio.copyQuick(); onClicked: studio.copyQuick()}
                    StudioButton {visible: studio.quickState === "failed" && !studio.recoveryAction.length; text: chooser.inlineFolderDialog ? "Change folder" : "Edit to change folder"; glyph: "folder"; enabled: chooser.ready; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: chooser.changeFolder(); Keys.onEnterPressed: chooser.changeFolder(); onClicked: chooser.changeFolder()}
                    StudioButton {visible: studio.recoveryAction.length > 0; text: studio.recoveryAction; glyph: "copy"; primary: true; enabled: !studio.busy; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.retryOutput(); Keys.onEnterPressed: studio.retryOutput(); onClicked: studio.retryOutput()}
                    StudioButton {text: chooser.captureError ? "Try capture again" : "Retake"; glyph: "capture"; quiet: !chooser.captureError; enabled: !studio.busy; hint: "Select a new region · R"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.capture(true); Keys.onEnterPressed: studio.capture(true); onClicked: studio.capture(true)}
                    StudioButton {objectName: "saveButton"; visible: !chooser.captureError && !studio.recoveryAction.length; text: "Copy and save"; glyph: "copy"; primary: true; enabled: chooser.ready; hint: "Copy and save to the folder · Enter"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: chooser.save(); Keys.onEnterPressed: chooser.save(); onClicked: chooser.save()}
                }
            }
        }
    }
}
