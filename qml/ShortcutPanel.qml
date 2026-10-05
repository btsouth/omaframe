import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shows the capture shortcuts and offers to point free/default keys at
// Omaframe. Only Omarchy's stock bindings or free keys are ever changed.
ColumnLayout {
    id: panel
    property bool compact: false
    spacing: 8
    Component.onCompleted: if (!shortcuts.available && !shortcuts.checking) shortcuts.refresh()
    function describe(key, state, action) {
        if (key.length)
            return key === (action === "screenshot" ? "Print" : action === "delay" ? "Shift+Print" : action === "pause" ? "Alt+Shift+Print" : "Alt+Print") ? "Ready" : "Ready on " + key;
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
            Layout.preferredWidth: 132
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
                font.pixelSize: 11
                width: parent.width - 12
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
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
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    width: 7
                    height: 7
                    radius: theme.radius > 0 ? 4 : 0
                    color: shortcutRow.ready ? theme.accent : shortcutRow.status === "Used by another action" ? theme.urgent : theme.faint
                }
                Text {
                    Layout.fillWidth: true
                    text: shortcutRow.status
                    wrapMode: Text.Wrap
                    color: shortcutRow.ready ? theme.selectedText : theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }
    }
    ShortcutRow {
        key: shortcuts.screenshotKey || "Print"
        label: "Take a screenshot"
        ready: shortcuts.screenshotKey.length > 0
        status: panel.describe(shortcuts.screenshotKey, shortcuts.screenshotState, "screenshot")
    }
    ShortcutRow {
        key: shortcuts.recordKey || "Alt+Print"
        label: "Start or stop recording"
        ready: shortcuts.recordKey.length > 0
        status: panel.describe(shortcuts.recordKey, shortcuts.recordState, "record")
    }
    ShortcutRow {
        key: shortcuts.pauseKey || "Alt+Shift+Print"
        label: "Pause or resume recording"
        ready: shortcuts.pauseKey.length > 0
        status: panel.describe(shortcuts.pauseKey, shortcuts.pauseState, "pause")
    }
    ShortcutRow {
        key: shortcuts.delayKey || "Shift+Print"
        label: "Delayed screenshot (optional)"
        ready: shortcuts.delayKey.length > 0
        status: panel.describe(shortcuts.delayKey, shortcuts.delayState, "delay")
    }
    StudioButton {
        objectName: "setupScreenshotDelay"
        visible: shortcuts.available && !shortcuts.delayKey.length &&
                 (shortcuts.delayState === "stock" || shortcuts.delayState === "none")
        text: "Use Shift+Print for delay"
        glyph: "timer"
        enabled: !shortcuts.checking
        onClicked: shortcuts.setUpDelay()
    }
    Text {
        Layout.fillWidth: true
        visible: text.length > 0
        text: shortcuts.message.length ? shortcuts.message
            : !shortcuts.available && !shortcuts.checking ? "Hyprland did not answer, so shortcuts cannot be checked here."
            : shortcuts.canSetUp ? "Use these keys for capture. Custom shortcuts are kept, and a backup is saved first."
            : !shortcuts.ready && (shortcuts.screenshotState === "custom" || shortcuts.recordState === "custom" || shortcuts.pauseState === "custom") ? "A key already runs something else, so Omaframe left it alone."
            : ""
        color: theme.muted
        font.family: theme.fontFamily
        font.pixelSize: 11
        wrapMode: Text.Wrap
        lineHeight: 1.2
    }
    StudioButton {
        visible: shortcuts.canSetUp
        text: shortcuts.checking ? "Setting up…" : "Use capture shortcuts"
        glyph: "keyboard"
        primary: !panel.compact
        enabled: !shortcuts.checking
        onClicked: shortcuts.setUp()
    }
}
