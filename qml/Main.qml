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
    minimumHeight: 520
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
    property string gifSignature: ""
    property bool closingApproved: false
    readonly property bool videoLoaded: videoMode && video.source.toString().length > 0
    readonly property bool videoUnchanged: videoLoaded && videoPane.clipStart <= 0.001 && Math.abs(videoPane.clipEnd - video.duration) <= 0.001 && !videoPane.muted && videoPane.cuts.length === 0 && video.marks.annotations.length === 0 && !video.marks.hasCrop && !(video.cameraSource.toString().length && video.cameraLayout.visible)
    readonly property bool videoSavedCurrent: videoLoaded && video.savedName.length > 0 && savedSignature === videoPane.signature
    readonly property bool typing: markCanvas.typing || videoPane.typing || colorInput.activeFocus || boxColorInput.activeFocus || fontField.inputFocus
    property bool shortcutsAllowed: !gifMenu.opened && !videoPane.popupOpen && !leaveDialog.opened && !openDialog.visible && !imageFolderDialog.visible && !videoFolderDialog.visible && !originalsDialog.opened && !draftDeleteDialog.opened && !captureMenu.opened && !settingsPopup.opened && !aspectChoice.popup.visible && !typing
    property bool working: navigation.saving || studio.busy || video.busy || (recorder.active && !studio.quickMode)
    property string currentStatus: videoMode ? video.status : studio.status
    property string currentDirectory: videoMode ? video.outputDirectory : studio.outputDirectory
    property string currentSaved: videoMode ? video.savedPath : studio.savedPath
    readonly property bool narrow: width < 1100

    function resumeRecent(draft) {
        if (draft.kind === "video") root.requestNavigation("video-draft", draft.id);
        else root.requestNavigation("image-draft", draft.id);
    }
    function home(path) { return path.replace(/^\/home\/[^/]+/, "~") }
    function requestNavigation(command, file) {
        if (studio.busy || video.busy || navigation.saving) return;
        if (videoLoaded) {
            videoPane.commitText();
            videoPane.pause();
            videoPane.syncDraft();
            video.saveDraftNow();
        }
        navigation.request(command, file || "");
    }
    Binding { target: navigation; property: "dirty"; value: root.videoLoaded && !root.videoUnchanged && !root.videoSavedCurrent && video.draftSignature !== videoPane.signature }
    function acceptCurrent() {
        if (markCanvas.typing)
            markCanvas.commitText();
        root.contentItem.forceActiveFocus();
        if (videoMode) {
            videoPane.commitText();
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
        if (markCanvas.typing) markCanvas.commitText();
        else if (markCanvas.dragging) markCanvas.cancelDrag();
        else if (studio.marks.selectedAnnotation.type !== undefined) studio.marks.clearSelection();
        else if (root.tool !== "select") root.tool = "select";
        else if (studio.quickMode) studio.showFinishes();
        else root.editing = false;
    }
    function showFinish() {
        if (markCanvas.typing) markCanvas.commitText();
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
        if (!visible || closingApproved) return;
        if (markCanvas.typing) markCanvas.commitText();
        if (root.working || recorder.active) {
            close.accepted = false;
            return;
        }
        if (studio.quickMode) { close.accepted = false; studio.dismissQuick(); }
        else { close.accepted = false; root.requestNavigation("quit"); }
    }
    Binding { target: studio; property: "editing"; value: root.editing && !root.videoMode && root.visible }
    onEditingChanged: if (!editing && markCanvas.typing) markCanvas.commitText()
    onToolChanged: if (markCanvas.typing) markCanvas.commitText()
    Connections {
        target: studio
        function onEditorRequested() { root.editing = true; root.videoMode = false; root.tool = "select"; }
        function onSourceChanged() {
            markCanvas.cancelText();
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
        }
        function onLoaded() { root.savedSignature = video.savedSignature; root.gifSignature = ""; }
        function onGifExported() { root.gifSignature = videoPane.signature; }
        function onExported() {
            root.savedSignature = videoPane.signature;
            video.recordSavedSignature(videoPane.signature);
            navigation.saveSucceeded();
        }
        function onExportFailed() {
            if (navigation.saving) {
                leaveDialog.saveError = video.status + " Your edits are still here.";
                navigation.saveFailed();
            }
        }
    }
    Connections {
        target: navigation
        function onConfirmationRequested() { leaveDialog.open(); }
        function onChanged() { if (!navigation.pending) leaveDialog.close(); }
        function onSaveRequested() {
            leaveDialog.close();
            video.exportEdited(videoPane.clipStart, videoPane.clipEnd, videoPane.muted, videoPane.cuts);
        }
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
        onActivated: studio.marks.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode
        onActivated: studio.marks.redo()
    }
    Shortcut {
        sequence: "Escape"
        enabled: (root.shortcutsAllowed || markCanvas.typing) && !root.videoMode && studio.hasImage && (root.editing || studio.quickMode)
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
        // H is the highlight tool here.
        sequence: "Shift+H"
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode && studio.secretCount > 0
        onActivated: studio.hideSecrets()
    }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode && studio.marks.selectedAnnotation.type !== undefined
        onActivated: studio.marks.deleteSelected()
    }
    Shortcut {
        sequence: "Ctrl+D"
        enabled: root.shortcutsAllowed && root.editing && !root.videoMode && studio.marks.selectedAnnotation.type !== undefined
        onActivated: studio.marks.duplicateSelected()
    }
    Shortcut {
        sequences: ["F2"]
        enabled: root.shortcutsAllowed && root.editing && studio.marks.selectedAnnotation.type === "text"
        onActivated: markCanvas.editSelectedText()
    }
    readonly property bool nudging: root.shortcutsAllowed && root.editing && !root.videoMode && studio.marks.selectedAnnotation.type !== undefined
    Shortcut { sequence: "Left"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(-1, 0) }
    Shortcut { sequence: "Right"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(1, 0) }
    Shortcut { sequence: "Up"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(0, -1) }
    Shortcut { sequence: "Down"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(0, 1) }
    Shortcut { sequence: "Shift+Left"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(-10, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(10, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: root.nudging; onActivated: studio.marks.nudgeSelected(0, 10) }

    FileDialog {
        id: openDialog
        title: "Open an image or recording"
        nameFilters: ["Images and recordings (*.png *.jpg *.jpeg *.webp *.bmp *.avif *.heic *.mp4 *.webm *.mkv *.mov *.m4v)", "Images (*.png *.jpg *.jpeg *.webp *.bmp)", "Recordings (*.mp4 *.webm *.mkv *.mov *.m4v)"]
        onAccepted: root.requestNavigation("open", selectedFile)
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
        property bool videoDraft: false
        title: "Delete this draft?"
        message: videoDraft ? "Only these saved edits are removed. Your original video and exports stay where they are."
                            : "Its private source image and marks are removed. Files you already saved stay where they are."
        confirmText: "Delete draft"
        onConfirmed: videoDraft ? video.deleteDraft(draftId) : studio.deleteDraft(draftId)
    }
    Popup {
        id: leaveDialog
        property string saveError: ""
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, root.width - 48)
        padding: 22
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: leaveCancel.forceActiveFocus()
        onClosed: {
            if (!navigation.saving) navigation.cancel();
            saveError = "";
        }
        Overlay.modal: Rectangle { color: theme.scrim }
        background: Rectangle {
            color: theme.alpha(theme.background, 1)
            radius: theme.radius
            border.width: 2
            border.color: theme.frame
        }
        contentItem: ColumnLayout {
            spacing: 14
            Text { Layout.fillWidth: true; text: "Save your video edits?"; color: theme.text; font.pixelSize: 16; font.weight: Font.Medium; wrapMode: Text.Wrap }
            Text {
                Layout.fillWidth: true
                text: "Your changes to " + video.name + " haven't been saved. Save them before continuing, or discard the edits. Your original video stays unchanged."
                color: theme.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
                lineHeight: 1.25
            }
            Text { Layout.fillWidth: true; visible: text.length > 0; text: leaveDialog.saveError; color: theme.urgent; font.pixelSize: 12; wrapMode: Text.Wrap }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                StudioButton { id: leaveCancel; text: "Cancel"; quiet: true; onClicked: navigation.cancel() }
                StudioButton { text: "Discard edits"; danger: true; onClicked: navigation.discard() }
                StudioButton { text: "Save and continue"; primary: true; onClicked: navigation.save() }
            }
        }
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
        implicitHeight: Math.max(detail.length ? 52 : 40, implicitContentHeight + topPadding + bottomPadding)
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
                    Layout.fillWidth: true
                    text: action.text
                    color: action.ink
                    font.family: theme.fontFamily
                    font.pixelSize: 13
                    font.weight: action.primary ? Font.DemiBold : Font.Normal
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: action.detail
                    color: action.primary ? theme.alpha(theme.onAccent, 0.8) : theme.muted
                    font.family: theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
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
        focus: true
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
                onClicked: { captureMenu.close(); root.requestNavigation("capture"); }
            }
            MenuAction {
                text: "Record video"
                detail: "Choose an area, window or display to record"
                glyph: "record"
                enabled: !recorder.active
                onClicked: { captureMenu.close(); root.requestNavigation("record"); }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
            MenuAction {
                text: "Repeat last area"
                glyph: "capture"
                quiet: true
                enabled: studio.hasLastArea
                hint: studio.hasLastArea ? "Capture the same part of the screen again" : "Capture an area first"
                onClicked: { captureMenu.close(); root.requestNavigation("repeat"); }
            }
            MenuAction {
                text: "Whole active display"
                glyph: "display"
                quiet: true
                onClicked: { captureMenu.close(); root.requestNavigation("screen"); }
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
        focus: true
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
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: "Show a notification with the save folder"
                    checked: notificationSetting.enabled
                    onToggled: notificationSetting.enabled = checked
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                SectionLabel { text: "PRIVACY" }
                RecordToggle {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
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
                    text: "Omaframe " + Qt.application.version + " · Everything stays on this computer. No accounts, uploads or telemetry."
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
                        onActivated: root.requestNavigation("capture")
                    }
                    ActionCard {
                        glyph: "record"
                        title: "Record video"
                        detail: "Same selection, with sound and a Stop button kept out of the video."
                        key: shortcuts.recordKey
                        enabled: !root.working && !recorder.active
                        onActivated: root.requestNavigation("record")
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
                                "Choose Screenshot above, then click a window, drag an area, or press F for the display. Tab switches to video.",
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
                    visible: studio.drafts.length + video.drafts.length > 0
                    Layout.fillWidth: true
                    spacing: 8
                    SectionLabel { text: "RECENT EDITS" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: startColumn.width > 640 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 8
                        Repeater {
                            model: studio.drafts.concat(video.drafts)
                            delegate: Rectangle {
                                id: draftCard
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 64
                                radius: theme.radius
                                color: draftMouse.containsMouse ? theme.hoverFill : theme.controlFill
                                border.width: activeFocus ? 2 : 1
                                border.color: activeFocus ? theme.focusBorder : theme.controlBorder
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.name + ". Resume edit"
                                Accessible.onPressAction: root.resumeRecent(modelData)
                                Keys.onReturnPressed: root.resumeRecent(modelData)
                                Keys.onSpacePressed: root.resumeRecent(modelData)
                                MouseArea {
                                    id: draftMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.resumeRecent(draftCard.modelData)
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
                                            source: draftCard.modelData.image || ""
                                            Glyph { anchors.centerIn: parent; visible: draftCard.modelData.kind === "video"; name: "record"; width: 24; height: 24; ink: theme.muted }
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
                                            text: (draftCard.modelData.kind === "video" ? "Video · " : "") + draftCard.modelData.when + " · " + draftCard.modelData.edits + (draftCard.modelData.edits === 1 ? " mark" : " marks") + (draftCard.modelData.exported ? " · saved" : "")
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
                                            draftDeleteDialog.videoDraft = draftCard.modelData.kind === "video";
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
                        text: "Image drafts keep a private original. Video drafts keep edits and refer to the original video; keep that file in place. Delete drafts when you are done."
                        color: theme.faint
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
                StudioButton {
                    text: "Try the editor on a sample image"
                    glyph: "spark"
                    quiet: true
                    enabled: !root.working
                    onClicked: { studio.loadDemo(0); root.editing = true; root.tool = "select"; }
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
                    ColumnLayout {
                        Layout.leftMargin: 6
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: 260
                        spacing: 2
                        Text { Layout.fillWidth: true; text: studio.name; elide: Text.ElideMiddle; color: theme.text; font.pixelSize: 12 }
                        Text { text: studio.dimensions; color: theme.faint; font.pixelSize: 10 }
                    }
                    Rectangle {
                        visible: studio.demo && !root.narrow
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
                        enabled: studio.marks.canUndo && !studio.busy
                        hint: "Undo · Ctrl+Z"
                        onClicked: studio.marks.undo()
                    }
                    StudioButton {
                        visible: root.editing
                        glyph: "redo"
                        text: root.narrow ? "" : "Redo"
                        quiet: true
                        implicitHeight: 34
                        enabled: studio.marks.canRedo && !studio.busy
                        hint: "Redo · Ctrl+Shift+Z"
                        onClicked: studio.marks.redo()
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
                        onClicked: { markCanvas.cancelText(); studio.closeImage(); }
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
                        MarkCanvas {
                            id: markCanvas
                            anchors.centerIn: preview
                            width: preview.paintedWidth
                            height: preview.paintedHeight
                            visible: root.editing
                            doc: studio.marks
                            tool: root.tool
                            locked: studio.busy
                            workingSize: studio.workingSize
                            sourceSize: studio.sourceSize
                            onToolRequested: key => root.tool = key
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.editing ? (markCanvas.typing ? "Typing a label. Enter adds a line; Esc or a click outside finishes it." : root.toolDescription) : studio.rendering ? "Refining the preview…" : "Output: " + studio.outputDimensions + " · PNG · full resolution"
                    font.pixelSize: 11
                    color: theme.faint
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
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
                Layout.minimumWidth: Layout.preferredWidth
                Layout.maximumWidth: Layout.preferredWidth
                color: theme.alpha(theme.background, 1)
                // Edit sidebar: tools stay in one place; the selected mark's
                // settings appear below them.
                ScrollView {
                    id: editSidebar
                    visible: root.editing
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: editSidebar.availableWidth
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
                                    padding: root.narrow ? 9 : 11
                                    text: modelData.label
                                    glyph: modelData.key
                                    selected: root.tool === modelData.key
                                    quiet: !selected
                                    hint: modelData.label + " · " + modelData.shortcut
                                    onClicked: root.tool = modelData.key
                                    contentItem: RowLayout {
                                        spacing: 8
                                        Glyph { name: toolButton.glyph; ink: toolButton.ink; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                                        Text { text: toolButton.text; color: toolButton.ink; font.family: theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Text { visible: !root.narrow; text: toolButton.modelData.shortcut; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }
                        StudioButton {
                            visible: root.tool === "crop" && studio.marks.hasCrop
                            Layout.leftMargin: 14
                            Layout.rightMargin: 14
                            Layout.fillWidth: true
                            text: "Clear crop"
                            quiet: true
                            onClicked: studio.marks.clearCrop()
                        }
                        StudioButton {
                            visible: studio.secretCount > 0
                            Layout.leftMargin: 14
                            Layout.rightMargin: 14
                            Layout.fillWidth: true
                            text: "Hide " + studio.secretCount + (studio.secretCount === 1 ? " possible secret" : " possible secrets")
                            glyph: "redact"
                            hint: "Redact what looks like keys, tokens, emails and card numbers · Shift+H"
                            enabled: !studio.busy
                            onClicked: studio.hideSecrets()
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
                            readonly property var mark: studio.marks.selectedAnnotation
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
                                onClicked: markCanvas.editSelectedText()
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                StudioButton { text: "Duplicate"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; hint: "Ctrl+D"; enabled: !studio.busy; onClicked: studio.marks.duplicateSelected() }
                                StudioButton { glyph: "trash"; text: "Delete"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; hint: "Delete"; enabled: !studio.busy; onClicked: studio.marks.deleteSelected() }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                StudioButton { text: "Backward"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; hint: "Move this mark one layer backward"; enabled: !studio.busy && selectedInspector.mark.layer > 1; onClicked: studio.marks.moveSelectedLayer(-1) }
                                StudioButton { text: "Forward"; quiet: true; Layout.fillWidth: true; implicitHeight: 32; hint: "Move this mark one layer forward"; enabled: !studio.busy && selectedInspector.mark.layer < selectedInspector.mark.layers; onClicked: studio.marks.moveSelectedLayer(1) }
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
                                    value: studio.marks.selectedAnnotation.fontPx || 24
                                    step: Math.max(1, Math.round(value / 12))
                                    suffix: "px"
                                    onCommitted: next => studio.marks.setSelectedFontPixels(next)
                                }
                                ThemedSlider {
                                    Layout.fillWidth: true
                                    from: 0; to: 1
                                    value: Math.log2(Math.max(8, studio.marks.selectedAnnotation.fontPx || 24) / 8) / 9
                                    onCommitted: v => studio.marks.setSelectedFontPixels(Math.round(8 * Math.pow(512, v)))
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
                            Flow {
                                visible: selectedInspector.colored
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: ["#ffffff", "#151a20", "#e75439", "#eab841", "#459ec7", "#4ca782"]
                                    Rectangle {
                                        required property string modelData
                                        width: 24; height: 24; radius: theme.radius > 0 ? 12 : 2
                                        color: modelData
                                        border.width: studio.marks.selectedAnnotation.color === modelData ? 3 : 1
                                        border.color: studio.marks.selectedAnnotation.color === modelData ? theme.focusBorder : theme.controlBorder
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Color " + modelData
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !studio.busy
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: studio.marks.setSelectedColor(parent.modelData)
                                        }
                                    }
                                }
                                TextField {
                                    id: colorInput
                                    width: 78
                                    height: 28
                                    property bool validColor: /^#[0-9a-fA-F]{6}$/.test(text)
                                    text: selectedInspector.mark.color || ""
                                    placeholderText: "#RRGGBB"
                                    color: theme.text
                                    font.pixelSize: 11
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (validColor)
                                            studio.marks.setSelectedColor(text)
                                        else
                                            text = studio.marks.selectedAnnotation.color || ""
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
                                value: studio.marks.selectedAnnotation.size || 1
                                onCommitted: v => studio.marks.setSelectedSize(v)
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
                                StudioButton { text: "Box"; selected: studio.marks.selectedAnnotation.textStyle === "box"; quiet: !selected; Layout.fillWidth: true; implicitHeight: 32; onClicked: studio.marks.setSelectedTextStyle("box") }
                                StudioButton { text: "Shadow"; selected: studio.marks.selectedAnnotation.textStyle === "shadow"; quiet: !selected; Layout.fillWidth: true; implicitHeight: 32; onClicked: studio.marks.setSelectedTextStyle("shadow") }
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
                                        selected: studio.marks.selectedAnnotation.textAlign === modelData
                                        quiet: !selected
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        font.pixelSize: 11
                                        hint: "Align lines " + modelData
                                        onClicked: studio.marks.setSelectedTextAlignment(modelData)
                                    }
                                }
                            }
                            Text {
                                visible: selectedInspector.mark.type === "text" && selectedInspector.mark.textStyle === "box"
                                text: "Box color · " + Math.round((selectedInspector.mark.backgroundOpacity || 0) * 100) + "%"
                                color: theme.text
                                font.pixelSize: 12
                            }
                            Flow {
                                visible: selectedInspector.mark.type === "text" && selectedInspector.mark.textStyle === "box"
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: ["#151a20", "#ffffff", "#e75439", "#eab841", "#459ec7", "#4ca782"]
                                    Rectangle {
                                        required property string modelData
                                        width: 24; height: 24; radius: theme.radius > 0 ? 12 : 2
                                        color: modelData
                                        border.width: studio.marks.selectedAnnotation.background === modelData ? 3 : 1
                                        border.color: studio.marks.selectedAnnotation.background === modelData ? theme.focusBorder : theme.controlBorder
                                        Accessible.role: Accessible.Button
                                        Accessible.name: "Box color " + modelData
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !studio.busy
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: studio.marks.setSelectedBackground(parent.modelData)
                                        }
                                    }
                                }
                                TextField {
                                    id: boxColorInput
                                    width: 78
                                    height: 28
                                    property bool validColor: /^#[0-9a-fA-F]{6}$/.test(text)
                                    text: selectedInspector.mark.background || ""
                                    placeholderText: "#RRGGBB"
                                    color: theme.text
                                    font.pixelSize: 11
                                    selectByMouse: true
                                    onEditingFinished: {
                                        if (validColor)
                                            studio.marks.setSelectedBackground(text)
                                        else
                                            text = studio.marks.selectedAnnotation.background || ""
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
                                value: studio.marks.selectedAnnotation.backgroundOpacity || 0
                                Accessible.name: "Box opacity"
                                onCommitted: v => studio.marks.setSelectedBackgroundOpacity(v)
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
                    id: finishSidebar
                    visible: !root.editing
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: finishSidebar.availableWidth
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
            property real outputDuration: item ? item.outputDuration : 0
            property bool muted: item ? item.muted : false
            property var cuts: item ? item.cuts : []
            property string signature: item ? item.signature : ""
            property bool typing: item ? item.typing : false
            property bool popupOpen: item ? item.popupOpen : false
            function pause() { if (item) item.pause() }
            function commitText() { if (item) item.commitText() }
            function syncDraft() { if (item) item.syncDraft() }
            onLoaded: {
                item.shortcutsAllowed = Qt.binding(function() { return root.shortcutsAllowed });
                item.savedCurrent = Qt.binding(function() { return root.videoSavedCurrent });
            }
        }
        // Footer
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.videoMode && root.height < 600 ? 64 : 74
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
                        StudioButton {
                            text: "Change"
                            quiet: true
                            implicitHeight: 24
                            padding: 6
                            font.pixelSize: 10
                            Accessible.name: root.videoMode ? "Change the recordings folder" : "Change the screenshots folder"
                            onClicked: root.videoMode ? videoFolderDialog.open() : imageFolderDialog.open()
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
                    id: gifButton
                    visible: root.videoLoaded
                    text: "Export GIF"
                    quiet: true
                    enabled: !root.working
                    hint: "Save a looping GIF of this clip, with your edits and no sound"
                    onClicked: gifMenu.opened ? gifMenu.close() : gifMenu.open()
                    Popup {
                        id: gifMenu
                        x: parent.width - width
                        y: -height - 8
                        width: 280; padding: 16; modal: true; focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        readonly property bool current: video.gifPath.length > 0 && root.gifSignature === videoPane.signature
                        background: Rectangle { color: theme.alpha(theme.background, 1); radius: theme.radius; border.width: 2; border.color: theme.frame }
                        contentItem: ColumnLayout {
                            spacing: 12
                            Text { text: "Export GIF"; color: theme.text; font.pixelSize: 15; font.weight: Font.Medium }
                            Text {
                                Layout.fillWidth: true
                                text: "Loops without sound. Includes your edits. Up to 720 px at 15 fps."
                                color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: videoPane.outputDuration > 30.000001 ? "Trim or cut this clip to 30 seconds or less."
                                    : "Clip length: " + videoPane.outputDuration.toFixed(1) + " seconds. GIFs can be larger than MP4."
                                color: videoPane.outputDuration > 30.000001 ? theme.urgent : theme.muted
                                font.pixelSize: 12; wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true; visible: gifMenu.current
                                text: "Saved " + video.gifPath.split("/").pop() + "\n" + video.gifSummary
                                color: theme.selectedText; font.pixelSize: 12; wrapMode: Text.Wrap
                            }
                            RowLayout {
                                visible: gifMenu.current
                                StudioButton { text: "Show file"; glyph: "folder"; quiet: true; enabled: !root.working; onClicked: video.revealGif() }
                            }
                            StudioButton {
                                Layout.fillWidth: true
                                text: root.working ? "Exporting…" : gifMenu.current ? "Copy GIF" : "Export GIF"
                                primary: true
                                enabled: !root.working && videoPane.outputDuration >= 0.1 && videoPane.outputDuration <= 30.000001
                                onClicked: {
                                    if (gifMenu.current) {
                                        if (video.copyGif()) gifMenu.close();
                                        return;
                                    }
                                    videoPane.commitText();
                                    videoPane.pause();
                                    video.exportGif(videoPane.clipStart, videoPane.clipEnd, videoPane.cuts);
                                }
                            }
                        }
                    }
                }
                StudioButton {
                    readonly property string label: root.working ? "Working…"
                        : root.videoMode ? (root.recordingReview ? (root.videoUnchanged || root.videoSavedCurrent ? "Copy and close" : "Save and copy") : root.videoSavedCurrent ? "Saved" : root.videoUnchanged ? "No edits yet" : "Export video")
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
                root.requestNavigation("open", drop.urls[0]);
        }
    }
}
