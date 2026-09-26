import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia

// The clip editor: one stage, one transport row, and one timeline where the
// filmstrip, trim handles and playhead live together.
Item {
    id: pane
    property bool shortcutsAllowed: true
    property bool muted: false
    property real clipStart: 0
    property real clipEnd: 0
    readonly property bool loaded: video.source.toString().length > 0
    readonly property bool editable: loaded && !video.busy
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    // The playhead in seconds, advanced every frame between the player's
    // coarser position updates so it glides instead of stepping.
    property real head: 0
    property real anchorTime: 0
    property real anchorClock: 0

    function time(seconds) {
        let n = Math.max(0, seconds);
        return Math.floor(n / 60).toString().padStart(2, "0") + ":" + (n % 60).toFixed(1).padStart(4, "0");
    }
    function pause() {
        player.pause();
    }
    function seek(seconds) {
        player.position = Math.max(0, Math.min(video.duration, seconds)) * 1000;
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
    function setStart(seconds) {
        player.pause();
        clipStart = Math.max(0, Math.min(clipEnd - 0.1, seconds));
        seek(clipStart);
    }
    function setEnd(seconds) {
        player.pause();
        clipEnd = Math.min(video.duration, Math.max(clipStart + 0.1, seconds));
        seek(Math.max(clipStart, clipEnd - 0.1));
    }
    function nudge(seconds) {
        player.pause();
        seek(player.position / 1000 + seconds);
    }

    onVisibleChanged: if (!visible)
        player.pause()
    MediaPlayer {
        id: player
        source: video.source
        videoOutput: output
        audioOutput: AudioOutput {
            muted: pane.muted
        }
        onPositionChanged: {
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
        onTriggered: pane.head = Math.min(pane.clipEnd, pane.anchorTime + (Date.now() - pane.anchorClock) / 1000)
    }
    Connections {
        target: video
        function onLoaded() {
            pane.clipStart = 0;
            pane.clipEnd = video.duration;
            player.pause();
            player.position = 0;
            pane.head = 0;
        }
    }
    readonly property bool keys: visible && editable && shortcutsAllowed
    Shortcut { sequence: "Space"; enabled: pane.keys; onActivated: pane.togglePlay() }
    Shortcut { sequence: "I"; enabled: pane.keys; onActivated: pane.setStart(player.position / 1000) }
    Shortcut { sequence: "O"; enabled: pane.keys; onActivated: pane.setEnd(player.position / 1000) }
    Shortcut { sequence: "Left"; enabled: pane.keys; onActivated: pane.nudge(-0.1) }
    Shortcut { sequence: "Right"; enabled: pane.keys; onActivated: pane.nudge(0.1) }
    Shortcut { sequence: "Shift+Left"; enabled: pane.keys; onActivated: pane.nudge(-1) }
    Shortcut { sequence: "Shift+Right"; enabled: pane.keys; onActivated: pane.nudge(1) }
    Shortcut { sequence: "Home"; enabled: pane.keys; onActivated: { player.pause(); pane.seek(pane.clipStart); } }
    Shortcut { sequence: "End"; enabled: pane.keys; onActivated: { player.pause(); pane.seek(Math.max(pane.clipStart, pane.clipEnd - 0.1)); } }

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
        onActiveFocusChanged: if (activeFocus)
            selectAll()
        onAccepted: focus = false
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
            onPressed: mouse => offset = handle.edge - mapToItem(handle.lane, mouse.x, 0).x
            onPositionChanged: mouse => {
                if (pressed)
                    handle.dragged(mapToItem(handle.lane, mouse.x, 0).x + offset);
            }
        }
    }
    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 22
        color: theme.separator
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        anchors.topMargin: 20
        anchors.bottomMargin: 16
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 14
            spacing: 16
            Text {
                text: pane.loaded ? video.name : "No recording open"
                font.pixelSize: 15
                font.weight: Font.Medium
                color: theme.text
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Text {
                visible: pane.loaded
                text: [video.dimensions, pane.time(video.duration), video.audioTracks === 0 ? "No audio" : video.audioTracks === 1 ? "Audio" : video.audioTracks + " audio tracks"].join("   ·   ")
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
            VideoOutput {
                id: output
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
            }
            MouseArea {
                id: stageMouse
                anchors.fill: parent
                enabled: pane.editable
                hoverEnabled: true
                cursorShape: pane.editable ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: pane.togglePlay()
            }
            Rectangle {
                anchors.centerIn: parent
                width: 64
                height: 64
                radius: theme.radius > 0 ? 32 : 0
                color: theme.alpha(theme.background, 0.78)
                border.width: 1
                border.color: theme.alpha(theme.text, 0.14)
                opacity: pane.editable && !pane.playing && stageMouse.containsMouse ? 1 : 0
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
                    text: "Open a recording to trim it."
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
                anchors.centerIn: parent
                width: parent.width - 80
                height: 80
                radius: theme.radius
                color: theme.alpha(theme.urgent, 0.1)
                border.width: 1
                border.color: theme.alpha(theme.urgent, 0.4)
                Text {
                    anchors.fill: parent
                    anchors.margins: 15
                    text: "Playback unavailable: " + player.errorString
                    color: theme.urgent
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
            }
            Rectangle {
                anchors.fill: parent
                visible: video.exporting
                color: theme.alpha(theme.background, 0.82)
                MouseArea {
                    anchors.fill: parent
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: 280
                    spacing: 14
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Exporting clip"
                        color: theme.text
                        font.pixelSize: 15
                        font.weight: Font.Medium
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
                        text: Math.round(video.progress * 100) + "%   ·   " + pane.time(pane.clipEnd - pane.clipStart) + " clip"
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
            Layout.topMargin: 16
            spacing: 12
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
                text: "IN"
            }
            TimeField {
                display: pane.time(pane.clipStart)
                enabled: pane.editable
                onCommitted: seconds => pane.setStart(seconds)
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: 600
                ToolTip.text: "Clip start · I sets it at the playhead"
            }
            Caption {
                Layout.leftMargin: 6
                text: "OUT"
            }
            TimeField {
                display: pane.time(pane.clipEnd)
                enabled: pane.editable
                onCommitted: seconds => pane.setEnd(seconds)
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: 600
                ToolTip.text: "Clip end · O sets it at the playhead"
            }
            Divider {
                Layout.leftMargin: 6
                Layout.rightMargin: 6
            }
            Caption {
                text: "CLIP"
            }
            Text {
                text: pane.time(pane.clipEnd - pane.clipStart)
                color: theme.selectedText
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Divider {
                Layout.leftMargin: 6
                Layout.rightMargin: 2
            }
            StudioButton {
                readonly property bool silent: pane.muted || video.audioTracks === 0
                text: silent ? "No audio" : "Audio"
                glyph: silent ? "mute" : "volume"
                quiet: true
                implicitHeight: 36
                enabled: pane.editable && video.audioTracks > 0
                hint: pane.muted ? "Audio will be removed from the clip. Click to keep it." : "Audio is kept in the clip. Click to remove it."
                onClicked: pane.muted = !pane.muted
            }
        }

        Item {
            id: timeline
            readonly property real gutter: 12
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.preferredHeight: track.y + track.height + 24
            enabled: pane.editable
            opacity: pane.loaded ? 1 : 0.35

            Item {
                id: track
                x: timeline.gutter
                y: 8
                width: timeline.width - 2 * timeline.gutter
                height: 56
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
                    anchors.topMargin: -8
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.PointingHandCursor
                    function go(x) {
                        player.pause();
                        pane.seek(track.secondsAt(x));
                    }
                    onPressed: mouse => go(mouse.x)
                    onPositionChanged: mouse => {
                        if (pressed)
                            go(mouse.x);
                    }
                }
                Rectangle {
                    width: track.xFor(pane.clipStart)
                    height: parent.height
                    color: theme.alpha(theme.background, 0.72)
                }
                Rectangle {
                    x: track.xFor(pane.clipEnd)
                    width: parent.width - x
                    height: parent.height
                    color: theme.alpha(theme.background, 0.72)
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
                label: "Clip start"
                edge: track.xFor(pane.clipStart)
                x: track.x + edge - width
                width: timeline.gutter
                onDragged: edge => pane.setStart(Math.round(track.secondsAt(edge) * 10) / 10)
            }
            TrimHandle {
                lane: track
                label: "Clip end"
                edge: track.xFor(pane.clipEnd)
                x: track.x + edge
                width: timeline.gutter
                onDragged: edge => pane.setEnd(Math.round(track.secondsAt(edge) * 10) / 10)
            }

            Item {
                id: ruler
                x: track.x
                y: track.y + track.height + 6
                width: track.width
                height: 16
                readonly property real step: {
                    const fit = Math.max(1, width / 84);
                    for (const s of [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800])
                        if (video.duration / s <= fit)
                            return s;
                    return 3600;
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
