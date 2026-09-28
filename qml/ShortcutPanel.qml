import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shows what Print and Alt+Print do now, and offers to point them at
// Omaframe. Only Omarchy's stock bindings or free keys are ever changed.
ColumnLayout {
    id: panel
    property bool compact: false
    spacing: 8
    Component.onCompleted: if (!shortcuts.available && !shortcuts.checking) shortcuts.refresh()
    function describe(key, state, action) {
        if (key.length)
            return key === (action === "screenshot" ? "Print" : "Alt+Print") ? "Ready" : "Ready on " + key;
        if (state === "stock")
            return "Omarchy's own tool for now";
        if (state === "none")
            return "Not set";
        if (state === "custom")
            return "Used by another action";
        return shortcuts.checking ? "Checking…" : "Unknown";
    }
    component ShortcutRow: RowLayout {
        id: shortcutRow
        property string key
        property string label
        property string status
        property bool ready
        Layout.fillWidth: true
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 78
            implicitHeight: 26
            radius: theme.radius
            color: theme.controlFill
            border.width: 1
            border.color: theme.controlBorder
            Text {
                anchors.centerIn: parent
                text: shortcutRow.key
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: 12
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: shortcutRow.label
                color: theme.text
                font.family: theme.fontFamily
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            RowLayout {
                spacing: 6
                Rectangle {
                    width: 7
                    height: 7
                    radius: theme.radius > 0 ? 4 : 0
                    color: shortcutRow.ready ? theme.accent : shortcutRow.status === "Used by another action" ? theme.urgent : theme.faint
                }
                Text {
                    text: shortcutRow.status
                    color: shortcutRow.ready ? theme.selectedText : theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }
    }
    ShortcutRow {
        key: "Print"
        label: "Take a screenshot"
        ready: shortcuts.screenshotKey.length > 0
        status: panel.describe(shortcuts.screenshotKey, shortcuts.screenshotState, "screenshot")
    }
    ShortcutRow {
        key: "Alt+Print"
        label: "Record, and stop recording"
        ready: shortcuts.recordKey.length > 0
        status: panel.describe(shortcuts.recordKey, shortcuts.recordState, "record")
    }
    Text {
        Layout.fillWidth: true
        visible: text.length > 0
        text: shortcuts.message.length ? shortcuts.message
            : !shortcuts.available && !shortcuts.checking ? "Hyprland did not answer, so shortcuts cannot be checked here."
            : shortcuts.canSetUp ? "This replaces only Omarchy's default Print and Alt+Print actions. Your other shortcuts stay as they are, and hypr/bindings.lua is backed up first."
            : !shortcuts.ready && (shortcuts.screenshotState === "custom" || shortcuts.recordState === "custom") ? "A key already runs something else, so Omaframe left it alone. To use another key, bind it to omaframe --capture or omaframe --record."
            : ""
        color: theme.muted
        font.family: theme.fontFamily
        font.pixelSize: 11
        wrapMode: Text.Wrap
        lineHeight: 1.2
    }
    StudioButton {
        visible: shortcuts.canSetUp
        text: shortcuts.checking ? "Setting up…" : "Use Print and Alt+Print"
        glyph: "keyboard"
        primary: !panel.compact
        enabled: !shortcuts.checking
        onClicked: shortcuts.setUp()
    }
}
