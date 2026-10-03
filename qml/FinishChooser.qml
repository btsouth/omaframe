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
    property bool canChoose: ready && !studio.recoveryAction.length
    readonly property bool inlineFolderDialog: "popupType" in saveFolder
    // A click only selects a finish, so a stray click right after a capture
    // cannot copy or save it. The selection stays here until it is used:
    // changing studio.style would redraw every card.
    property int selected: studio.style
    function select(index) { if (canChoose) selected = index }
    function useSelected() { if (studio.style !== selected) studio.style = selected }
    function choose(index) { if (canChoose) {selected = index; studio.chooseFinish(index)} }
    // While a saved screenshot waits for a retry, the finish is fixed.
    function finish() { if (ready) studio.chooseFinish(canChoose ? selected : studio.style) }
    function save() { if (canChoose) {useSelected(); studio.saveQuick()} }
    function copy() { if (canChoose) {useSelected(); studio.copyQuick()} }
    function edit() { if (canChoose) {useSelected(); studio.openEditor()} }
    function changeFolder() {
        // A popup inside the overlay stays above the capture. Older Qt
        // versions can change the folder from the regular editor window.
        if (inlineFolderDialog) {
            saveFolder.popupType = Popup.Item
            saveFolder.open()
        } else studio.openEditor()
    }
    onVisibleChanged: if (visible) {selected = studio.style; keyboard.forceActiveFocus()}
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
    // Clicking outside the panel does nothing; Esc and × cancel.
    MouseArea {anchors.fill: parent}
    Item {
        id: keyboard
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            if (saveFolder.visible) return
            if (event.key === Qt.Key_C && event.modifiers === Qt.ControlModifier) {
                chooser.copy()
                event.accepted = true
                return
            }
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {chooser.choose(event.key - Qt.Key_1); event.accepted = true}
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && keyboard.activeFocus) {chooser.finish(); event.accepted = true}
            else if (event.key === Qt.Key_E) {chooser.edit(); event.accepted = true}
            else if (event.key === Qt.Key_R) {if(!studio.busy) studio.capture(true); event.accepted = true}
            else if (event.key === Qt.Key_H) {if(chooser.canChoose) studio.hideSecrets(); event.accepted = true}
            else if (event.key === Qt.Key_T) {if(chooser.ready) studio.copyText(); event.accepted = true}
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
                    Text {text: chooser.captureError ? "Your clipboard is unchanged." : studio.recoveryAction.length ? "Retry the unfinished step before continuing." : studio.dimensions + "   ·   Click to select a finish. Double-click, Enter or 1–9 uses it. Press E to edit first."; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight}
                    }
                    StudioButton {text: "Edit"; glyph: "crop"; enabled: chooser.canChoose; hint: "Crop, annotate, redact the selected finish · E"; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: chooser.edit(); Keys.onEnterPressed: chooser.edit(); onClicked: chooser.edit()}
                    Text {text: "E"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 12; Layout.rightMargin: 10}
                    StudioButton {glyph: "close"; quiet: true; enabled: !studio.busy; hint: "Cancel · Esc"; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.dismissQuick(); Keys.onEnterPressed: studio.dismissQuick(); onClicked: studio.dismissQuick()}
                }
                RowLayout {
                    visible: studio.secretCount > 0 && !chooser.captureError
                    Layout.fillWidth: true
                    spacing: 12
                    StudioButton {text: "Hide " + studio.secretCount + (studio.secretCount === 1 ? " possible secret" : " possible secrets"); glyph: "redact"; primary: true; enabled: chooser.canChoose; hint: "Redact possible keys, tokens, emails and card numbers · H"; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.hideSecrets(); Keys.onEnterPressed: studio.hideSecrets(); onClicked: studio.hideSecrets()}
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
                            enabled: chooser.canChoose
                            Accessible.name: (index+1) + ". " + studio.styles[index] + ". Select; double-click to " + (studio.autoSaveScreenshots ? "copy and save" : "copy to clipboard") + "."
                            onClicked: chooser.select(index)
                            onDoubleClicked: chooser.choose(index)
                            // Moving through the cards with the keyboard selects them too.
                            onActiveFocusChanged: if (activeFocus) chooser.select(index)
                            // Numbers/E/Esc still work when a card holds focus.
                            Keys.forwardTo: [keyboard]
                            Keys.onReturnPressed: chooser.choose(index)
                            Keys.onEnterPressed: chooser.choose(index)
                            Keys.onLeftPressed: if (index > 0) styleCards.itemAt(index - 1).forceActiveFocus()
                            Keys.onRightPressed: if (index < 8) styleCards.itemAt(index + 1).forceActiveFocus()
                            Keys.onUpPressed: if (index >= 3) styleCards.itemAt(index - 3).forceActiveFocus()
                            Keys.onDownPressed: if (index < 6) styleCards.itemAt(index + 3).forceActiveFocus()
                            background: Rectangle {
                                radius: theme.radius
                                color: tile.down ? theme.pressedFill : tile.hovered || tile.activeFocus ? theme.hoverFill : theme.controlFill
                                border.color: tile.activeFocus ? theme.focusBorder : chooser.selected === tile.index ? theme.selectedText : tile.hovered ? theme.hoverBorder : theme.controlBorder
                                border.width: chooser.selected === tile.index || tile.activeFocus ? 2 : 1
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
                                    Rectangle {width: 22; height: 22; radius: theme.radius; color: chooser.selected === tile.index ? theme.selectedFill : "transparent"; border.width: 1; border.color: theme.controlBorder; Text {anchors.centerIn: parent; text: tile.index+1; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 11}}
                                    Text {text: studio.styles[tile.index]; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight}
                                    Text {visible: tile.width >= 220; text: chooser.selected===tile.index ? "Selected" : tile.index===8 ? "No border" : studio.style===tile.index ? "Last used" : ""; color: chooser.selected===tile.index ? theme.selectedText : theme.muted; font.family: theme.fontFamily; font.pixelSize: 11}
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
                    Text {text: chooser.copying ? "Copying. One moment…" : chooser.accepting ? "Saving and copying. One moment…" : studio.recoveryAction.length ? "The finished PNG is already saved" : studio.autoSaveScreenshots ? "Copies and saves to " + studio.outputDirectory.replace(/^\/home\/[^/]+/, "~") : "Copies to clipboard without saving a screenshot file"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideMiddle}
                    StudioButton {text: "Copy text"; glyph: "text"; visible: studio.canReadText && !chooser.captureError; quiet: true; enabled: chooser.ready; hint: "Copy text recognized in this screenshot · T"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.copyText(); Keys.onEnterPressed: studio.copyText(); onClicked: studio.copyText()}
                    StudioButton {objectName: "clipboardButton"; visible: !chooser.captureError; text: "Clipboard"; glyph: "copy"; quiet: true; enabled: chooser.canChoose; hint: "Copy to the clipboard without saving · Ctrl+C"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: chooser.copy(); Keys.onEnterPressed: chooser.copy(); onClicked: chooser.copy()}
                    StudioButton {visible: studio.quickState === "failed" && !studio.recoveryAction.length; text: chooser.inlineFolderDialog ? "Change folder" : "Edit to change folder"; glyph: "folder"; enabled: chooser.ready; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: chooser.changeFolder(); Keys.onEnterPressed: chooser.changeFolder(); onClicked: chooser.changeFolder()}
                    StudioButton {visible: studio.recoveryAction.length > 0; text: studio.recoveryAction; glyph: "copy"; primary: true; enabled: !studio.busy; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.retryOutput(); Keys.onEnterPressed: studio.retryOutput(); onClicked: studio.retryOutput()}
                    StudioButton {text: chooser.captureError ? "Try capture again" : "Retake"; glyph: "capture"; quiet: !chooser.captureError; enabled: !studio.busy; hint: "Select a new region · R"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: studio.capture(true); Keys.onEnterPressed: studio.capture(true); onClicked: studio.capture(true)}
                    StudioButton {objectName: "saveButton"; visible: !chooser.captureError && !studio.recoveryAction.length; text: "Copy and save"; glyph: "copy"; primary: true; enabled: chooser.canChoose; hint: studio.autoSaveScreenshots ? "Copy and save to the folder · Enter" : "Copy and save to the folder"; implicitHeight: 32; Keys.forwardTo: [keyboard]; Keys.onReturnPressed: chooser.save(); Keys.onEnterPressed: chooser.save(); onClicked: chooser.save()}
                }
            }
        }
    }
}
