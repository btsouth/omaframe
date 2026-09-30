import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtMultimedia

// The clip editor: one stage, one transport row, and one timeline where the
// filmstrip, trim handles, removed parts and playhead live together.
// Clicking the filmstrip moves the playhead. Dragging across it selects a
// part, which can then be removed. While paused, G and R draw a blur or a
// redaction over the video, and A, B, T and N an arrow, a box, a label or a
// numbered step. Nothing here changes the source file.
Item {
    id: pane
    property bool shortcutsAllowed: true
    property bool savedCurrent: false
    property bool muted: false
    readonly property bool compact: height < 440
    property real clipStart: 0
    property real clipEnd: 0
    property var cuts: []
    property var undoStack: []
    property var redoStack: []
    property bool hasSelection: false
    property real selStart: 0
    property real selEnd: 0
    property int selectedCut: -1
    property string notice: ""
    // Taken when a trim handle is grabbed; pushed on the first real change.
    property var pendingUndo: null
    readonly property bool loaded: video.source.toString().length > 0
    readonly property bool editable: loaded && !video.busy
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property string signature: JSON.stringify([clipStart.toFixed(3), clipEnd.toFixed(3), muted, cuts.map(c => [c.start.toFixed(3), c.end.toFixed(3)]), video.marks.annotations, video.marks.cropBounds, video.cameraLayout])
    // The mark tool: select, blur, redact, arrow, box, text or step.
    property string tool: "select"
    readonly property bool typing: markCanvas.typing || startField.activeFocus || endField.activeFocus
        || markStartField.activeFocus || markEndField.activeFocus || partStartField.activeFocus || partEndField.activeFocus
    readonly property var selectedMark: video.marks.selectedAnnotation
    readonly property bool markSelected: selectedMark.type !== undefined
    readonly property bool hasMarks: video.marks.annotations.length > 0
    // What the bar above the timeline describes.
    readonly property string barMode: markSelected ? "mark" : tool !== "select" ? "tool" : hasSelection ? "part" : selectedCut >= 0 ? "cut" : "none"
    // Set while undo or redo replays a mark change, so it is not recorded again.
    property bool replaying: false
    readonly property real outputDuration: {
        let remaining = clipEnd - clipStart;
        for (const cut of cuts)
            remaining -= Math.max(0, Math.min(clipEnd, cut.end) - Math.max(clipStart, cut.start));
        return Math.max(0, remaining);
    }
    // The playhead in seconds, advanced every frame between the player's
    // coarser position updates so it glides instead of stepping.
    property real head: 0
    property real anchorTime: 0
    property real anchorClock: 0

    function time(seconds) {
        let n = Math.round(Math.max(0, seconds) * 10) / 10;
        return Math.floor(n / 60).toString().padStart(2, "0") + ":" + (n % 60).toFixed(1).padStart(4, "0");
    }
    function pause() {
        player.pause();
    }
    function seek(seconds) {
        let target = Math.max(0, Math.min(video.duration, seconds));
        for (const cut of cuts) {
            if (target >= cut.start && target < cut.end) {
                target = cut.end;
                break;
            }
        }
        player.position = Math.min(video.duration, target) * 1000;
    }
    function togglePlay() {
        if (!editable)
            return;
        if (playing)
            player.pause();
        else {
            if (player.position < clipStart * 1000 || player.position >= clipEnd * 1000)
                player.position = clipStart * 1000;
            player.play();
        }
    }
    // Every change to the edit (trim, removed parts, sound) can be undone.
    function snapshot() {
        return { clipStart: clipStart, clipEnd: clipEnd, muted: muted, cuts: cuts.slice(), cameraLayout: Object.assign({}, video.cameraLayout) };
    }
    function syncDraft() {
        if (editable && clipEnd - clipStart >= 0.1)
            video.setEditState(Object.assign(snapshot(), { signature: signature }));
    }
    onSignatureChanged: syncDraft()
    function loadEditState() {
        const state = video.editState;
        clipStart = state.clipStart || 0;
        clipEnd = state.clipEnd || video.duration;
        muted = state.muted || false;
        cuts = state.cuts || [];
        player.pause();
        player.position = clipStart * 1000;
        head = clipStart;
        undoStack = [];
        redoStack = [];
        tool = "select";
        clearSelection();
        syncDraft();
    }
    Component.onCompleted: if (loaded) loadEditState()
    function changeCamera(layout) {
        if (Object.keys(layout).every(key => video.cameraLayout[key] === layout[key]))
            return;
        pushUndo();
        video.setCameraLayout(Object.assign({}, video.cameraLayout, layout));
    }
    function pushUndo() {
        undoStack = undoStack.concat([snapshot()]);
        redoStack = [];
    }
    function restore(state) {
        clipStart = state.clipStart;
        clipEnd = state.clipEnd;
        muted = state.muted;
        cuts = state.cuts;
        if (state.cameraLayout) video.setCameraLayout(state.cameraLayout);
        clearSelection();
    }
    // Marks keep their own history. Their place in the order is recorded
    // here as a "marks" entry, so one Ctrl+Z steps back through both.
    function undo() {
        if (undoStack.length === 0)
            return;
        const last = undoStack[undoStack.length - 1];
        if (last === "marks") {
            redoStack = redoStack.concat(["marks"]);
            replaying = true;
            video.marks.undo();
            replaying = false;
        } else {
            redoStack = redoStack.concat([snapshot()]);
            restore(last);
        }
        undoStack = undoStack.slice(0, -1);
    }
    function redo() {
        if (redoStack.length === 0)
            return;
        const next = redoStack[redoStack.length - 1];
        if (next === "marks") {
            undoStack = undoStack.concat(["marks"]);
            replaying = true;
            video.marks.redo();
            replaying = false;
        } else {
            undoStack = undoStack.concat([snapshot()]);
            restore(next);
        }
        redoStack = redoStack.slice(0, -1);
    }
    function commitText() {
        markCanvas.commitText();
    }
    // Whether a mark is on at the playhead. One that runs to the end of the
    // clip is still on at its last frame.
    function shows(start, end) {
        return head >= start && (head < end || end >= video.duration);
    }
    function markName(type) {
        return ({ redact: "Redaction", blur: "Blur", arrow: "Arrow", box: "Box", text: "Label", step: "Step" })[type] || "Mark";
    }
    function useTool(key) {
        if (!editable)
            return;
        player.pause();
        clearSelection();
        tool = key;
    }
    // Esc steps back one layer: a drag, the selected mark, the mark tool,
    // then the selected part of the timeline.
    function stepBack() {
        if (markCanvas.dragging) markCanvas.cancelDrag();
        else if (markSelected) video.marks.clearSelection();
        else if (tool !== "select") tool = "select";
        else clearSelection();
    }
    function deleteSelected() {
        if (markSelected) video.marks.deleteSelected();
        else removeSelection();
    }
    // With a mark selected, I and O set when it shows instead of trimming.
    function setIn() {
        if (markSelected) video.marks.setSelectedTimes(head, selectedMark.end);
        else setStart(player.position / 1000, true);
    }
    function setOut() {
        if (markSelected) video.marks.setSelectedTimes(selectedMark.start, head);
        else setEnd(player.position / 1000, true);
    }
    function selectMark(index, start, end) {
        player.pause();
        if (!shows(start, end))
            seek(start);
        video.marks.select(index);
    }
    function takePendingUndo() {
        if (!pendingUndo)
            return;
        undoStack = undoStack.concat([pendingUndo]);
        redoStack = [];
        pendingUndo = null;
    }
    function setStart(seconds, record) {
        const next = Math.max(0, Math.min(clipEnd - 0.1, seconds));
        if (Math.abs(next - clipStart) < 0.0005)
            return;
        if (record)
            pushUndo();
        else
            takePendingUndo();
        player.pause();
        clipStart = next;
        seek(clipStart);
    }
    function setEnd(seconds, record) {
        const next = Math.min(video.duration, Math.max(clipStart + 0.1, seconds));
        if (Math.abs(next - clipEnd) < 0.0005)
            return;
        if (record)
            pushUndo();
        else
            takePendingUndo();
        player.pause();
        clipEnd = next;
        seek(Math.max(clipStart, clipEnd - 0.1));
    }
    function toggleSound() {
        pushUndo();
        muted = !muted;
    }
    function nudge(seconds) {
        player.pause();
        seek(player.position / 1000 + seconds);
    }
    function clearSelection() {
        hasSelection = false;
        selectedCut = -1;
        notice = "";
    }
    function select(from, to) {
        video.marks.clearSelection();
        selStart = Math.max(clipStart, Math.min(from, to));
        selEnd = Math.min(clipEnd, Math.max(from, to));
        hasSelection = selEnd - selStart >= 0.1;
        selectedCut = -1;
        notice = "";
    }
    function setSelectionStart(seconds) {
        select(Math.min(seconds, selEnd - 0.1), selEnd);
    }
    function setSelectionEnd(seconds) {
        select(selStart, Math.max(seconds, selStart + 0.1));
    }
    // Removed parts are merged when they touch, so the list stays simple.
    function withCut(list, start, end) {
        let merged = { start: start, end: end };
        const kept = [];
        for (const cut of list) {
            if (cut.end < merged.start - 0.0005 || cut.start > merged.end + 0.0005)
                kept.push(cut);
            else
                merged = { start: Math.min(cut.start, merged.start), end: Math.max(cut.end, merged.end) };
        }
        kept.push(merged);
        kept.sort((a, b) => a.start - b.start);
        return kept;
    }
    function keptAfter(list) {
        let remaining = clipEnd - clipStart;
        for (const cut of list)
            remaining -= Math.max(0, Math.min(clipEnd, cut.end) - Math.max(clipStart, cut.start));
        return remaining;
    }
    function removeSelection() {
        if (!editable || !hasSelection)
            return;
        const updated = withCut(cuts, selStart, selEnd);
        if (keptAfter(updated) + 0.000001 < 0.1) {
            notice = "Keep at least 0.1 seconds of the clip.";
            return;
        }
        pushUndo();
        cuts = updated;
        const end = selEnd;
        clearSelection();
        seek(end);
    }
    function selectCut(index) {
        video.marks.clearSelection();
        hasSelection = false;
        selectedCut = index;
        notice = "";
        player.pause();
        seek(Math.max(clipStart, cuts[index].start - 0.05));
    }
    function restoreCut(index) {
        if (index < 0 || index >= cuts.length)
            return;
        pushUndo();
        const updated = cuts.slice();
        updated.splice(index, 1);
        cuts = updated;
        clearSelection();
    }
    function setCutTimes(index, start, end) {
        if (index < 0 || index >= cuts.length)
            return;
        start = Math.max(clipStart, Math.min(start, end - 0.1));
        end = Math.min(clipEnd, Math.max(end, start + 0.1));
        const others = cuts.slice();
        others.splice(index, 1);
        const updated = withCut(others, start, end);
        if (keptAfter(updated) + 0.000001 < 0.1) {
            notice = "Keep at least 0.1 seconds of the clip.";
            return;
        }
        pushUndo();
        cuts = updated;
        for (let i = 0; i < cuts.length; ++i)
            if (cuts[i].start <= start + 0.0005 && cuts[i].end >= end - 0.0005)
                selectedCut = i;
    }
    function cutAt(seconds) {
        for (let i = 0; i < cuts.length; ++i)
            if (seconds >= cuts[i].start && seconds <= cuts[i].end)
                return i;
        return -1;
    }
    function skipRemoved(seconds) {
        for (const cut of cuts) {
            if (seconds >= cut.start && seconds < cut.end) {
                player.position = cut.end * 1000;
                head = cut.end;
                anchorTime = cut.end;
                anchorClock = Date.now();
                return true;
            }
        }
        return false;
    }

    onVisibleChanged: if (!visible)
        player.pause()
    onMarkSelectedChanged: if (markSelected) {
        hasSelection = false;
        selectedCut = -1;
        notice = "";
    }
    // A mark whose outline is hidden lets go of the keys: while playing, or
    // once the playhead leaves its time, I, O, Delete and the arrows are
    // back on the clip.
    onPlayingChanged: if (playing) {
        tool = "select";
        markCanvas.commitText();
        video.marks.clearSelection();
    }
    // A twentieth of a second of slack: a seek can land a frame early.
    onHeadChanged: if (markSelected && (head < selectedMark.start - 0.05 || (head > selectedMark.end + 0.05 && selectedMark.end < video.duration)))
        video.marks.clearSelection()
    Binding { target: video.marks; property: "playhead"; value: pane.head }
    Connections {
        target: video.marks
        function onEdited(modified) {
            if (modified && !pane.replaying) {
                // The marks keep their last 100 steps. Past that, drop the
                // oldest entry here too so every Ctrl+Z still undoes one.
                const stack = pane.undoStack.concat(["marks"]);
                if (stack.filter(entry => entry === "marks").length > 100)
                    stack.splice(stack.indexOf("marks"), 1);
                pane.undoStack = stack;
                pane.redoStack = [];
            }
        }
    }
    MediaPlayer {
        id: player
        source: video.source
        videoOutput: output
        audioOutput: AudioOutput {
            muted: pane.muted
        }
        onPositionChanged: {
            if (playbackState === MediaPlayer.PlayingState && pane.skipRemoved(position / 1000))
                return;
            if (playbackState === MediaPlayer.PlayingState && position >= pane.clipEnd * 1000) {
                pause();
                position = pane.clipStart * 1000;
            }
            pane.anchorTime = position / 1000;
            pane.anchorClock = Date.now();
            if (playbackState !== MediaPlayer.PlayingState)
                pane.head = pane.anchorTime;
        }
        onPlaybackStateChanged: {
            pane.anchorTime = position / 1000;
            pane.anchorClock = Date.now();
            pane.head = pane.anchorTime;
        }
    }
    FrameAnimation {
        running: pane.playing && pane.visible
        onTriggered: {
            const next = Math.min(pane.clipEnd, pane.anchorTime + (Date.now() - pane.anchorClock) / 1000);
            if (!pane.skipRemoved(next))
                pane.head = next;
        }
    }
    Connections {
        target: video
        function onLoaded() { pane.loadEditState(); }
    }

    readonly property bool popupOpen: cameraMenu.opened
    readonly property bool keys: visible && editable && shortcutsAllowed && !typing && !popupOpen
    Shortcut { sequence: "Space"; enabled: pane.keys; onActivated: pane.togglePlay() }
    Shortcut { sequence: "I"; enabled: pane.keys; onActivated: pane.setIn() }
    Shortcut { sequence: "O"; enabled: pane.keys; onActivated: pane.setOut() }
    Shortcut { sequence: "Escape"; enabled: pane.keys && (pane.barMode !== "none" || markCanvas.dragging); onActivated: pane.stepBack() }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: pane.keys && (pane.hasSelection || pane.markSelected); onActivated: pane.deleteSelected() }
    Shortcut { sequence: "C"; enabled: pane.keys; onActivated: pane.useTool("crop") }
    Shortcut { sequence: "G"; enabled: pane.keys; onActivated: pane.useTool("blur") }
    Shortcut { sequence: "R"; enabled: pane.keys; onActivated: pane.useTool("redact") }
    Shortcut { sequence: "A"; enabled: pane.keys; onActivated: pane.useTool("arrow") }
    Shortcut { sequence: "B"; enabled: pane.keys; onActivated: pane.useTool("box") }
    Shortcut { sequence: "T"; enabled: pane.keys; onActivated: pane.useTool("text") }
    Shortcut { sequence: "N"; enabled: pane.keys; onActivated: pane.useTool("step") }
    Shortcut { sequence: "V"; enabled: pane.keys; onActivated: pane.tool = "select" }
    // Arrow keys move a selected mark, like the screenshot editor, and
    // otherwise step through time.
    Shortcut { sequence: "Left"; enabled: pane.keys; onActivated: pane.markSelected ? video.marks.nudgeSelected(-1, 0) : pane.nudge(-0.1) }
    Shortcut { sequence: "Right"; enabled: pane.keys; onActivated: pane.markSelected ? video.marks.nudgeSelected(1, 0) : pane.nudge(0.1) }
    Shortcut { sequence: "Shift+Left"; enabled: pane.keys; onActivated: pane.markSelected ? video.marks.nudgeSelected(-10, 0) : pane.nudge(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: pane.keys; onActivated: pane.markSelected ? video.marks.nudgeSelected(10, 0) : pane.nudge(1) }
    Shortcut { sequence: "Up"; enabled: pane.keys && pane.markSelected; onActivated: video.marks.nudgeSelected(0, -1) }
    Shortcut { sequence: "Down"; enabled: pane.keys && pane.markSelected; onActivated: video.marks.nudgeSelected(0, 1) }
    Shortcut { sequence: "Shift+Up"; enabled: pane.keys && pane.markSelected; onActivated: video.marks.nudgeSelected(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: pane.keys && pane.markSelected; onActivated: video.marks.nudgeSelected(0, 10) }
    Shortcut { sequence: "Home"; enabled: pane.keys; onActivated: { player.pause(); pane.seek(pane.clipStart); } }
    Shortcut { sequence: "End"; enabled: pane.keys; onActivated: { player.pause(); pane.seek(Math.max(pane.clipStart, pane.clipEnd - 0.1)); } }
    Shortcut { sequence: "Ctrl+Z"; enabled: pane.keys && pane.undoStack.length > 0; onActivated: pane.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: pane.keys && pane.redoStack.length > 0; onActivated: pane.redo() }

    // Inline components do not see this file's ids; they get what they need
    // through properties.
    component Caption: Text {
        font.family: theme.fontFamily
        font.pixelSize: 10
        font.letterSpacing: 0.8
        color: theme.muted
    }
    component TimeField: TextField {
        id: field
        property string display
        property string label: "Time"
        signal committed(real seconds)
        implicitWidth: 84
        implicitHeight: 32
        text: display
        selectByMouse: true
        horizontalAlignment: TextInput.AlignHCenter
        font.family: theme.fontFamily
        font.pixelSize: 12
        color: theme.text
        selectionColor: theme.alpha(theme.accent, 0.4)
        selectedTextColor: theme.text
        padding: 6
        hoverEnabled: true
        opacity: enabled ? 1 : 0.5
        Accessible.name: label
        onActiveFocusChanged: if (activeFocus)
            selectAll()
        onAccepted: focus = false
        Keys.onEscapePressed: { text = Qt.binding(() => field.display); focus = false; }
        // Accepts "12.5", "1:04" or "01:04.2".
        onEditingFinished: {
            const m = text.trim().match(/^(?:(\d+):)?(\d+(?:\.\d*)?)$/);
            if (m)
                committed((m[1] ? Number(m[1]) * 60 : 0) + Number(m[2]));
            text = Qt.binding(() => field.display);
        }
        background: Rectangle {
            radius: theme.radius
            color: field.activeFocus ? theme.controlFill : field.hovered ? theme.hoverFill : theme.controlFill
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? theme.focusBorder : field.hovered ? theme.hoverBorder : theme.controlBorder
        }
    }
    component TrimHandle: Rectangle {
        id: handle
        required property Item lane
        // The clip edge this handle drags, in track coordinates.
        property real edge
        property string label
        signal dragged(real edge)
        signal grabbed()
        signal released()
        height: lane.height
        y: lane.y
        radius: theme.radius
        color: grab.pressed || grab.containsMouse ? theme.mix(theme.accent, theme.text, 0.18) : theme.accent
        Accessible.role: Accessible.Slider
        Accessible.name: label
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: 18
            radius: 1
            color: theme.onAccent
            opacity: 0.8
        }
        MouseArea {
            id: grab
            property real offset: 0
            anchors.fill: parent
            anchors.leftMargin: -4
            anchors.rightMargin: -4
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            onPressed: mouse => {
                offset = handle.edge - mapToItem(handle.lane, mouse.x, 0).x;
                handle.grabbed();
            }
            onPositionChanged: mouse => {
                if (pressed)
                    handle.dragged(mapToItem(handle.lane, mouse.x, 0).x + offset);
            }
            onReleased: handle.released()
        }
    }
    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 22
        color: theme.separator
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.topMargin: pane.compact ? 8 : 16
        anchors.bottomMargin: pane.compact ? 8 : 14
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: pane.compact ? 6 : 12
            spacing: 14
            Text {
                text: pane.loaded ? video.name : "No video open"
                font.pixelSize: 14
                font.weight: Font.Medium
                color: theme.text
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            Rectangle {
                visible: pane.savedCurrent
                implicitWidth: savedRow.implicitWidth + 18
                Layout.maximumWidth: pane.width * 0.65
                implicitHeight: 26
                radius: theme.radius
                color: theme.alpha(theme.accent, 0.14)
                border.width: 1
                border.color: theme.alpha(theme.accent, 0.5)
                RowLayout {
                    id: savedRow
                    anchors.fill: parent
                    anchors.margins: 9
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    spacing: 6
                    Glyph { name: "check"; ink: theme.selectedText; Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
                    Text { Layout.fillWidth: true; text: "Saved " + video.savedName + (video.savedSummary.length ? " · " + video.savedSummary : ""); color: theme.selectedText; font.pixelSize: 11; elide: Text.ElideMiddle }
                }
            }
            StudioButton {
                id: cameraMenuButton
                visible: video.cameraSource.toString().length > 0
                text: "Camera"; quiet: true; glyph: "record"; enabled: pane.editable
                onClicked: cameraMenu.opened ? cameraMenu.close() : cameraMenu.open()
                Popup {
                    id: cameraMenu
                    y: parent.height + 6
                    x: Math.min(0, pane.width - cameraMenuButton.x - width - 22)
                    width: 280; padding: 16; modal: true; focus: true
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: theme.alpha(theme.background, 1); radius: theme.radius; border.width: 2; border.color: theme.frame }
                    contentItem: ColumnLayout {
                        spacing: 10
                        Text { text: "Camera overlay"; color: theme.text; font.pixelSize: 14; font.weight: Font.Medium }
                        RecordToggle {
                            Layout.fillWidth: true; text: "Show camera"; checked: video.cameraLayout.visible
                            onToggled: pane.changeCamera({ visible: checked })
                        }
                        Caption { text: "SIZE" }
                        Choice {
                            Layout.fillWidth: true; model: ["Small", "Medium", "Large"]
                            currentIndex: video.cameraLayout.width <= 0.2 ? 0 : video.cameraLayout.width <= 0.3 ? 1 : 2
                            enabled: video.cameraLayout.visible
                            onActivated: pane.changeCamera({ width: [0.18, 0.24, 0.34][currentIndex] })
                        }
                        Caption { text: "MOVE TO CORNER" }
                        Choice {
                            Layout.fillWidth: true; model: ["Bottom right", "Bottom left", "Top right", "Top left"]
                            currentIndex: -1; displayText: "Choose a corner…"; enabled: video.cameraLayout.visible
                            onActivated: pane.changeCamera({
                                x: currentIndex % 2 ? 0.02 : 0.98 - video.cameraBounds.width,
                                y: currentIndex < 2 ? 0.98 - video.cameraBounds.height : 0.02
                            })
                        }
                        Text { Layout.fillWidth: true; text: "Pause playback, then drag the camera to move it. Changes can be undone."; color: theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                    }
                }
            }
            Text {
                visible: pane.loaded && !pane.savedCurrent
                Layout.maximumWidth: pane.width * 0.45
                elide: Text.ElideMiddle
                text: [video.marks.hasCrop ? video.outputSize.width + " × " + video.outputSize.height : video.dimensions, pane.time(video.duration), video.audioTracks === 0 ? "No sound" : video.audioTracks === 1 ? "Sound" : video.audioTracks + " sound tracks"].join("   ·   ")
                font.pixelSize: 11
                color: theme.muted
            }
        }

        Rectangle {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radius
            color: theme.well
            clip: true
            readonly property rect crop: pane.tool === "crop" ? Qt.rect(0, 0, 1, 1) : video.cropBounds
            readonly property real aspect: video.frameSize.width * crop.width / Math.max(1, video.frameSize.height * crop.height)
            Item {
                id: viewport
                z: 1
                width: Math.min(stage.width, stage.height * stage.aspect)
                height: width / Math.max(0.01, stage.aspect)
                anchors.centerIn: parent
                clip: true
                VideoOutput {
                    id: output
                    x: -stage.crop.x * width
                    y: -stage.crop.y * height
                    width: viewport.width / stage.crop.width
                    height: viewport.height / stage.crop.height
                    fillMode: VideoOutput.Stretch
                }
            }
            MouseArea {
                id: stageMouse
                anchors.fill: parent
                enabled: pane.editable
                hoverEnabled: true
                cursorShape: pane.editable ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: pane.togglePlay()
            }
            // The picture itself, inside the letterbox. Marks are placed in it.
            Item {
                id: picture
                parent: viewport
                x: output.x
                y: output.y
                width: output.width
                height: output.height
                visible: pane.loaded
                // What the saved video will show: blur and redactions while
                // they are on at the playhead.
                Repeater {
                    model: video.marks.annotations
                    Item {
                        id: preview
                        required property var modelData
                        x: modelData.x1 * picture.width
                        y: modelData.y1 * picture.height
                        width: Math.max(1, (modelData.x2 - modelData.x1) * picture.width)
                        height: Math.max(1, (modelData.y2 - modelData.y1) * picture.height)
                        visible: pane.shows(modelData.start, modelData.end)
                        Rectangle {
                            anchors.fill: parent
                            visible: preview.modelData.type === "redact"
                            color: "#151a20"
                        }
                        ShaderEffectSource {
                            id: area
                            anchors.fill: parent
                            visible: false
                            live: true
                            sourceItem: preview.modelData.type === "blur" && preview.visible ? output : null
                            sourceRect: Qt.rect(preview.x, preview.y, preview.width, preview.height)
                        }
                        MultiEffect {
                            anchors.fill: parent
                            visible: preview.modelData.type === "blur"
                            source: area
                            autoPaddingEnabled: false
                            blurEnabled: true
                            blurMax: 64
                            blur: 1
                        }
                    }
                }
                // Arrows, boxes, labels and steps, drawn by the same renderer
                // as the saved video.
                Repeater {
                    model: video.overlays
                    Image {
                        required property var modelData
                        x: modelData.x * picture.width
                        y: modelData.y * picture.height
                        width: modelData.w * picture.width
                        height: modelData.h * picture.height
                        visible: pane.shows(modelData.start, modelData.end)
                        source: modelData.source
                        cache: false
                        smooth: true
                        mipmap: true
                    }
                }
                MarkCanvas {
                    id: markCanvas
                    parent: viewport
                    anchors.fill: parent
                    visible: pane.editable && !pane.playing
                    doc: video.marks
                    tool: pane.tool
                    locked: video.busy
                    workingSize: pane.tool === "crop" ? video.frameSize : video.outputSize
                    sourceSize: video.frameSize
                    time: pane.head
                    duration: video.duration
                    onToolRequested: key => pane.tool = key
                    onEmptyClicked: hadSelection => { if (!hadSelection) pane.togglePlay(); }
                }
            }
            Item {
                id: cameraOverlay
                parent: viewport
                z: 4
                x: video.cameraBounds.x * viewport.width
                y: video.cameraBounds.y * viewport.height
                width: video.cameraBounds.width * viewport.width
                height: video.cameraBounds.height * viewport.height
                visible: video.cameraSource.toString().length > 0 && video.cameraLayout.visible && pane.head < video.cameraDuration && pane.tool !== "crop"
                clip: true
                MediaPlayer {
                    id: cameraPlayer
                    source: video.cameraSource
                    videoOutput: cameraOutput
                    onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) position = player.position
                }
                VideoOutput { id: cameraOutput; anchors.fill: parent; fillMode: VideoOutput.Stretch }
                Connections {
                    target: player
                    function onPositionChanged() {
                        if (!pane.playing || Math.abs(cameraPlayer.position - player.position) > 150)
                            cameraPlayer.position = player.position;
                    }
                    function onPlaybackStateChanged() {
                        cameraPlayer.position = player.position;
                        if (pane.playing) cameraPlayer.play(); else cameraPlayer.pause();
                    }
                }
                Rectangle { anchors.fill: parent; color: "transparent"; border.width: cameraDrag.containsMouse ? 2 : 0; border.color: theme.accent }
                MouseArea {
                    id: cameraDrag
                    anchors.fill: parent
                    enabled: pane.editable && !pane.playing
                    hoverEnabled: true
                    cursorShape: Qt.SizeAllCursor
                    property real startX
                    property real startY
                    property real layoutX
                    property real layoutY
                    property bool moved: false
                    onPressed: mouse => {
                        moved = false;
                        const point = mapToItem(viewport, mouse.x, mouse.y);
                        startX = point.x; startY = point.y;
                        layoutX = video.cameraBounds.x; layoutY = video.cameraBounds.y;
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) return;
                        const point = mapToItem(viewport, mouse.x, mouse.y);
                        if (!moved) {
                            if (Math.hypot(point.x - startX, point.y - startY) < 3) return;
                            pane.pushUndo();
                            moved = true;
                        }
                        video.setCameraLayout(Object.assign({}, video.cameraLayout, {
                            x: layoutX + (point.x - startX) / viewport.width,
                            y: layoutY + (point.y - startY) / viewport.height
                        }));
                    }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 64
                z: 3
                height: 64
                radius: theme.radius > 0 ? 32 : 0
                color: theme.alpha(theme.background, 0.78)
                border.width: 1
                border.color: theme.alpha(theme.text, 0.14)
                opacity: pane.editable && !pane.playing && pane.tool === "select" && !markCanvas.dragging && (stageMouse.containsMouse || markCanvas.hovered) ? 1 : 0
                visible: opacity > 0
                Behavior on opacity {
                    NumberAnimation { duration: 140 }
                }
                Glyph {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 2
                    name: "play"
                    width: 26
                    height: 26
                    ink: theme.text
                }
            }
            Column {
                anchors.centerIn: parent
                visible: !pane.loaded
                spacing: 8
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: video.busy ? "Opening your video…" : "Open a video to edit it."
                    color: theme.text
                    font.pixelSize: 15
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "MP4 · WebM · MOV · MKV"
                    color: theme.faint
                    font.pixelSize: 11
                }
            }
            Rectangle {
                visible: player.error !== MediaPlayer.NoError
                z: 5
                anchors.centerIn: parent
                width: parent.width - 80
                height: Math.min(stage.height - 24, playbackError.implicitHeight + 30)
                radius: theme.radius
                color: theme.alpha(theme.urgent, 0.1)
                border.width: 1
                border.color: theme.alpha(theme.urgent, 0.4)
                Text {
                    id: playbackError
                    anchors.fill: parent
                    anchors.margins: 15
                    text: "Playback unavailable: " + player.errorString
                    color: theme.urgent
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                }
            }
            Rectangle {
                anchors.fill: parent
                z: 10
                visible: video.exporting
                color: theme.alpha(theme.background, 0.82)
                MouseArea {
                    anchors.fill: parent
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(280, stage.width - 32)
                    spacing: pane.compact ? 8 : 14
                    Text {
                        Layout.fillWidth: true
                        text: video.status
                        color: theme.text
                        font.pixelSize: 15
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: theme.radius > 0 ? 2 : 0
                        color: theme.controlBorder
                        Rectangle {
                            width: parent.width * video.progress
                            height: parent.height
                            radius: parent.radius
                            color: theme.accent
                            Behavior on width {
                                NumberAnimation { duration: 180 }
                            }
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: Math.round(video.progress * 100) + "%   ·   " + pane.time(pane.outputDuration) + " long"
                        color: theme.muted
                        font.pixelSize: 11
                    }
                    StudioButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Cancel"
                        quiet: true
                        implicitHeight: 32
                        onClicked: video.cancel()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: pane.compact ? 8 : 14
            spacing: 10
            StudioButton {
                glyph: pane.playing ? "pause" : "play"
                primary: true
                implicitWidth: 40
                implicitHeight: 40
                enabled: pane.editable
                hint: (pane.playing ? "Pause" : "Play") + " · Space"
                onClicked: pane.togglePlay()
            }
            Row {
                Layout.leftMargin: 4
                spacing: 8
                Text {
                    id: clock
                    text: pane.time(pane.head)
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.Medium
                }
                Text {
                    anchors.baseline: clock.baseline
                    text: "/ " + pane.time(video.duration)
                    color: theme.faint
                    font.pixelSize: 12
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Caption {
                text: "START"
            }
            TimeField {
                id: startField
                label: "Video start"
                display: pane.time(pane.clipStart)
                enabled: pane.editable
                onCommitted: seconds => pane.setStart(seconds, true)
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: 600
                ToolTip.text: "Where the video begins · I sets it at the playhead"
            }
            Caption {
                Layout.leftMargin: 4
                text: "END"
            }
            TimeField {
                id: endField
                label: "Video end"
                display: pane.time(pane.clipEnd)
                enabled: pane.editable
                onCommitted: seconds => pane.setEnd(seconds, true)
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: 600
                ToolTip.text: "Where the video ends · O sets it at the playhead"
            }
            Divider {
                Layout.leftMargin: 4
                Layout.rightMargin: 4
            }
            Caption {
                text: "LENGTH"
            }
            Text {
                text: pane.time(pane.outputDuration)
                color: theme.selectedText
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Divider {
                Layout.leftMargin: 4
                Layout.rightMargin: 2
            }
            StudioButton {
                readonly property bool silent: pane.muted || video.audioTracks === 0
                text: video.audioTracks === 0 ? "No sound" : pane.muted ? "Sound off" : "Sound on"
                glyph: silent ? "mute" : "volume"
                quiet: true
                implicitHeight: 36
                enabled: pane.editable && video.audioTracks > 0
                hint: video.audioTracks === 0 ? "This video has no audio track."
                    : pane.muted ? "The saved video will be silent. Click to keep the sound." : "Click to save the video without sound"
                onClicked: pane.toggleSound()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 6
            StudioButton {
                text: "Crop"; glyph: "crop"; selected: pane.tool === "crop"; quiet: !selected
                implicitHeight: 36; enabled: pane.editable
                hint: "Drag a rectangle to keep that part of the video · C"
                onClicked: pane.tool === "crop" ? pane.tool = "select" : pane.useTool("crop")
            }
            StudioButton {
                visible: video.marks.hasCrop
                text: "Reset crop"; quiet: true; implicitHeight: 36; enabled: pane.editable
                onClicked: video.marks.clearCrop()
            }
            StudioButton {
                text: "Blur"
                glyph: "blur"
                quiet: pane.tool !== "blur"
                selected: pane.tool === "blur"
                implicitHeight: 36
                enabled: pane.editable
                hint: "Drag over something to blur it in the saved video · G"
                onClicked: pane.tool === "blur" ? pane.tool = "select" : pane.useTool("blur")
            }
            StudioButton {
                text: "Redact"
                glyph: "redact"
                quiet: pane.tool !== "redact"
                selected: pane.tool === "redact"
                implicitHeight: 36
                enabled: pane.editable
                hint: "Drag over something private to cover it in the saved video · R"
                onClicked: pane.tool === "redact" ? pane.tool = "select" : pane.useTool("redact")
            }
            Repeater {
                model: [
                    { key: "arrow", label: "Arrow", hint: "Arrow · A" }, { key: "box", label: "Box", hint: "Box · B" },
                    { key: "text", label: "Label", hint: "Label · T" }, { key: "step", label: "Steps", hint: "Numbered step · N" }
                ]
                StudioButton {
                    required property var modelData
                    glyph: modelData.key
                    text: modelData.label
                    quiet: pane.tool !== modelData.key
                    selected: pane.tool === modelData.key
                    implicitHeight: 36
                    enabled: pane.editable
                    hint: modelData.hint
                    onClicked: pane.tool === modelData.key ? pane.tool = "select" : pane.useTool(modelData.key)
                }
            }
            Item { Layout.fillWidth: true }
        }

        // One fixed-height bar that describes the selection, so choosing a
        // part never shifts the timeline.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: pane.compact ? 6 : 10
            Layout.preferredHeight: 36
            spacing: 8
            Text {
                visible: pane.barMode === "tool"
                Layout.fillWidth: true
                text: ({
                        crop: "Drag a rectangle to crop the whole clip. Press V to preview it; Reset crop restores the full frame.",
                        blur: "Drag over what to blur. It stays blurred for the whole clip; I and O change that. Use Redact for anything private.",
                        redact: "Drag over what to cover. It is covered for the whole clip; I and O change that.",
                        arrow: "Drag from the tail to the tip. It shows from here to the end of the clip.",
                        box: "Drag to draw a box. It shows from here to the end of the clip.",
                        text: "Click where the label should go, then type. It shows from here to the end of the clip.",
                        step: "Click to place the next number. It shows from here to the end of the clip."
                    })[pane.tool] || ""
                color: theme.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Caption {
                visible: pane.barMode === "mark"
                text: pane.markName(pane.selectedMark.type).toUpperCase()
                color: theme.selectedText
            }
            TimeField {
                id: markStartField
                label: "Mark start"
                visible: pane.barMode === "mark"
                display: pane.time(pane.selectedMark.start || 0)
                enabled: pane.editable
                onCommitted: seconds => video.marks.setSelectedTimes(seconds, pane.selectedMark.end)
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: 600
                ToolTip.text: "When it starts · I sets it at the playhead"
            }
            Text {
                visible: pane.barMode === "mark"
                text: "to"
                color: theme.muted
                font.pixelSize: 12
            }
            TimeField {
                id: markEndField
                label: "Mark end"
                visible: pane.barMode === "mark"
                display: pane.time(pane.selectedMark.end || 0)
                enabled: pane.editable
                onCommitted: seconds => video.marks.setSelectedTimes(pane.selectedMark.start, seconds)
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: 600
                ToolTip.text: "When it ends · O sets it at the playhead"
            }
            Text {
                visible: pane.barMode === "mark"
                Layout.fillWidth: true
                readonly property bool toEnd: pane.selectedMark.end >= video.duration - 0.0005
                readonly property bool hides: pane.selectedMark.type === "blur" || pane.selectedMark.type === "redact"
                text: hides && toEnd && pane.selectedMark.start <= 0.0005 ? "Covers the whole clip. I and O set where it starts and ends."
                    : !hides && toEnd ? "Shows until the end. Press O to end it here."
                    : "(" + ((pane.selectedMark.end || 0) - (pane.selectedMark.start || 0)).toFixed(1) + " s)"
                color: theme.faint
                font.pixelSize: 11
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            StudioButton {
                visible: pane.barMode === "mark"
                text: "Delete"
                glyph: "trash"
                implicitHeight: 32
                enabled: pane.editable
                hint: "Take this mark off the video · Delete"
                onClicked: video.marks.deleteSelected()
            }
            StudioButton {
                visible: pane.barMode === "mark"
                glyph: "close"
                quiet: true
                implicitHeight: 32
                hint: "Clear the selection · Esc"
                onClicked: video.marks.clearSelection()
            }
            Text {
                visible: pane.barMode === "none"
                Layout.fillWidth: true
                text: pane.notice.length ? pane.notice
                    : pane.cuts.length ? pane.cuts.length + (pane.cuts.length === 1 ? " part removed. Click it on the filmstrip to change or restore it." : " parts removed. Click one on the filmstrip to change or restore it.")
                    : "Drag the ends of the filmstrip to trim. Drag across it to select a part to remove."
                color: pane.notice.length ? theme.urgent : theme.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Caption {
                visible: pane.barMode === "part" || pane.barMode === "cut"
                text: pane.hasSelection ? "SELECTED" : "REMOVED PART"
                color: pane.hasSelection ? theme.selectedText : theme.urgent
            }
            TimeField {
                id: partStartField
                label: pane.hasSelection ? "Selected part start" : "Removed part start"
                visible: pane.barMode === "part" || pane.barMode === "cut"
                display: pane.time(pane.hasSelection ? pane.selStart : pane.selectedCut >= 0 ? pane.cuts[pane.selectedCut].start : 0)
                enabled: pane.editable
                onCommitted: seconds => pane.hasSelection ? pane.setSelectionStart(seconds) : pane.setCutTimes(pane.selectedCut, seconds, pane.cuts[pane.selectedCut].end)
            }
            Text {
                visible: pane.barMode === "part" || pane.barMode === "cut"
                text: "to"
                color: theme.muted
                font.pixelSize: 12
            }
            TimeField {
                id: partEndField
                label: pane.hasSelection ? "Selected part end" : "Removed part end"
                visible: pane.barMode === "part" || pane.barMode === "cut"
                display: pane.time(pane.hasSelection ? pane.selEnd : pane.selectedCut >= 0 ? pane.cuts[pane.selectedCut].end : 0)
                enabled: pane.editable
                onCommitted: seconds => pane.hasSelection ? pane.setSelectionEnd(seconds) : pane.setCutTimes(pane.selectedCut, pane.cuts[pane.selectedCut].start, seconds)
            }
            Text {
                visible: pane.barMode === "part" || pane.barMode === "cut"
                text: "(" + ((pane.hasSelection ? pane.selEnd - pane.selStart : pane.selectedCut >= 0 ? pane.cuts[pane.selectedCut].end - pane.cuts[pane.selectedCut].start : 0)).toFixed(1) + " s)"
                color: theme.faint
                font.pixelSize: 11
            }
            StudioButton {
                visible: pane.barMode === "part"
                text: "Remove this part"
                glyph: "cut"
                implicitHeight: 32
                enabled: pane.editable
                hint: "The rest joins together in the saved video · Delete"
                onClicked: pane.removeSelection()
            }
            StudioButton {
                visible: pane.barMode === "cut"
                text: "Restore"
                glyph: "undo"
                implicitHeight: 32
                enabled: pane.editable
                hint: "Put this part back in the video"
                onClicked: pane.restoreCut(pane.selectedCut)
            }
            StudioButton {
                visible: pane.barMode === "part" || pane.barMode === "cut"
                glyph: "close"
                quiet: true
                implicitHeight: 32
                hint: "Clear the selection · Esc"
                onClicked: pane.clearSelection()
            }
            Text {
                visible: pane.notice.length > 0 && (pane.barMode === "part" || pane.barMode === "cut")
                Layout.fillWidth: true
                text: pane.notice
                color: theme.urgent
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: pane.barMode === "part" || pane.barMode === "cut" }
            StudioButton {
                glyph: "undo"
                quiet: true
                implicitHeight: 32
                enabled: pane.editable && pane.undoStack.length > 0
                hint: "Undo · Ctrl+Z"
                onClicked: pane.undo()
            }
            StudioButton {
                glyph: "redo"
                quiet: true
                implicitHeight: 32
                enabled: pane.editable && pane.redoStack.length > 0
                hint: "Redo · Ctrl+Shift+Z"
                onClicked: pane.redo()
            }
        }

        Item {
            id: timeline
            readonly property real gutter: 12
            Layout.fillWidth: true
            Layout.topMargin: pane.compact ? 4 : 8
            Layout.preferredHeight: ruler.y + ruler.height + 2
            enabled: pane.editable
            opacity: pane.loaded ? 1 : 0.35

            Item {
                id: track
                x: timeline.gutter
                y: 8
                width: timeline.width - 2 * timeline.gutter
                height: pane.compact ? 42 : 58
                function xFor(seconds) {
                    return video.duration > 0 ? seconds / video.duration * width : 0;
                }
                function secondsAt(x) {
                    return video.duration > 0 ? Math.max(0, Math.min(video.duration, x / width * video.duration)) : 0;
                }
                Rectangle {
                    anchors.fill: parent
                    radius: theme.radius
                    color: theme.well
                    clip: true
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: video.thumbnails
                            Image {
                                required property string modelData
                                width: track.width / Math.max(1, video.thumbnails.length)
                                height: track.height
                                source: modelData
                                sourceSize.height: 120
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: false
                                clip: true
                                opacity: status === Image.Ready ? 1 : 0
                                Behavior on opacity {
                                    NumberAnimation { duration: 220 }
                                }
                            }
                        }
                    }
                }
                MouseArea {
                    id: scrub
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.IBeamCursor
                    property real anchor: 0
                    property real pressX: 0
                    property bool dragging: false
                    onPressed: mouse => {
                        player.pause();
                        pressX = mouse.x;
                        dragging = false;
                        anchor = track.secondsAt(mouse.x);
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        if (!dragging && Math.abs(mouse.x - pressX) > 4)
                            dragging = true;
                        if (dragging) {
                            const at = track.secondsAt(mouse.x);
                            pane.select(anchor, at);
                            player.position = Math.max(pane.clipStart, Math.min(pane.clipEnd, at)) * 1000;
                        }
                    }
                    onReleased: mouse => {
                        if (dragging) {
                            if (!pane.hasSelection)
                                pane.notice = "Drag across at least a tenth of a second.";
                            return;
                        }
                        const at = track.secondsAt(mouse.x);
                        const cut = pane.cutAt(at);
                        if (cut >= 0)
                            pane.selectCut(cut);
                        else {
                            pane.clearSelection();
                            video.marks.clearSelection();
                            pane.seek(at);
                        }
                    }
                }
                Rectangle {
                    width: track.xFor(pane.clipStart)
                    height: parent.height
                    color: theme.alpha(theme.background, 0.74)
                }
                Rectangle {
                    x: track.xFor(pane.clipEnd)
                    width: parent.width - x
                    height: parent.height
                    color: theme.alpha(theme.background, 0.74)
                }
                Repeater {
                    model: pane.cuts.length
                    Rectangle {
                        required property int index
                        x: track.xFor(pane.cuts[index].start)
                        width: Math.max(2, track.xFor(pane.cuts[index].end) - x)
                        height: track.height
                        color: theme.alpha(theme.background, 0.62)
                        border.width: pane.selectedCut === index ? 2 : 1
                        border.color: theme.urgent
                        clip: true
                        Canvas {
                            anchors.fill: parent
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const c = getContext("2d");
                                c.reset();
                                c.strokeStyle = theme.alpha(theme.urgent, 0.55);
                                c.lineWidth = 2;
                                for (let x = -height; x < width; x += 10) {
                                    c.beginPath();
                                    c.moveTo(x, height);
                                    c.lineTo(x + height, 0);
                                    c.stroke();
                                }
                            }
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            visible: parent.width > 80
                            width: removedLabel.implicitWidth + 12
                            height: 18
                            radius: theme.radius
                            color: theme.alpha(theme.background, 0.9)
                            border.width: 1
                            border.color: theme.urgent
                            Text {
                                id: removedLabel
                                anchors.centerIn: parent
                                text: "REMOVED"
                                color: theme.urgent
                                font.family: theme.fontFamily
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
                Rectangle {
                    visible: pane.hasSelection
                    x: track.xFor(pane.selStart)
                    width: Math.max(2, track.xFor(pane.selEnd) - x)
                    height: track.height
                    color: theme.alpha(theme.background, 0.45)
                    border.width: 3
                    border.color: theme.accent
                    Rectangle {
                        anchors.centerIn: parent
                        visible: parent.width > 80
                        width: selectedLabel.implicitWidth + 12
                        height: 18
                        radius: theme.radius
                        color: theme.accent
                        Text {
                            id: selectedLabel
                            anchors.centerIn: parent
                            text: "SELECTED"
                            color: theme.onAccent
                            font.family: theme.fontFamily
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }
                }
                Rectangle {
                    x: track.xFor(pane.clipStart)
                    width: track.xFor(pane.clipEnd) - x
                    height: parent.height
                    color: "transparent"
                    border.width: 2
                    border.color: theme.accent
                }
                Rectangle {
                    visible: scrub.containsMouse && !scrub.pressed
                    x: Math.max(0, Math.min(track.width, scrub.mouseX))
                    width: 1
                    height: parent.height
                    color: theme.alpha(theme.text, 0.4)
                }
                Item {
                    x: track.xFor(pane.head)
                    height: parent.height
                    Rectangle {
                        x: -1
                        y: -4
                        width: 2
                        height: track.height + 8
                        color: theme.text
                    }
                    Rectangle {
                        x: -5
                        y: -8
                        width: 10
                        height: 6
                        radius: theme.radius > 0 ? 2 : 0
                        color: theme.text
                    }
                }
            }

            TrimHandle {
                lane: track
                label: "Video start"
                edge: track.xFor(pane.clipStart)
                x: track.x + edge - width
                width: timeline.gutter
                onGrabbed: pane.pendingUndo = pane.snapshot()
                onDragged: edge => pane.setStart(Math.round(track.secondsAt(edge) * 10) / 10, false)
            }
            TrimHandle {
                lane: track
                label: "Video end"
                edge: track.xFor(pane.clipEnd)
                x: track.x + edge
                width: timeline.gutter
                onGrabbed: pane.pendingUndo = pane.snapshot()
                onDragged: edge => pane.setEnd(Math.round(track.secondsAt(edge) * 10) / 10, false)
            }

            // One bar per mark, for when it shows. Click one to select it; the
            // selected one has handles.
            Item {
                id: markLane
                x: track.x
                y: track.y + track.height + 6
                width: track.width
                height: pane.hasMarks ? 12 : 0
                visible: pane.hasMarks
                // The new times while a handle is dragged, applied on release
                // so the whole drag is one undo step.
                property real draftStart: -1
                property real draftEnd: -1
                readonly property real shownStart: draftStart >= 0 ? draftStart : pane.selectedMark.start || 0
                readonly property real shownEnd: draftEnd >= 0 ? draftEnd : pane.selectedMark.end || 0
                function grab() {
                    player.pause();
                    draftStart = pane.selectedMark.start;
                    draftEnd = pane.selectedMark.end;
                }
                function commit() {
                    if (draftStart >= 0)
                        video.marks.setSelectedTimes(draftStart, draftEnd);
                    draftStart = draftEnd = -1;
                }
                Repeater {
                    model: video.marks.annotations
                    Rectangle {
                        required property var modelData
                        readonly property bool chosen: modelData.index === pane.selectedMark.index
                        x: track.xFor(chosen ? markLane.shownStart : modelData.start)
                        width: Math.max(4, track.xFor(chosen ? markLane.shownEnd : modelData.end) - x)
                        height: markLane.height
                        radius: theme.radius > 0 ? height / 2 : 0
                        z: chosen ? 1 : 0
                        color: chosen ? theme.accent : theme.alpha(theme.text, 0.22)
                        border.width: 1
                        border.color: chosen ? theme.accent : theme.alpha(theme.background, 0.8)
                        Accessible.role: Accessible.Button
                        Accessible.name: pane.markName(modelData.type) + " from " + pane.time(modelData.start) + " to " + pane.time(modelData.end)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pane.selectMark(parent.modelData.index, parent.modelData.start, parent.modelData.end)
                        }
                    }
                }
            }
            TrimHandle {
                lane: markLane
                visible: pane.markSelected
                label: "Mark start"
                edge: track.xFor(markLane.shownStart)
                x: markLane.x + edge - width
                width: 10
                onGrabbed: markLane.grab()
                onDragged: edge => {
                    markLane.draftStart = Math.max(0, Math.min(markLane.draftEnd - 0.1, Math.round(track.secondsAt(edge) * 10) / 10));
                    pane.seek(markLane.draftStart);
                }
                onReleased: markLane.commit()
            }
            TrimHandle {
                lane: markLane
                visible: pane.markSelected
                label: "Mark end"
                edge: track.xFor(markLane.shownEnd)
                x: markLane.x + edge
                width: 10
                onGrabbed: markLane.grab()
                onDragged: edge => {
                    markLane.draftEnd = Math.min(video.duration, Math.max(markLane.draftStart + 0.1, Math.round(track.secondsAt(edge) * 10) / 10));
                    pane.seek(Math.max(markLane.draftStart, markLane.draftEnd - 0.1));
                }
                onReleased: markLane.commit()
            }

            // The time ruler doubles as a scrub strip.
            Item {
                id: ruler
                x: track.x
                y: markLane.y + markLane.height + (pane.hasMarks ? 4 : -2)
                width: track.width
                height: 20
                readonly property real step: {
                    const fit = Math.max(1, width / 84);
                    for (const s of [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800])
                        if (video.duration / s <= fit)
                            return s;
                    return 3600;
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true
                    onPressed: mouse => { player.pause(); pane.seek(track.secondsAt(mouse.x)); }
                    onPositionChanged: mouse => { if (pressed) pane.seek(track.secondsAt(mouse.x)); }
                }
                Repeater {
                    model: video.duration > 0 ? Math.floor(video.duration / ruler.step) + 1 : 0
                    Item {
                        required property int index
                        readonly property real seconds: index * ruler.step
                        x: track.xFor(seconds)
                        Rectangle {
                            width: 1
                            height: 4
                            color: theme.faint
                        }
                        Text {
                            x: index === 0 ? 0 : parent.x + width / 2 > ruler.width ? -width : -width / 2
                            y: 5
                            text: Math.floor(parent.seconds / 60) + ":" + Math.floor(parent.seconds % 60).toString().padStart(2, "0") + (parent.seconds % 1 ? ".5" : "")
                            color: theme.faint
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }
}
