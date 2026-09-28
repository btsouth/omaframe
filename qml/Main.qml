import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: !captureAtStartup
    width: 1360
    height: 900
    minimumWidth: 900
    minimumHeight: 620
    title: "Omaframe"
    color: theme.alpha(theme.background, 1)
    font.family: theme.fontFamily
    font.pixelSize: 13
    // Stock controls (tool tips, text fields, scroll bars) take the theme too.
    palette.window: theme.alpha(theme.background, 1)
    palette.windowText: theme.text
    palette.base: theme.well
    palette.alternateBase: theme.controlFill
    palette.text: theme.text
    palette.button: theme.controlFill
    palette.buttonText: theme.text
    palette.brightText: theme.text
    palette.toolTipBase: theme.alpha(theme.background, 1)
    palette.toolTipText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: theme.onAccent
    palette.placeholderText: theme.faint
    palette.light: theme.controlFill
    palette.midlight: theme.hoverFill
    palette.mid: theme.controlBorder
    palette.dark: theme.frame
    palette.shadow: theme.scrim
    property bool videoMode: false
    property bool recordingReview: false
    property bool editing: false
    property string tool: "select"
    property string savedSignature: ""
    readonly property bool videoLoaded: videoMode && video.source.toString().length > 0
    readonly property bool videoUnchanged: videoLoaded && videoPane.clipStart <= 0.001 && Math.abs(videoPane.clipEnd - video.duration) <= 0.001 && !videoPane.muted && videoPane.cuts.length === 0
    readonly property bool videoSavedCurrent: videoLoaded && video.savedName.length > 0 && savedSignature === videoPane.signature
    readonly property bool typing: textEditor.active || colorInput.activeFocus || boxColorInput.activeFocus || fontField.inputFocus
    property bool shortcutsAllowed: !openDialog.visible && !imageFolderDialog.visible && !videoFolderDialog.visible && !originalsDialog.opened && !draftDeleteDialog.opened && !captureMenu.opened && !settingsPopup.opened && !aspectChoice.popup.visible && !typing
    property bool working: studio.busy || video.busy || (recorder.active && !studio.quickMode)
    property string currentStatus: videoMode ? video.status : studio.status
    property string currentDirectory: videoMode ? video.outputDirectory : studio.outputDirectory
    property string currentSaved: videoMode ? video.savedPath : studio.savedPath
    readonly property bool narrow: width < 1100

    function home(path) { return path.replace(/^\/home\/[^/]+/, "~") }
    function acceptCurrent() {
        if (textEditor.active)
            textEditor.commit();
        root.contentItem.forceActiveFocus();
        if (videoMode) {
            videoPane.pause();
            if (recordingReview && (videoUnchanged || videoSavedCurrent))
                video.finish();
            else if (!videoSavedCurrent && !videoUnchanged)
                video.exportEdited(videoPane.clipStart, videoPane.clipEnd, videoPane.muted, videoPane.cuts);
        } else if (studio.recoveryAction.length)
            studio.retryOutput();
        else
            studio.accept();
    }
    // Escape peels one layer at a time: typing, a drag, the selection, the
    // tool, then Edit itself.
    function escapeEditor() {
        if (textEditor.active) textEditor.commit();
        else if (drawArea.pressed) { drawArea.interaction = "none"; guide.requestPaint(); }
        else if (studio.selectedAnnotation.type !== undefined) studio.clearSelection();
        else if (root.tool !== "select") root.tool = "select";
        else if (studio.quickMode) studio.showFinishes();
        else root.editing = false;
    }
    function showFinish() {
        if (textEditor.active) textEditor.commit();
        if (studio.quickMode) studio.showFinishes();
        else root.editing = false;
    }
    property var annotationTools: [
        { key: "select", label: "Select", shortcut: "V" }, { key: "crop", label: "Crop", shortcut: "C" },
        { key: "arrow", label: "Arrow", shortcut: "A" }, { key: "line", label: "Line", shortcut: "L" },
        { key: "box", label: "Box", shortcut: "B" }, { key: "ellipse", label: "Oval", shortcut: "O" },
        { key: "highlight", label: "Highlight", shortcut: "H" }, { key: "redact", label: "Redact", shortcut: "R" },
        { key: "blur", label: "Blur", shortcut: "G" }, { key: "pen", label: "Pen", shortcut: "P" },
        { key: "step", label: "Steps", shortcut: "N" }, { key: "text", label: "Text", shortcut: "T" }
    ]
    property string toolDescription: ({
            select: "Click a mark to select it, drag to move it, or drag a handle to resize. Double-click a label to change its words.",
            crop: "Drag a frame over the full image. Marks outside the frame are kept. Press V when you are done.",
            arrow: "Drag from the tail to the tip.",
            line: "Drag to draw a line.",
            box: "Drag to draw an outline box.",
            ellipse: "Drag to draw an oval.",
            highlight: "Drag across the part you want to stand out.",
            redact: "Drag over private details. Their pixels are replaced in the saved image.",
            blur: "Drag to soften an area. Use Redact for anything private.",
            pen: "Draw freehand.",
            step: "Click to place the next number.",
            text: "Click where the label should go, then type. Click a label to change it."
        })[tool] || ""

    onClosing: function (close) {
        if (!visible) return;
        if (textEditor.active) textEditor.commit();
        if (root.working || recorder.active) {
            close.accepted = false;
            return;
        }
        if (studio.quickMode) { close.accepted = false; studio.dismissQuick(); }
        else Qt.quit();
    }
    Binding { target: studio; property: "editing"; value: root.editing && !root.videoMode && root.visible }
    onEditingChanged: if (!editing && textEditor.active) textEditor.commit()
    onToolChanged: if (textEditor.active) textEditor.commit()
    Connections {
        target: studio
        function onEditorRequested() { root.editing = true; root.videoMode = false; root.tool = "select"; }
        function onSourceChanged() {
            textEditor.cancel();
            root.editing = false;
            root.videoMode = false;
            root.tool = "select";
        }
    }
    Connections {
        target: video
        function onOpening() {
            root.videoMode = true;
            root.editing = false;
            root.recordingReview = false;
            root.savedSignature = "";
        }
        function onExported() { root.savedSignature = videoPane.signature; }
    }
    Shortcut {
        sequence: "Ctrl+O"
        enabled: root.shortcutsAllowed && !root.working && !studio.quickMode
        onActivated: openDialog.open()
    }
    Shortcut {
        sequences: ["Ctrl+S", "Ctrl+Shift+C"]
        enabled: root.shortcutsAllowed && (root.videoMode || studio.hasImage)
        onActivated: root.acceptCurrent()
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: root.shortcutsAllowed && !root.videoMode && studio.hasImage && !root.working
        onActivated: root.acceptCurrent()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.shortcutsAllowed && !root.videoMode && studio.hasImage && !root.editing && (!root.activeFocusItem || root.activeFocusItem.objectName !== "finishChoice")
        onActivated: root.acceptCurrent()
    }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode
        onActivated: studio.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode
        onActivated: studio.redo()
    }
    Shortcut {
        sequence: "Escape"
        enabled: (root.shortcutsAllowed || textEditor.active) && !root.videoMode && studio.hasImage && (root.editing || studio.quickMode)
        onActivated: root.editing ? root.escapeEditor() : studio.showFinishes()
    }
    Shortcut {
        sequence: "E"
        enabled: root.shortcutsAllowed && !root.videoMode && studio.hasImage && !root.editing && !root.working
        onActivated: root.editing = true
    }
    Repeater {
        model: root.annotationTools
        Item {
            required property var modelData
            Shortcut {
                sequence: modelData.shortcut
                enabled: root.shortcutsAllowed && root.editing && !root.videoMode
                onActivated: root.tool = modelData.key
            }
        }
    }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode && studio.selectedAnnotation.type !== undefined
        onActivated: studio.deleteSelected()
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode && studio.selectedAnnotation.type !== undefined
        onActivated: studio.duplicateSelected()
    }
    Shortcut {
        sequences: ["F2"]
        enabled: root.shortcutsAllowed && root.editing && studio.selectedAnnotation.type === "text"
        onActivated: textEditor.editSelected()
    }
    readonly property bool nudging: root.shortcutsAllowed && root.editing && !root.videoMode && studio.selectedAnnotation.type !== undefined
    Shortcut { sequence: "Left"; enabled: root.nudging; onActivated: studio.nudgeSelected(-1, 0) }
    Shortcut { sequence: "Right"; enabled: root.nudging; onActivated: studio.nudgeSelected(1, 0) }
    Shortcut { sequence: "Up"; enabled: root.nudging; onActivated: studio.nudgeSelected(0, -1) }
    Shortcut { sequence: "Down"; enabled: root.nudging; onActivated: studio.nudgeSelected(0, 1) }
    Shortcut { sequence: "Shift+Left"; enabled: root.nudging; onActivated: studio.nudgeSelected(-10, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: root.nudging; onActivated: studio.nudgeSelected(10, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: root.nudging; onActivated: studio.nudgeSelected(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: root.nudging; onActivated: studio.nudgeSelected(0, 10) }

    FileDialog {
        id: openDialog
        title: "Open an image or recording"
        nameFilters: ["Images and recordings (*.png *.jpg *.jpeg *.webp *.bmp *.avif *.heic *.mp4 *.webm *.mkv *.mov *.m4v)", "Images (*.png *.jpg *.jpeg *.webp *.bmp)", "Recordings (*.mp4 *.webm *.mkv *.mov *.m4v)"]
        onAccepted: studio.open(selectedFile)
    }
    FolderDialog {
        id: imageFolderDialog
        title: "Save screenshots in"
        onAccepted: studio.setOutputDirectory(selectedFolder)
    }
    FolderDialog {
        id: videoFolderDialog
        title: "Save recordings and clips in"
        onAccepted: video.setOutputDirectory(selectedFolder)
    }
    ConfirmDialog {
        id: originalsDialog
        title: "Delete private originals?"
        message: "This removes the unedited copies kept after earlier captures. Your finished screenshots and editable drafts are not affected."
        confirmText: "Delete originals"
        onConfirmed: studio.clearOriginals()
    }
    ConfirmDialog {
        id: draftDeleteDialog
        property string draftId: ""
        title: "Delete this draft?"
        message: "Its private source image and marks are removed. Files you already saved stay where they are."
        confirmText: "Delete draft"
        onConfirmed: studio.deleteDraft(draftId)
    }
    component SectionLabel: Text {
        color: theme.muted
        font.family: theme.fontFamily
        font.pixelSize: 10
        font.letterSpacing: 0.8
    }
    component MenuAction: StudioButton {
        id: action
        property string detail: ""
        Layout.fillWidth: true
        implicitHeight: detail.length ? 52 : 40
        contentItem: RowLayout {
            spacing: 10
            Glyph {
                name: action.glyph
                ink: action.ink
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text {
                    text: action.text
                    color: action.ink
                    font.family: theme.fontFamily
                    font.pixelSize: 13
                    font.weight: action.primary ? Font.DemiBold : Font.Normal
                }
                Text {
                    visible: text.length > 0
                    text: action.detail
                    color: action.primary ? theme.alpha(theme.onAccent, 0.8) : theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }
    }
    Popup {
        id: captureMenu
        x: root.width - width - 20
        y: 60
        width: 340
        padding: 14
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: "transparent" }
        background: Rectangle {
            color: theme.alpha(theme.background, 1)
            radius: theme.radius
            border.width: 2
            border.color: theme.frame
        }
        ColumnLayout {
            width: parent.width
            spacing: 8
            MenuAction {
                text: "Screenshot"
                detail: "Click a window, drag an area, or press F"
                glyph: "capture"
                primary: true
                onClicked: { captureMenu.close(); studio.capture(true); }
            }
            MenuAction {
                text: "Record video"
                detail: "Choose an area, window or display to record"
                glyph: "record"
                enabled: !recorder.active
                onClicked: { captureMenu.close(); studio.captureVideo(); }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
            MenuAction {
                text: "Repeat last area"
                glyph: "capture"
                quiet: true
                enabled: studio.hasLastArea
                hint: studio.hasLastArea ? "Capture the same part of the screen again" : "Capture an area first"
                onClicked: { captureMenu.close(); studio.repeatLastArea(); }
            }
            MenuAction {
                text: "Whole active display"
                glyph: "display"
                quiet: true
                onClicked: { captureMenu.close(); studio.capture(false); }
            }
            Text {
                text: "The screen freezes while you choose."
                font.pixelSize: 11
                font.family: theme.fontFamily
                color: theme.faint
            }
        }
    }
    Popup {
        id: settingsPopup
        x: root.width - width - 20
        y: 60
        width: Math.min(460, root.width - 40)
        height: Math.min(settingsColumn.implicitHeight + 36, root.height - 90)
        padding: 18
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: "transparent" }
        onOpened: shortcuts.refresh()
        background: Rectangle {
            color: theme.alpha(theme.background, 1)
            radius: theme.radius
            border.width: 2
            border.color: theme.frame
        }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: settingsColumn
                width: settingsPopup.availableWidth - 10
                spacing: 14
                Text { text: "Settings"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 17; font.weight: Font.Medium }
                SectionLabel { text: "SHORTCUTS" }
                ShortcutPanel { Layout.fillWidth: true; compact: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                SectionLabel { text: "SAVE FOLDERS" }
                Repeater {
                    model: [
                        { label: "Screenshots", path: studio.outputDirectory, video: false },
                        { label: "Recordings", path: video.outputDirectory, video: true }
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.maximumWidth: settingsColumn.width
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.label; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 12 }
                            Text { Layout.fillWidth: true; text: root.home(modelData.path); color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 11; elide: Text.ElideMiddle }
                        }
                        StudioButton {
                            text: "Change"
                            quiet: true
                            implicitHeight: 30
                            onClicked: { settingsPopup.close(); modelData.video ? videoFolderDialog.open() : imageFolderDialog.open(); }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                SectionLabel { text: "AFTER A CAPTURE IS COPIED" }
                RecordToggle {
                    text: "Show a notification with the save folder"
                    checked: notificationSetting.enabled
                    onToggled: notificationSetting.enabled = checked
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                SectionLabel { text: "PRIVACY" }
                RecordToggle {
                    text: "Keep an unedited private copy of each capture"
                    checked: studio.keepOriginals
                    onToggled: studio.keepOriginals = checked
                }
                Text {
                    Layout.fillWidth: true
                    text: "These copies can include anything you redacted. They stay in " + root.home(studio.originalsFolder) + " until you delete them. " + studio.originalsSummary
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                StudioButton {
                    text: "Delete private originals…"
                    glyph: "trash"
                    quiet: true
                    enabled: !studio.busy && studio.originalsCount > 0
                    onClicked: { settingsPopup.close(); originalsDialog.open(); }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: "Omaframe 0.2 · Everything stays on this computer. No accounts, uploads or telemetry."
                    color: theme.faint
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
        }
    }
    QtObject {
        id: notificationSetting
        property bool enabled: studio.notifications
        onEnabledChanged: studio.notifications = enabled
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: theme.alpha(theme.background, 1)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 20
                spacing: 10
                Rectangle {
                    width: 30
                    height: 30
                    radius: theme.radius
                    color: theme.accent
                    Glyph {
                        anchors.centerIn: parent
                        name: "capture"
                        ink: theme.onAccent
                        width: 19
                        height: 19
                    }
                }
                Text {
                    text: "omaframe"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: theme.text
                }
                Text {
                    Layout.leftMargin: 6
                    visible: !root.narrow
                    text: root.videoMode ? (root.recordingReview ? "Review recording" : "Edit video") : studio.hasImage ? (root.editing ? "Edit screenshot" : "Choose a finish") : ""
                    color: theme.muted
                    font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
                StudioButton {
                    visible: !studio.quickMode
                    text: root.narrow ? "" : "Open"
                    glyph: "image"
                    quiet: true
                    enabled: !root.working
                    hint: "Open an image or recording · Ctrl+O"
                    onClicked: openDialog.open()
                }
                StudioButton {
                    visible: !studio.quickMode
                    text: "New capture"
                    glyph: "capture"
                    enabled: !root.working
                    onClicked: captureMenu.open()
                }
                StudioButton {
                    glyph: "settings"
                    quiet: true
                    hint: "Settings"
                    onClicked: settingsPopup.open()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.separator
            }
        }

        // Start screen: no image or video open.
        Flickable {
            id: startScreen
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.videoMode && !studio.hasImage
            clip: true
            contentWidth: width
            contentHeight: startColumn.implicitHeight + 64
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            component ActionCard: Rectangle {
                id: card
                property string glyph
                property string title
                property string detail
                property string key
                property bool accent: false
                signal activated()
                Layout.fillWidth: true
                Layout.preferredHeight: 132
                radius: theme.radius
                color: cardMouse.pressed ? theme.pressedFill : cardMouse.containsMouse ? theme.hoverFill : theme.controlFill
                border.width: card.activeFocus ? 2 : 1
                border.color: card.activeFocus ? theme.focusBorder : card.accent ? theme.alpha(theme.accent, 0.7) : cardMouse.containsMouse ? theme.hoverBorder : theme.controlBorder
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: title
                Keys.onReturnPressed: activated()
                Keys.onSpacePressed: activated()
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Glyph { name: card.glyph; ink: card.accent ? theme.accent : theme.text; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            visible: card.key.length > 0
                            implicitWidth: keyLabel.implicitWidth + 14
                            implicitHeight: 22
                            radius: theme.radius
                            color: "transparent"
                            border.width: 1
                            border.color: theme.controlBorder
                            Text { id: keyLabel; anchors.centerIn: parent; text: card.key; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 11 }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: card.title; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 15; font.weight: Font.Medium }
                    Text { Layout.fillWidth: true; text: card.detail; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                }
                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: card.enabled
                    onClicked: card.activated()
                }
            }
            ColumnLayout {
                id: startColumn
                width: Math.min(820, startScreen.width - 48)
                x: (startScreen.width - width) / 2
                y: 34
                spacing: 20
                ColumnLayout {
                    spacing: 6
                    Text { text: studio.welcomed ? "What would you like to capture?" : "Welcome to Omaframe"; color: theme.text; font.pixelSize: 24; font.weight: Font.Medium }
                    Text {
                        Layout.fillWidth: true
                        text: "Screenshots and screen recordings for Omarchy. Everything stays on this computer."
                        color: theme.muted
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ActionCard {
                        glyph: "capture"
                        title: "Screenshot"
                        detail: "Click a window, drag an area, or press F for the whole display."
                        key: shortcuts.screenshotKey
                        accent: true
                        enabled: !root.working
                        onActivated: studio.capture(true)
                    }
                    ActionCard {
                        glyph: "record"
                        title: "Record video"
                        detail: "Same selection, with sound and a Stop button kept out of the video."
                        key: shortcuts.recordKey
                        enabled: !root.working && !recorder.active
                        onActivated: studio.captureVideo()
                    }
                    ActionCard {
                        glyph: "image"
                        title: "Open a file"
                        detail: "Edit an image or trim a video. You can also drop a file here."
                        key: "Ctrl+O"
                        enabled: !root.working
                        onActivated: openDialog.open()
                    }
                }
                Rectangle {
                    visible: !shortcuts.ready || !studio.welcomed || shortcuts.message.length > 0
                    Layout.fillWidth: true
                    implicitHeight: shortcutColumn.implicitHeight + 36
                    radius: theme.radius
                    color: theme.controlFill
                    border.width: 1
                    border.color: theme.controlBorder
                    ColumnLayout {
                        id: shortcutColumn
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10
                        Text { text: "Shortcuts"; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                        ShortcutPanel { Layout.fillWidth: true }
                    }
                }
                Rectangle {
                    visible: !studio.welcomed
                    Layout.fillWidth: true
                    implicitHeight: welcomeColumn.implicitHeight + 36
                    radius: theme.radius
                    color: theme.alpha(theme.accent, 0.07)
                    border.width: 1
                    border.color: theme.alpha(theme.accent, 0.35)
                    ColumnLayout {
                        id: welcomeColumn
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10
                        Text { text: "How it works"; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                        Repeater {
                            model: [
                                "Press Print. The screen freezes. Click a window, drag an area, or press F for the display. Tab switches to video.",
                                "Press a number to pick a finish. It is copied and saved at once. Press E first to crop, blur or add labels.",
                                "Paste anywhere. Screenshots go to " + root.home(studio.outputDirectory) + ", recordings to " + root.home(video.outputDirectory) + "."
                            ]
                            RowLayout {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 10
                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    width: 22; height: 22; radius: theme.radius
                                    color: theme.selectedFill
                                    Text { anchors.centerIn: parent; text: index + 1; color: theme.selectedText; font.pixelSize: 11 }
                                }
                                Text { Layout.fillWidth: true; text: modelData; color: theme.text; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.2 }
                            }
                        }
                        StudioButton {
                            Layout.alignment: Qt.AlignRight
                            text: "Got it"
                            quiet: true
                            onClicked: studio.welcomed = true
                        }
                    }
                }
                ColumnLayout {
                    visible: studio.drafts.length > 0
                    Layout.fillWidth: true
                    spacing: 8
                    SectionLabel { text: "RECENT EDITS" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: startColumn.width > 640 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 8
                        Repeater {
                            model: studio.drafts
                            delegate: Rectangle {
                                id: draftCard
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 64
                                radius: theme.radius
                                color: draftMouse.containsMouse ? theme.hoverFill : theme.controlFill
                                border.width: 1
                                border.color: theme.controlBorder
                                MouseArea {
                                    id: draftMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: studio.resumeDraft(draftCard.modelData.id)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 72
                                        Layout.fillHeight: true
                                        radius: theme.radius
                                        color: theme.well
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            source: draftCard.modelData.image
                                            sourceSize.width: 144
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { Layout.fillWidth: true; text: draftCard.modelData.name; color: theme.text; font.pixelSize: 12; elide: Text.ElideMiddle }
                                        Text {
                                            text: draftCard.modelData.when + " · " + draftCard.modelData.edits + (draftCard.modelData.edits === 1 ? " mark" : " marks") + (draftCard.modelData.exported ? " · saved" : "")
                                            color: theme.muted
                                            font.pixelSize: 11
                                        }
                                    }
                                    StudioButton {
                                        glyph: "trash"
                                        hint: "Delete this draft"
                                        quiet: true
                                        implicitHeight: 30
                                        onClicked: {
                                            draftDeleteDialog.draftId = draftCard.modelData.id;
                                            draftDeleteDialog.open();
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Drafts keep a private copy of the unedited capture so you can change your marks later. Delete a draft when you are done with it."
                        color: theme.faint
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
                StudioButton {
                    text: "Try the editor on a sample image"
                    glyph: "spark"
                    quiet: true
                    onClicked: studio.loadDemo(0)
                }
            }
        }

        // Image workspace
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            visible: !root.videoMode && studio.hasImage
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 20
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        implicitWidth: modeRow.implicitWidth + 8
                        implicitHeight: 40
                        radius: theme.radius
                        color: theme.controlFill
                        border.color: theme.controlBorder
                        RowLayout {
                            id: modeRow
                            anchors.centerIn: parent
                            spacing: 2
                            StudioButton {
                                text: studio.quickMode ? "Finishes" : "Finish"
                                glyph: "spark"
                                quiet: !selected
                                selected: !root.editing
                                implicitHeight: 32
                                hint: studio.quickMode ? "Back to the finish picker · Esc" : "Border and background"
                                onClicked: root.showFinish()
                            }
                            StudioButton {
                                text: "Edit"
                                glyph: "crop"
                                quiet: !selected
                                selected: root.editing
                                implicitHeight: 32
                                hint: "Crop, mark up and redact · E"
                                onClicked: root.editing = true
                            }
                        }
                    }
                    Text {
                        Layout.leftMargin: 6
                        text: studio.name
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: 260
                        color: theme.text
                        font.pixelSize: 12
                    }
                    Text {
                        text: studio.dimensions
                        color: theme.faint
                        font.pixelSize: 12
                    }
                    Rectangle {
                        visible: studio.demo
                        width: 58
                        height: 19
                        radius: theme.radius
                        color: theme.selectedFill
                        Text {
                            anchors.centerIn: parent
                            text: "SAMPLE"
                            font.pixelSize: 9
                            color: theme.selectedText
                        }
                    }
                    Item { Layout.fillWidth: true }
                    StudioButton {
                        visible: root.editing
                        glyph: "undo"
                        text: root.narrow ? "" : "Undo"
                        quiet: true
                        implicitHeight: 34
                        enabled: studio.canUndo && !studio.busy
                        hint: "Undo · Ctrl+Z"
                        onClicked: studio.undo()
                    }
                    StudioButton {
                        visible: root.editing
                        glyph: "redo"
                        text: root.narrow ? "" : "Redo"
                        quiet: true
                        implicitHeight: 34
                        enabled: studio.canRedo && !studio.busy
                        hint: "Redo · Ctrl+Shift+Z"
                        onClicked: studio.redo()
                    }
                    Text {
                        visible: !root.editing
                        text: studio.style === 8 ? "RAW" : studio.styles[studio.style].toUpperCase()
                        font.pixelSize: 10
                        color: theme.muted
                    }
                    StudioButton {
                        visible: !studio.quickMode
                        glyph: "close"
                        quiet: true
                        implicitHeight: 34
                        enabled: !root.working
                        hint: "Close this image. Editable drafts stay in Recent edits."
                        onClicked: { textEditor.cancel(); studio.closeImage(); }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius
                    color: theme.well
                    border.color: theme.controlBorder
                    clip: true
                    Canvas {
                        id: dotGrid
                        anchors.fill: parent
                        Connections {target: theme; function onChanged() { dotGrid.requestPaint() }}
                        onPaint: {
                            let c = getContext("2d");
                            c.reset();
                            c.fillStyle = theme.alpha(theme.text, 0.12);
                            for (let x = 18; x < width; x += 22)
                                for (let y = 18; y < height; y += 22) {
                                    c.beginPath();
                                    c.arc(x, y, 0.7, 0, Math.PI * 2);
                                    c.fill();
                                }
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                    }
                    Item {
                        id: canvasArea
                        anchors.fill: parent
                        anchors.margins: root.editing ? 16 : 26
                        Image {
                            id: preview
                            anchors.fill: parent
                            source: studio.hasImage && studio.revision > 0 ? "image://frames/" + (root.editing ? (root.tool === "crop" ? "uncropped" : "source") : "preview") + "?" + studio.revision : ""
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            asynchronous: true
                            retainWhileLoading: true
                        }
                        Item {
                            id: editSurface
                            anchors.centerIn: preview
                            width: preview.paintedWidth
                            height: preview.paintedHeight
                            visible: root.editing
                            MouseArea {
                                id: drawArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                enabled: !studio.busy
                                cursorShape: hoverHandle >= 0 ? Qt.SizeFDiagCursor
                                    : hoverMark.type !== undefined && root.tool !== "crop" ? Qt.SizeAllCursor
                                    : root.tool === "select" ? Qt.ArrowCursor
                                    : root.tool === "text" ? Qt.IBeamCursor : Qt.CrossCursor
                                property real startX: 0
                                property real startY: 0
                                property real endX: 0
                                property real endY: 0
                                property string interaction: "none"
                                property int handle: -1
                                property int hoverHandle: -1
                                property var hoverMark: ({})
                                property string pressedType: ""
                                property var strokePoints: []
                                readonly property bool moved: Math.hypot(endX - startX, endY - startY) > 3
                                readonly property bool selectionShown: root.tool !== "crop" && !textEditor.active
                                function handleAt(px, py) {
                                    const selected = studio.selectedAnnotation;
                                    if (!selected.type || !selectionShown)
                                        return -1;
                                    if (selected.type === "text" || selected.type === "step") {
                                        const left = selected.boundX * width, top = selected.boundY * height;
                                        const right = (selected.boundX + selected.boundW) * width;
                                        const bottom = (selected.boundY + selected.boundH) * height;
                                        const corners = [[left, top], [right, top], [right, bottom], [left, bottom]];
                                        const small = selected.type === "text" && (right - left < 60 || bottom - top < 28);
                                        if (small)
                                            return Math.hypot(px - right, py - bottom) < 10 ? 2 : -1;
                                        for (let i = 0; i < corners.length; ++i)
                                            if (Math.hypot(px - corners[i][0], py - corners[i][1]) < 14)
                                                return i;
                                        return -1;
                                    }
                                    const points = selected.type === "line" || selected.type === "arrow"
                                        ? [[selected.x1, selected.y1], [selected.x2, selected.y2]]
                                        : [[Math.min(selected.x1, selected.x2), Math.min(selected.y1, selected.y2)],
                                           [Math.max(selected.x1, selected.x2), Math.min(selected.y1, selected.y2)],
                                           [Math.max(selected.x1, selected.x2), Math.max(selected.y1, selected.y2)],
                                           [Math.min(selected.x1, selected.x2), Math.max(selected.y1, selected.y2)]];
                                    for (let i = 0; i < points.length; ++i)
                                        if (Math.hypot(px - points[i][0] * width, py - points[i][1] * height) < 14)
                                            return i;
                                    return -1;
                                }
                                onPressed: function (mouse) {
                                    // A click outside the label being typed finishes it.
                                    if (textEditor.active) {
                                        textEditor.commit();
                                        interaction = "none";
                                        mouse.accepted = true;
                                        return;
                                    }
                                    forceActiveFocus();
                                    hoverMark = ({});
                                    hoverHandle = -1;
                                    startX = endX = mouse.x;
                                    startY = endY = mouse.y;
                                    const nx = mouse.x / width, ny = mouse.y / height;
                                    if (mouse.button === Qt.RightButton) {
                                        interaction = "none";
                                        if (root.tool !== "crop") {
                                            root.tool = "select";
                                            studio.selectAt(nx, ny);
                                        }
                                        return;
                                    }
                                    handle = handleAt(mouse.x, mouse.y);
                                    if (handle >= 0) {
                                        interaction = "resize";
                                        guide.requestPaint();
                                        return;
                                    }
                                    if (root.tool === "crop") {
                                        interaction = "draw";
                                        guide.requestPaint();
                                        return;
                                    }
                                    // Existing marks stay editable with any tool. Drawing tools
                                    // pick up filled areas only by their edge, so a new mark can
                                    // still start inside one.
                                    const hit = studio.hitAt(nx, ny, root.tool !== "select");
                                    if (hit.index !== undefined) {
                                        studio.select(hit.index);
                                        pressedType = hit.type;
                                        interaction = "move";
                                        guide.requestPaint();
                                        return;
                                    }
                                    studio.clearSelection();
                                    if (root.tool === "select")
                                        interaction = "none";
                                    else if (root.tool === "text")
                                        interaction = "newText";
                                    else if (root.tool === "pen") {
                                        strokePoints = [{ x: nx, y: ny }];
                                        interaction = "stroke";
                                    } else
                                        interaction = "draw";
                                    guide.requestPaint();
                                }
                                onPositionChanged: function (mouse) {
                                    if (!pressed) {
                                        hoverHandle = handleAt(mouse.x, mouse.y);
                                        hoverMark = root.tool === "crop" || hoverHandle >= 0 ? ({}) : studio.hitAt(mouse.x / width, mouse.y / height, root.tool !== "select");
                                        return;
                                    }
                                    endX = Math.max(0, Math.min(width, mouse.x));
                                    endY = Math.max(0, Math.min(height, mouse.y));
                                    if (interaction === "stroke") {
                                        const last = strokePoints[strokePoints.length - 1];
                                        if (!last || Math.hypot(endX - last.x * width, endY - last.y * height) >= 2)
                                            strokePoints = strokePoints.concat([{ x: endX / width, y: endY / height }]);
                                    }
                                    guide.requestPaint();
                                }
                                onExited: { hoverMark = ({}); hoverHandle = -1; }
                                onReleased: function (mouse) {
                                    if (mouse.button === Qt.RightButton)
                                        return;
                                    endX = Math.max(0, Math.min(width, mouse.x));
                                    endY = Math.max(0, Math.min(height, mouse.y));
                                    if (interaction === "resize")
                                        studio.resizeSelected(handle, endX / width, endY / height);
                                    else if (interaction === "move") {
                                        if (moved)
                                            studio.moveSelected((endX - startX) / width, (endY - startY) / height);
                                        else if (pressedType === "text" && root.tool === "text")
                                            textEditor.editSelected();
                                    } else if (interaction === "stroke") {
                                        strokePoints = strokePoints.concat([{ x: endX / width, y: endY / height }]);
                                        studio.addStroke(strokePoints);
                                        strokePoints = [];
                                    } else if (interaction === "newText")
                                        textEditor.create(startX / width, startY / height);
                                    else if (interaction === "draw")
                                        studio.edit(root.tool, startX / width, startY / height, endX / width, endY / height);
                                    interaction = "none";
                                    hoverMark = ({});
                                    hoverHandle = handleAt(endX, endY);
                                    guide.requestPaint();
                                }
                                onDoubleClicked: function (mouse) {
                                    const hit = studio.hitAt(mouse.x / width, mouse.y / height);
                                    if (hit.type === "text") {
                                        interaction = "none";
                                        studio.select(hit.index);
                                        textEditor.editSelected();
                                    }
                                }
                                onCanceled: { interaction = "none"; strokePoints = []; guide.requestPaint(); }
                                Canvas {
                                    id: guide
                                    anchors.fill: parent
                                    onPaint: {
                                        let c = getContext("2d");
                                        c.reset();
                                        if (!drawArea.pressed || drawArea.interaction === "none")
                                            return;
                                        let x = drawArea.startX, y = drawArea.startY, w = drawArea.endX - x, h = drawArea.endY - y;
                                        c.strokeStyle = theme.accent;
                                        c.lineWidth = 2;
                                        c.setLineDash([5, 3]);
                                        if (drawArea.interaction === "stroke") {
                                            c.setLineDash([]);
                                            c.beginPath();
                                            for (let i = 0; i < drawArea.strokePoints.length; ++i) {
                                                const point = drawArea.strokePoints[i];
                                                if (i === 0) c.moveTo(point.x * width, point.y * height);
                                                else c.lineTo(point.x * width, point.y * height);
                                            }
                                            c.stroke();
                                        } else if (drawArea.interaction === "resize") {
                                            const mark = studio.selectedAnnotation;
                                            if (!mark.type)
                                                return;
                                            const left = mark.boundX * width, top = mark.boundY * height;
                                            const right = (mark.boundX + mark.boundW) * width;
                                            const bottom = (mark.boundY + mark.boundH) * height;
                                            const corners = [[left, top], [right, top], [right, bottom], [left, bottom]];
                                            const opposite = corners[(drawArea.handle + 2) % 4];
                                            if (mark.type === "text" || mark.type === "step") {
                                                const pivot = mark.type === "step" ? [(left + right) / 2, (top + bottom) / 2] : opposite;
                                                const old = [corners[drawArea.handle][0] - pivot[0], corners[drawArea.handle][1] - pivot[1]];
                                                const now = [drawArea.endX - pivot[0], drawArea.endY - pivot[1]];
                                                const ratio = Math.max(0.1, Math.min(30, (old[0] * now[0] + old[1] * now[1]) / Math.max(1, old[0] * old[0] + old[1] * old[1])));
                                                const nextW = (right - left) * ratio, nextH = (bottom - top) * ratio;
                                                const nextLeft = mark.type === "step" ? pivot[0] - nextW / 2 : drawArea.handle === 0 || drawArea.handle === 3 ? opposite[0] - nextW : opposite[0];
                                                const nextTop = mark.type === "step" ? pivot[1] - nextH / 2 : drawArea.handle === 0 || drawArea.handle === 1 ? opposite[1] - nextH : opposite[1];
                                                c.strokeRect(nextLeft, nextTop, nextW, nextH);
                                                if (mark.type === "text") {
                                                    c.setLineDash([]);
                                                    c.font = "12px monospace";
                                                    const label = Math.max(8, Math.min(4096, Math.round(mark.fontPx * ratio))) + " px";
                                                    c.fillStyle = theme.alpha(theme.background, 0.85);
                                                    c.fillRect(nextLeft, nextTop - 22, c.measureText(label).width + 12, 18);
                                                    c.fillStyle = theme.text;
                                                    c.fillText(label, nextLeft + 6, nextTop - 9);
                                                }
                                            } else if (mark.type === "line" || mark.type === "arrow") {
                                                const other = drawArea.handle === 0 ? [mark.x2 * width, mark.y2 * height] : [mark.x1 * width, mark.y1 * height];
                                                c.beginPath(); c.moveTo(other[0], other[1]); c.lineTo(drawArea.endX, drawArea.endY); c.stroke();
                                            } else {
                                                c.strokeRect(opposite[0], opposite[1], drawArea.endX - opposite[0], drawArea.endY - opposite[1]);
                                            }
                                        } else if (drawArea.interaction === "move" || drawArea.interaction === "newText") {
                                            return;
                                        } else if (root.tool === "arrow" || root.tool === "line") {
                                            c.beginPath();
                                            c.moveTo(x, y);
                                            c.lineTo(x + w, y + h);
                                            c.stroke();
                                        } else if (root.tool === "ellipse") {
                                            if (Math.abs(w) > 1 && Math.abs(h) > 1) {
                                                c.beginPath();
                                                c.save();
                                                c.translate(x + w / 2, y + h / 2);
                                                c.scale(Math.abs(w) / 2, Math.abs(h) / 2);
                                                c.arc(0, 0, 1, 0, Math.PI * 2);
                                                c.restore();
                                                c.stroke();
                                            }
                                        } else if (root.tool !== "step") {
                                            if (root.tool !== "box") {
                                                c.fillStyle = root.tool === "redact" ? theme.alpha(theme.urgent, 0.22) : theme.alpha(theme.accent, 0.18);
                                                c.fillRect(x, y, w, h);
                                            }
                                            c.strokeRect(x, y, w, h);
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                visible: root.tool === "crop" && studio.hasCrop
                                x: studio.cropBounds.x * parent.width
                                y: studio.cropBounds.y * parent.height
                                width: studio.cropBounds.width * parent.width
                                height: studio.cropBounds.height * parent.height
                                color: "transparent"
                                border.width: 2
                                border.color: theme.accent
                            }
                            // Hover: a quiet dashed outline says "this can be picked up".
                            Canvas {
                                id: hoverOutline
                                readonly property var mark: drawArea.hoverMark
                                readonly property bool shown: mark.type !== undefined && !drawArea.pressed && drawArea.selectionShown && mark.index !== undefined && (studio.selectedAnnotation.type === undefined || mark.x !== studio.selectedAnnotation.boundX || mark.y !== studio.selectedAnnotation.boundY)
                                visible: shown
                                x: (mark.x || 0) * parent.width - 4
                                y: (mark.y || 0) * parent.height - 4
                                width: Math.max(1, (mark.w || 0) * parent.width) + 8
                                height: Math.max(1, (mark.h || 0) * parent.height) + 8
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onVisibleChanged: requestPaint()
                                onPaint: {
                                    const c = getContext("2d");
                                    c.reset();
                                    c.strokeStyle = theme.alpha(theme.accent, 0.8);
                                    c.lineWidth = 1.5;
                                    c.setLineDash([4, 3]);
                                    c.strokeRect(1, 1, width - 2, height - 2);
                                }
                            }
                            Item {
                                id: selectedOutline
                                readonly property var mark: studio.selectedAnnotation
                                readonly property bool anchorOnly: mark.type === "text" || mark.type === "step" || mark.type === "pen"
                                readonly property bool compactLabel: mark.type === "text" && (width < 60 || height < 28)
                                readonly property real dragX: drawArea.interaction === "move" ? drawArea.endX - drawArea.startX : 0
                                readonly property real dragY: drawArea.interaction === "move" ? drawArea.endY - drawArea.startY : 0
                                visible: drawArea.selectionShown && mark.type !== undefined
                                x: (anchorOnly ? mark.boundX || 0 : Math.min(mark.x1 || 0, mark.x2 || 0)) * parent.width + dragX
                                y: (anchorOnly ? mark.boundY || 0 : Math.min(mark.y1 || 0, mark.y2 || 0)) * parent.height + dragY
                                width: anchorOnly ? Math.max(1, (mark.boundW || 0) * parent.width) : Math.max(1, Math.abs((mark.x2 || 0) - (mark.x1 || 0)) * parent.width)
                                height: anchorOnly ? Math.max(1, (mark.boundH || 0) * parent.height) : Math.max(1, Math.abs((mark.y2 || 0) - (mark.y1 || 0)) * parent.height)
                                Rectangle {
                                    visible: selectedOutline.mark.type !== "line" && selectedOutline.mark.type !== "arrow"
                                    anchors.fill: parent
                                    anchors.margins: -2
                                    color: "transparent"
                                    border.width: 2
                                    border.color: theme.accent
                                }
                                Repeater {
                                    model: selectedOutline.mark.type === "pen" ? 0 : selectedOutline.compactLabel ? 1 : selectedOutline.mark.type === "line" || selectedOutline.mark.type === "arrow" ? 2 : 4
                                    Rectangle {
                                        required property int index
                                        width: selectedOutline.compactLabel ? 11 : 13
                                        height: width
                                        radius: theme.radius > 0 ? width / 2 : 1
                                        color: theme.background
                                        border.width: 2
                                        border.color: theme.accent
                                        readonly property bool segment: selectedOutline.mark.type === "line" || selectedOutline.mark.type === "arrow"
                                        x: selectedOutline.compactLabel ? selectedOutline.width - width / 2
                                           : selectedOutline.anchorOnly ? (index === 1 || index === 2 ? selectedOutline.width - width / 2 : -width / 2)
                                           : segment ? (index === 0 ? selectedOutline.mark.x1 : selectedOutline.mark.x2) * editSurface.width - selectedOutline.x + selectedOutline.dragX - width / 2
                                                   : (index === 1 || index === 2) ? selectedOutline.width - width / 2 : -width / 2
                                        y: selectedOutline.compactLabel ? selectedOutline.height - height / 2
                                           : selectedOutline.anchorOnly ? (index >= 2 ? selectedOutline.height - height / 2 : -height / 2)
                                           : segment ? (index === 0 ? selectedOutline.mark.y1 : selectedOutline.mark.y2) * editSurface.height - selectedOutline.y + selectedOutline.dragY - height / 2
                                                   : (index >= 2) ? selectedOutline.height - height / 2 : -height / 2
                                    }
                                }
                            }
                            // Labels are typed directly on the image, in their own size and
                            // colors. The rendered copy is hidden until typing ends.
                            Item {
                                id: textEditor
                                property bool active: false
                                property bool creating: false
                                property real anchorX: 0
                                property real anchorY: 0
                                property var mark: ({})
                                readonly property real viewScale: editSurface.width / Math.max(1, studio.workingSize.width)
                                readonly property int fontPx: creating ? studio.newTextPixels : (mark.fontPx || studio.newTextPixels)
                                readonly property real inset: Math.max(4, fontPx * 0.27) * viewScale
                                readonly property bool boxStyle: creating || mark.textStyle !== "shadow"
                                readonly property color ink: creating ? "#ffffff" : (mark.color || "#ffffff")
                                readonly property color fill: creating ? "#151a20" : (mark.background || "#151a20")
                                readonly property real fillOpacity: creating || mark.backgroundOpacity === undefined ? 1 : mark.backgroundOpacity
                                readonly property real maxLine: Math.max(60, studio.sourceSize.width * 0.85 * viewScale - inset * 2)
                                visible: active
                                z: 30
                                x: Math.min(anchorX * editSurface.width, Math.max(0, editSurface.width - width))
                                y: anchorY * editSurface.height
                                width: box.width
                                height: box.height
                                function create(nx, ny) {
                                    creating = true;
                                    mark = ({});
                                    anchorX = nx;
                                    anchorY = ny;
                                    field.text = "";
                                    active = true;
                                    field.forceActiveFocus();
                                }
                                function editSelected() {
                                    const m = studio.selectedAnnotation;
                                    if (m.type !== "text")
                                        return;
                                    creating = false;
                                    mark = m;
                                    anchorX = m.boundX;
                                    anchorY = m.boundY;
                                    field.text = m.text;
                                    studio.beginTextEdit();
                                    active = true;
                                    field.forceActiveFocus();
                                    field.selectAll();
                                }
                                function commit() {
                                    if (!active)
                                        return;
                                    active = false;
                                    const text = field.text;
                                    if (creating) {
                                        if (text.trim().length)
                                            studio.edit("text", anchorX, anchorY, anchorX, anchorY, text);
                                    } else
                                        studio.endTextEdit(text, true);
                                    drawArea.forceActiveFocus();
                                }
                                function cancel() {
                                    if (!active)
                                        return;
                                    active = false;
                                    if (!creating)
                                        studio.endTextEdit("", false);
                                }
                                Rectangle {
                                    id: box
                                    width: Math.min(textEditor.maxLine, Math.max(measure.contentWidth, placeholder.contentWidth) + 4) + textEditor.inset * 2
                                    height: Math.max(field.contentHeight, measure.contentHeight) + textEditor.inset * 2
                                    radius: Math.max(2, textEditor.fontPx * 0.12 * textEditor.viewScale)
                                    color: textEditor.boxStyle ? Qt.rgba(textEditor.fill.r, textEditor.fill.g, textEditor.fill.b, textEditor.fillOpacity) : theme.alpha("#000000", 0.18)
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        color: "transparent"
                                        radius: parent.radius + 2
                                        border.width: 2
                                        border.color: theme.accent
                                    }
                                    TextEdit {
                                        id: field
                                        anchors.fill: parent
                                        leftPadding: textEditor.inset
                                        rightPadding: textEditor.inset
                                        topPadding: textEditor.inset
                                        bottomPadding: textEditor.inset
                                        font.family: "sans-serif"
                                        font.weight: Font.DemiBold
                                        font.pixelSize: Math.max(6, textEditor.fontPx * textEditor.viewScale)
                                        color: textEditor.ink
                                        selectionColor: theme.alpha(theme.accent, 0.55)
                                        selectedTextColor: textEditor.ink
                                        wrapMode: TextEdit.Wrap
                                        horizontalAlignment: textEditor.creating || textEditor.mark.textAlign === "center" || !textEditor.mark.textAlign ? TextEdit.AlignHCenter : textEditor.mark.textAlign === "right" ? TextEdit.AlignRight : TextEdit.AlignLeft
                                        selectByMouse: true
                                        Accessible.name: "Label text"
                                        onTextChanged: if (length > 240) remove(240, length)
                                        onActiveFocusChanged: if (!activeFocus && textEditor.active) textEditor.commit()
                                        Keys.onPressed: function (event) {
                                            if (event.key === Qt.Key_Escape) {
                                                textEditor.commit();
                                                event.accepted = true;
                                            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                                                textEditor.commit();
                                                event.accepted = true;
                                            }
                                        }
                                    }
                                    Text {
                                        id: measure
                                        visible: false
                                        font: field.font
                                        text: field.text.length ? field.text : " "
                                    }
                                    Text {
                                        id: placeholder
                                        visible: field.length === 0
                                        anchors.centerIn: parent
                                        text: "Type a label"
                                        font: field.font
                                        color: Qt.rgba(textEditor.ink.r, textEditor.ink.g, textEditor.ink.b, 0.45)
                                    }
                                }
                                Rectangle {
                                    y: box.height + 8
                                    width: hintText.implicitWidth + 16
                                    height: 24
                                    radius: theme.radius
                                    color: theme.alpha(theme.background, 0.94)
                                    border.width: 1
                                    border.color: theme.controlBorder
                                    Text {
                                        id: hintText
                                        anchors.centerIn: parent
                                        text: "Enter adds a line · Esc or click outside to finish"
                                        color: theme.muted
                                        font.family: theme.fontFamily
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.editing ? (textEditor.active ? "Typing a label. Enter adds a line; Esc or a click outside finishes it." : root.toolDescription) : studio.rendering ? "Refining the preview…" : "Saved at full resolution · " + studio.outputDimensions + " · PNG"
                    font.pixelSize: 11
                    color: theme.faint
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: theme.separator
            }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: root.narrow ? 250 : 286
                color: theme.alpha(theme.background, 1)
                // Edit sidebar: tools stay in one place; the selected mark's
                // settings appear below them.
                ScrollView {
                    visible: root.editing
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        Item { Layout.preferredHeight: 6 }
                        SectionLabel { Layout.leftMargin: 18; text: "TOOLS" }
                        GridLayout {
                            Layout.leftMargin: 14
                            Layout.rightMargin: 14
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: root.annotationTools
                                StudioButton {
                                    id: toolButton
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 34
                                    text: modelData.label
                                    glyph: modelData.key
                                    selected: root.tool === modelData.key
                                    quiet: !selected
                                    hint: modelData.label + " · " + modelData.shortcut
                                    onClicked: root.tool = modelData.key
                                    contentItem: RowLayout {
                                        spacing: 8
                                        Glyph { name: toolButton.glyph; ink: toolButton.ink; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                                        Text { text: toolButton.text; color: toolButton.ink; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true }
                                        Text { text: toolButton.modelData.shortcut; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }
                        StudioButton {
                            visible: root.tool === "crop" && studio.hasCrop
                            Layout.leftMargin: 14
                            Layout.rightMargin: 14
                            Layout.fillWidth: true
                            text: "Clear crop"
                            quiet: true
                            onClicked: studio.clearCrop()
                        }
                        Rectangle {
                            Layout.leftMargin: 14
                            Layout.rightMargin: 14
                            Layout.topMargin: 4
                            Layout.fillWidth: true
                            height: 1
                            color: theme.separator
                        }
                        ColumnLayout {
                            id: selectedInspector
                            readonly property var mark: studio.selectedAnnotation
                            readonly property bool colored: ["text", "step", "arrow", "line", "box", "ellipse", "pen"].includes(mark.type)
                            readonly property var names: ({ text: "LABEL", step: "STEP", arrow: "ARROW", line: "LINE", box: "BOX", ellipse: "OVAL", pen: "PEN STROKE", highlight: "HIGHLIGHT", redact: "REDACTION", blur: "BLUR" })
                            Layout.leftMargin: 18
                            Layout.rightMargin: 18
                            Layout.fillWidth: true
                            spacing: 9
                            visible: mark.type !== undefined && root.tool !== "crop"
                            RowLayout {
                                Layout.fillWidth: true
                                SectionLabel { text: "SELECTED " + (selectedInspector.names[selectedInspector.mark.type] || ""); Layout.fillWidth: true }
                                Text { text: "Layer " + (selectedInspector.mark.layer || 0) + "/" + (selectedInspector.mark.layers || 0); color: theme.faint; font.pixelSize: 10 }
                            }
                            StudioButton {
                                visible: selectedInspector.mark.type === "text"
                                Layout.fillWidth: true
                                text: "Change words"
                                glyph: "text"
                                hint: "Double-click the label or press F2"
                                enabled: !studio.busy
                                onClicked: textEditor.editSelected()
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                StudioButton { text: "Duplicate"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; hint: "Ctrl+D"; enabled: !studio.busy; onClicked: studio.duplicateSelected() }
                                StudioButton { glyph: "trash"; text: "Delete"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; hint: "Delete"; enabled: !studio.busy; onClicked: studio.deleteSelected() }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                StudioButton { text: "Send back"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; enabled: !studio.busy && selectedInspector.mark.layer > 1; onClicked: studio.moveSelectedLayer(-1) }
                                StudioButton { text: "Bring forward"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; enabled: !studio.busy && selectedInspector.mark.layer < selectedInspector.mark.layers; onClicked: studio.moveSelectedLayer(1) }
                            }
                            Text {
                                visible: selectedInspector.mark.type === "text"
                                text: "Size"
                                color: theme.text
                                font.pixelSize: 12
                            }
                            RowLayout {
                                visible: selectedInspector.mark.type === "text"
                                Layout.fillWidth: true
                                spacing: 8
                                NumberField {
                                    id: fontField
                                    from: 8; to: 4096
                                    value: studio.selectedAnnotation.fontPx || 24
                                    step: Math.max(1, Math.round(value / 12))
                                    suffix: "px"
                                    onCommitted: next => studio.setSelectedFontPixels(next)
                                }
                                ThemedSlider {
                                    Layout.fillWidth: true
                                    from: 0; to: 1
                                    value: Math.log2(Math.max(8, studio.selectedAnnotation.fontPx || 24) / 8) / 9
                                    onCommitted: v => studio.setSelectedFontPixels(Math.round(8 * Math.pow(512, v)))
                                }
                            }
                            Text {
                                visible: selectedInspector.mark.type === "text"
                                Layout.fillWidth: true
                                text: "Or drag a corner handle on the image."
                                color: theme.faint
                                font.pixelSize: 10
                            }
                            Text {
                                visible: selectedInspector.colored
                                text: selectedInspector.mark.type === "text" ? "Text color" : "Color"
                                color: theme.text
                                font.pixelSize: 12
                            }
                            RowLayout {
                                visible: selectedInspector.colored
                                spacing: 6
                                Repeater {
                                    model: ["#ffffff", "#151a20", "#e75439", "#eab841", "#459ec7", "#4ca782"]
                                    Rectangle {
                                        required property string modelData
                                        width: 24; height: 24; radius: theme.radius > 0 ? 12 : 2
                                        color: modelData
                                        border.width: studio.selectedAnnotation.color === modelData ? 3 : 1
                                        border.color: studio.selectedAnnotation.color === modelData ? theme.focusBorder : theme.controlBorder
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Color " + modelData
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !studio.busy
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: studio.setSelectedColor(parent.modelData)
                                        }
                                    }
                                }
                                TextField {
                                    id: colorInput
                                    Layout.preferredWidth: 78
                                    Layout.preferredHeight: 28
                                    property bool validColor: /^#[0-9a-fA-F]{6}$/.test(text)
                                    text: selectedInspector.mark.color || ""
                                    placeholderText: "#RRGGBB"
                                    color: theme.text
                                    font.pixelSize: 11
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (validColor)
                                            studio.setSelectedColor(text)
                                        else
                                            text = studio.selectedAnnotation.color || ""
                                    }
                                    background: Rectangle {
                                        color: theme.well
                                        border.width: colorInput.activeFocus ? 2 : 1
                                        border.color: colorInput.validColor ? (colorInput.activeFocus ? theme.focusBorder : theme.controlBorder) : theme.urgent
                                    }
                                }
                            }
                            Text {
                                visible: selectedInspector.mark.type !== "text" && ["step", "arrow", "line", "box", "ellipse", "pen", "blur"].includes(selectedInspector.mark.type)
                                text: (selectedInspector.mark.type === "blur" ? "Strength · " : "Thickness · ") + Number(selectedInspector.mark.size || 1).toFixed(2) + "×"
                                color: theme.text
                                font.pixelSize: 12
                            }
                            ThemedSlider {
                                visible: selectedInspector.mark.type !== "text" && ["step", "arrow", "line", "box", "ellipse", "pen", "blur"].includes(selectedInspector.mark.type)
                                Layout.fillWidth: true
                                from: 0.5; to: 8; stepSize: 0.25
                                value: studio.selectedAnnotation.size || 1
                                onCommitted: v => studio.setSelectedSize(v)
                            }
                            Text {
                                visible: selectedInspector.mark.type === "text"
                                text: "Style"
                                color: theme.text
                                font.pixelSize: 12
                            }
                            RowLayout {
                                visible: selectedInspector.mark.type === "text"
                                Layout.fillWidth: true
                                spacing: 4
                                StudioButton { text: "Caption box"; selected: studio.selectedAnnotation.textStyle === "box"; quiet: !selected; Layout.fillWidth: true; implicitHeight: 32; onClicked: studio.setSelectedTextStyle("box") }
                                StudioButton { text: "Shadow"; selected: studio.selectedAnnotation.textStyle === "shadow"; quiet: !selected; Layout.fillWidth: true; implicitHeight: 32; onClicked: studio.setSelectedTextStyle("shadow") }
                            }
                            RowLayout {
                                visible: selectedInspector.mark.type === "text"
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: ["left", "center", "right"]
                                    StudioButton {
                                        required property string modelData
                                        text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                                        selected: studio.selectedAnnotation.textAlign === modelData
                                        quiet: !selected
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        font.pixelSize: 11
                                        hint: "Align lines " + modelData
                                        onClicked: studio.setSelectedTextAlignment(modelData)
                                    }
                                }
                            }
                            Text {
                                visible: selectedInspector.mark.type === "text" && selectedInspector.mark.textStyle === "box"
                                text: "Box color · " + Math.round((selectedInspector.mark.backgroundOpacity || 0) * 100) + "%"
                                color: theme.text
                                font.pixelSize: 12
                            }
                            RowLayout {
                                visible: selectedInspector.mark.type === "text" && selectedInspector.mark.textStyle === "box"
                                spacing: 6
                                Repeater {
                                    model: ["#151a20", "#ffffff", "#e75439", "#eab841", "#459ec7", "#4ca782"]
                                    Rectangle {
                                        required property string modelData
                                        width: 24; height: 24; radius: theme.radius > 0 ? 12 : 2
                                        color: modelData
                                        border.width: studio.selectedAnnotation.background === modelData ? 3 : 1
                                        border.color: studio.selectedAnnotation.background === modelData ? theme.focusBorder : theme.controlBorder
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Box color " + modelData
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !studio.busy
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: studio.setSelectedBackground(parent.modelData)
                                        }
                                    }
                                }
                                TextField {
                                    id: boxColorInput
                                    Layout.preferredWidth: 78
                                    Layout.preferredHeight: 28
                                    property bool validColor: /^#[0-9a-fA-F]{6}$/.test(text)
                                    text: selectedInspector.mark.background || ""
                                    placeholderText: "#RRGGBB"
                                    color: theme.text
                                    font.pixelSize: 11
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (validColor)
                                            studio.setSelectedBackground(text)
                                        else
                                            text = studio.selectedAnnotation.background || ""
                                    }
                                    background: Rectangle {
                                        color: theme.well
                                        border.width: boxColorInput.activeFocus ? 2 : 1
                                        border.color: boxColorInput.validColor ? (boxColorInput.activeFocus ? theme.focusBorder : theme.controlBorder) : theme.urgent
                                    }
                                }
                            }
                            ThemedSlider {
                                visible: selectedInspector.mark.type === "text" && selectedInspector.mark.textStyle === "box"
                                Layout.fillWidth: true
                                from: 0; to: 1; stepSize: 0.05
                                value: studio.selectedAnnotation.backgroundOpacity || 0
                                Accessible.name: "Box opacity"
                                onCommitted: v => studio.setSelectedBackgroundOpacity(v)
                            }
                            Text {
                                visible: selectedInspector.mark.type === "redact"
                                Layout.fillWidth: true
                                text: "Redactions replace pixels with a solid fill and cannot be recolored."
                                color: theme.muted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                        Text {
                            visible: !selectedInspector.visible
                            Layout.leftMargin: 18
                            Layout.rightMargin: 18
                            Layout.fillWidth: true
                            text: root.toolDescription + (root.tool !== "select" && root.tool !== "crop" ? "\n\nClick any mark to adjust it, or right-click to select it." : "")
                            color: theme.muted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            lineHeight: 1.2
                        }
                        Item { Layout.preferredHeight: 12 }
                    }
                }
                // Finish sidebar
                ScrollView {
                    visible: !root.editing
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        Item { Layout.preferredHeight: 4 }
                        SectionLabel { Layout.leftMargin: 20; text: "FINISH" }
                        GridLayout {
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 10
                            Repeater {
                                model: 8
                                Item {
                                    id: finishChoice
                                    objectName: "finishChoice"
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 90
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        height: 68
                                        radius: theme.radius
                                        color: styleMouse.containsMouse ? theme.hoverFill : theme.well
                                        border.width: studio.style === index || finishChoice.activeFocus ? 2 : 1
                                        border.color: finishChoice.activeFocus ? theme.focusBorder : studio.style === index ? theme.selectedText : styleMouse.containsMouse ? theme.hoverBorder : theme.controlBorder
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 5
                                            source: studio.hasImage && studio.revision > 0 ? "image://frames/style" + index + "?" + studio.revision : ""
                                            fillMode: Image.PreserveAspectFit
                                            cache: false
                                            asynchronous: true
                                            retainWhileLoading: true
                                        }
                                        Rectangle {
                                            visible: studio.style === index
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 4
                                            width: 17
                                            height: 17
                                            radius: theme.radius
                                            color: theme.accent
                                            Glyph {
                                                anchors.centerIn: parent
                                                name: "check"
                                                width: 13
                                                height: 13
                                                ink: theme.onAccent
                                            }
                                        }
                                    }
                                    Text {
                                        y: 74
                                        text: studio.styles[index]
                                        color: studio.style === index ? theme.selectedText : theme.muted
                                        font.pixelSize: 11
                                        font.weight: studio.style === index ? Font.DemiBold : Font.Normal
                                    }
                                    MouseArea {
                                        id: styleMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !root.working
                                        onClicked: studio.style = index
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: studio.styles[index] + " finish"
                                    Accessible.onPressAction: studio.style = index
                                    activeFocusOnTab: true
                                    Keys.onReturnPressed: { studio.style = index; root.contentItem.forceActiveFocus(); }
                                    Keys.onSpacePressed: { studio.style = index; root.contentItem.forceActiveFocus(); }
                                }
                            }
                        }
                        StudioButton {
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            Layout.fillWidth: true
                            text: "Raw · no border"
                            glyph: "image"
                            selected: studio.style === 8
                            quiet: studio.style !== 8
                            implicitHeight: 34
                            hint: "Your image and marks exactly as they are"
                            onClicked: studio.style = 8
                        }
                        Rectangle {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            height: 1
                            color: theme.separator
                        }
                        ColumnLayout {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            spacing: 10
                            enabled: studio.style !== 8 && !studio.busy
                            opacity: enabled ? 1 : 0.4
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Padding"; font.pixelSize: 12; color: theme.text; Layout.fillWidth: true }
                                Text { text: Math.round(studio.padding * 100) + "%"; font.pixelSize: 11; color: theme.selectedText }
                            }
                            ThemedSlider {
                                Layout.fillWidth: true
                                from: 0.02
                                to: 0.18
                                stepSize: 0.01
                                value: studio.padding
                                Accessible.name: "Padding"
                                onCommitted: v => studio.padding = v
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Canvas"; font.pixelSize: 12; color: theme.text; Layout.fillWidth: true }
                                Choice {
                                    id: aspectChoice
                                    model: ["Auto", "Square", "16:9", "4:3", "9:16"]
                                    currentIndex: studio.aspect
                                    implicitWidth: 122
                                    implicitHeight: 33
                                    font.pixelSize: 12
                                    onActivated: studio.aspect = currentIndex
                                }
                            }
                        }
                        Item { Layout.preferredHeight: 8 }
                    }
                }
            }
        }
        Loader {
            id: videoPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.videoMode
            visible: active
            source: "VideoPane.qml"
            property real clipStart: item ? item.clipStart : 0
            property real clipEnd: item ? item.clipEnd : 0
            property bool muted: item ? item.muted : false
            property var cuts: item ? item.cuts : []
            property string signature: item ? item.signature : ""
            function pause() { if (item) item.pause() }
            onLoaded: {
                item.shortcutsAllowed = Qt.binding(function() { return root.shortcutsAllowed });
                item.savedCurrent = Qt.binding(function() { return root.videoSavedCurrent });
            }
        }
        // Footer
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            visible: root.videoMode || studio.hasImage
            color: theme.alpha(theme.background, 1)
            Rectangle {
                width: parent.width
                height: 1
                color: theme.separator
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 20
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        id: statusLine
                        text: root.currentStatus
                        color: root.currentSaved.length ? theme.selectedText : theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        ToolTip.visible: statusMouse.containsMouse && statusLine.truncated
                        ToolTip.text: root.currentStatus
                        ToolTip.delay: 400
                        MouseArea {
                            id: statusMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }
                    RowLayout {
                        spacing: 6
                        Glyph {
                            name: "folder"
                            width: 12
                            height: 12
                            ink: theme.muted
                        }
                        Text {
                            text: root.home(root.currentDirectory)
                            color: theme.muted
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: 320
                        }
                        Text {
                            text: "Change"
                            color: theme.selectedText
                            font.pixelSize: 10
                            Accessible.role: Accessible.Button
                            Accessible.name: root.videoMode ? "Change the recordings folder" : "Change the screenshots folder"
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.videoMode ? videoFolderDialog.open() : imageFolderDialog.open()
                            }
                        }
                    }
                }
                StudioButton {
                    visible: root.videoLoaded && root.recordingReview && !root.videoSavedCurrent
                    text: root.narrow ? "" : "Show recording"
                    glyph: "folder"
                    quiet: true
                    hint: "Open the recordings folder"
                    onClicked: video.revealSource()
                }
                StudioButton {
                    visible: root.videoSavedCurrent && !root.recordingReview
                    text: "Copy file"
                    glyph: "copy"
                    quiet: true
                    enabled: !video.busy
                    hint: "Put the video on the clipboard to paste into a chat or folder"
                    onClicked: video.copyFile()
                }
                StudioButton {
                    visible: root.currentSaved.length > 0 && (!root.videoMode || root.videoSavedCurrent)
                    text: root.narrow ? "" : "Show file"
                    glyph: "folder"
                    quiet: true
                    hint: "Open the folder with the saved file"
                    onClicked: root.videoMode ? video.revealSaved() : studio.revealSaved()
                }
                StudioButton {
                    readonly property string label: root.working ? "Working…"
                        : root.videoMode ? (root.recordingReview ? (root.videoUnchanged || root.videoSavedCurrent ? "Copy and close" : "Save and copy") : root.videoSavedCurrent ? "Saved" : "Export video")
                        : studio.recoveryAction.length ? studio.recoveryAction : "Copy and save"
                    text: label
                    hint: root.videoMode ? (root.recordingReview && (root.videoUnchanged || root.videoSavedCurrent) ? "The video is saved in " + root.home(video.outputDirectory) + ". Copy it to the clipboard and close · Ctrl+S" : root.recordingReview ? "Save a new MP4 with your changes, copy it and close · Ctrl+S" : "Save a new MP4 with your changes · Ctrl+S") : "Copy to the clipboard and save a PNG · Ctrl+C"
                    glyph: root.videoMode && !root.recordingReview ? "check" : "copy"
                    primary: true
                    implicitHeight: 44
                    implicitWidth: Math.max(170, implicitContentWidth + 26)
                    enabled: !root.working && (root.videoMode ? root.videoLoaded && !(root.videoSavedCurrent && !root.recordingReview) && (root.recordingReview || !root.videoUnchanged) : !studio.rendering)
                    onClicked: root.acceptCurrent()
                }
            }
        }
    }
    DropArea {
        anchors.fill: parent
        onDropped: function (drop) {
            if (!root.working && drop.hasUrls && drop.urls.length)
                studio.open(drop.urls[0]);
        }
    }
}
