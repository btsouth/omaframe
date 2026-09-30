import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia

// Full recording options. The capture bar covers the usual case; this opens
// from its settings button, and whenever a recording needs attention.
Window {
    id: setup
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
    title: "Omaframe recording"
    readonly property int wantedHeight: content.implicitHeight + 56
    onWantedHeightChanged: if (visible) height = Math.min(wantedHeight, screen ? screen.height : wantedHeight)
    onVisibleChanged: if (visible) keys.forceActiveFocus()
    onClosing: function(event) { if (visible) { event.accepted = false; recorder.cancel() } }
    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: recorder.cancel()
        Keys.onReturnPressed: primaryButton.clicked()
        Rectangle {
            anchors.fill: parent
            radius: theme.radius
            color: theme.alpha(theme.background, 1)
            border.width: 2
            border.color: recorder.state === "failed" ? theme.urgent : theme.frame
            ScrollView {
                id: optionsScroll
                anchors.fill: parent
                anchors.margins: 28
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: content
                    width: optionsScroll.availableWidth
                    spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: recorder.state === "failed" ? "Recording needs attention" : "Record your screen"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 19; font.weight: Font.Medium }
                            Text { text: "Nothing starts until you choose what to record."; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        StudioButton { glyph: "close"; quiet: true; hint: "Cancel · Esc"; onClicked: recorder.cancel() }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                    Text { text: "WHAT TO RECORD"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 10; font.letterSpacing: 0.8 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        StudioButton {
                            text: "Area or window"
                            glyph: "capture"
                            selected: recorder.hasTarget && !recorder.targetLabel.startsWith("Entire")
                            quiet: !selected
                            enabled: recorder.state !== "loading"
                            hint: "Choose on screen. Recording starts when you let go."
                            onClicked: recorder.chooseRegion()
                        }
                        Choice {
                            id: displayChoice
                            Layout.fillWidth: true
                            model: recorder.displays
                            currentIndex: -1
                            displayText: recorder.targetLabel.startsWith("Entire") ? recorder.targetLabel.replace("Entire display · ", "Whole display · ") : "Whole display…"
                            onActivated: recorder.selectDisplay(currentIndex)
                        }
                    }
                    Text {
                        visible: recorder.hasTarget && !recorder.targetLabel.startsWith("Entire")
                        text: recorder.targetLabel
                        color: theme.selectedText
                        font.family: theme.fontFamily
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                    Text { text: "SOUND AND DETAILS"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 10; font.letterSpacing: 0.8 }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 4
                        RecordToggle { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Computer sound"; checked: recorder.desktopAudio; onToggled: recorder.desktopAudio = checked }
                        RecordToggle { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Microphone"; checked: recorder.micAudio; onToggled: recorder.micAudio = checked }
                        RecordToggle { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Show the cursor"; checked: recorder.cursor; onToggled: recorder.cursor = checked }
                        RowLayout {
                            spacing: 8
                            Text { text: "Countdown"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13 }
                            Choice {
                                model: ["Off", "3 seconds", "5 seconds"]
                                currentIndex: recorder.countdown === 0 ? 0 : recorder.countdown === 3 ? 1 : 2
                                onActivated: recorder.countdown = [0, 3, 5][currentIndex]
                            }
                        }
                    }
                    Choice {
                        Layout.fillWidth: true
                        visible: recorder.micAudio
                        model: recorder.microphones
                        textRole: "label"
                        currentIndex: recorder.microphone
                        onActivated: recorder.microphone = currentIndex
                        displayText: currentIndex < 0 ? (recorder.microphones.length ? "Choose a microphone" : "No microphone found") : currentText
                    }
                    Text {
                        visible: !recorder.desktopAudio && !recorder.micAudio
                        text: "The video will be silent."
                        color: theme.muted
                        font.family: theme.fontFamily
                        font.pixelSize: 12
                    }
                    RecordToggle {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        visible: recorder.desktopAudio || recorder.micAudio
                        text: "Silence the first 0.4 s to hide a start-up pop"
                        checked: recorder.suppressStartupPop
                        onToggled: recorder.suppressStartupPop = checked
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                    RecordToggle {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        text: "Camera overlay"; checked: recorder.camera.enabled
                        onToggled: recorder.camera.enabled = checked
                    }
                    Choice {
                        Layout.fillWidth: true; visible: recorder.camera.enabled
                        model: recorder.camera.devices; textRole: "label"
                        currentIndex: recorder.camera.device
                        displayText: currentIndex < 0 ? "No camera found" : currentText
                        onActivated: recorder.camera.device = currentIndex
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 130
                        visible: recorder.camera.enabled
                        color: theme.well; radius: theme.radius; clip: true
                        VideoOutput { id: cameraPreview; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
                        Text { anchors.centerIn: parent; visible: !recorder.camera.ready; text: "Waiting for camera…"; color: theme.muted; font.pixelSize: 12 }
                    }
                    Binding { target: recorder.camera; property: "previewSink"; value: setup.visible && recorder.camera.enabled ? cameraPreview.videoSink : null }
                    Text {
                        Layout.fillWidth: true; visible: recorder.camera.enabled
                        text: recorder.camera.status; color: recorder.camera.ready ? theme.muted : theme.urgent
                        font.pixelSize: 12; wrapMode: Text.Wrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: stopColumn.implicitHeight + 26
                        radius: theme.radius
                        readonly property color tone: recorder.needsStopShortcut ? theme.urgent : recorder.hasTarget ? theme.accent : theme.text
                        color: theme.alpha(tone, 0.07)
                        border.width: 1
                        border.color: theme.alpha(tone, 0.35)
                        ColumnLayout {
                            id: stopColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 13
                            spacing: 9
                            Text {
                                Layout.fillWidth: true
                                text: recorder.controlLocation
                                color: recorder.needsStopShortcut ? theme.urgent : theme.text
                                font.family: theme.fontFamily
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                lineHeight: 1.2
                            }
                            StudioButton {
                                visible: (!recorder.stopShortcut && shortcuts.recordKey.length === 0 && (shortcuts.recordState === "stock" || shortcuts.recordState === "none"))
                                         || (!recorder.hasControl && shortcuts.pauseKey.length === 0 && shortcuts.pauseState === "none")
                                text: shortcuts.checking ? "Setting up…" : recorder.stopShortcut ? "Enable pause shortcut" : "Set up recording shortcuts"
                                glyph: "keyboard"
                                enabled: !shortcuts.checking
                                onClicked: shortcuts.setUpRecording()
                            }
                            Text {
                                visible: !recorder.stopShortcut && shortcuts.recordState === "custom"
                                Layout.fillWidth: true
                                text: "Alt+Print already runs something else. Bind any key to omaframe --record to use it here."
                                color: theme.muted
                                font.family: theme.fontFamily
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                            Text {
                                visible: shortcuts.message.length > 0 && !recorder.stopShortcut
                                Layout.fillWidth: true
                                text: shortcuts.message
                                color: theme.muted
                                font.family: theme.fontFamily
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    Text {
                        visible: recorder.state === "failed" || (recorder.status.length > 0 && !recorder.needsStopShortcut && recorder.status !== "Ready to record." && recorder.status !== "Choose what to record.")
                        text: recorder.status
                        color: recorder.state === "failed" ? theme.urgent : theme.muted
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.family: theme.fontFamily
                        font.pixelSize: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Text { text: "60 fps · MP4 · saved in " + video.outputDirectory.replace(/^\/home\/[^/]+/, "~"); color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        StudioButton {
                            id: primaryButton
                            text: !recorder.hasTarget ? "Choose area and record" : recorder.countdown > 0 ? "Record in " + recorder.countdown + " s" : "Start recording"
                            glyph: "record"
                            primary: true
                            enabled: recorder.state !== "loading" && !recorder.active && (!recorder.hasTarget || recorder.canStart)
                            onClicked: if (enabled) (!recorder.hasTarget ? recorder.chooseRegion() : recorder.start())
                        }
                    }
                }
            }
        }
    }
}
