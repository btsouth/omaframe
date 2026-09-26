import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: !captureAtStartup
    width: 1360
    height: 900
    minimumWidth: 980
    minimumHeight: 730
    title: "Omaframe"
    color: theme.alpha(theme.background, 1)
    font.family: theme.fontFamily
    font.pixelSize: 13
    property bool videoMode: false
    property bool shortcutsAllowed: !openDialog.visible && !folderDialog.visible && !textDialog.opened && !captureMenu.opened && !aspectChoice.popup.visible
    property bool working: studio.busy || video.busy || recorder.active
    property string currentStatus: videoMode ? video.status : studio.status
    property string currentDirectory: videoMode ? video.outputDirectory : studio.outputDirectory
    property string currentSaved: videoMode ? video.savedPath : studio.savedPath
    function acceptCurrent() {
        root.contentItem.forceActiveFocus();
        if (videoMode) {
            videoPane.pause();
            video.exportClip(videoPane.clipStart, videoPane.clipEnd, videoPane.muted);
        } else
            studio.accept();
    }
    property bool editing: false
    property string tool: "crop"
    property real textX: 0
    property real textY: 0
    property string toolDescription: ({
            crop: "Drag around the part you want to keep.",
            arrow: "Drag from the start to the tip of your arrow.",
            highlight: "Drag across the detail you want to emphasize.",
            redact: "Drag over private details to replace their pixels.",
            step: "Click to add the next numbered step.",
            text: "Click where you want to add a short label."
        })[tool] || ""

    onClosing: function (close) {
        if (!visible) return;
        if (root.working) {
            close.accepted = false;
            return;
        }
        if (studio.quickMode) { close.accepted = false; studio.dismissQuick(); }
        else Qt.quit();
    }
    Connections {
        target: studio
        function onEditorRequested() { root.editing = true; root.videoMode = false; }
        function onSourceChanged() {
            root.editing = false;
            root.videoMode = false;
        }
    }
    Connections {
        target: video
        function onOpening() {
            root.videoMode = true;
            root.editing = false;
        }
    }
    Shortcut {
        sequence: "Ctrl+O"
        enabled: root.shortcutsAllowed && !root.working && !studio.quickMode
        onActivated: openDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+S"
        enabled: root.shortcutsAllowed
        onActivated: root.acceptCurrent()
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        enabled: root.shortcutsAllowed && !root.videoMode
        onActivated: root.acceptCurrent()
    }
    Shortcut {
        sequence: "Return"
        enabled: root.shortcutsAllowed && !root.videoMode && !root.editing && (!root.activeFocusItem || root.activeFocusItem.objectName !== "finishChoice")
        onActivated: root.acceptCurrent()
    }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: studio.undo()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Z"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: studio.redo()
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.shortcutsAllowed && (root.editing || studio.quickMode) && !textDialog.opened
        onActivated: studio.quickMode ? studio.showFinishes() : root.editing = false
    }
    Shortcut {
        sequence: "C"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: root.tool = "crop"
    }
    Shortcut {
        sequence: "A"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: root.tool = "arrow"
    }
    Shortcut {
        sequence: "H"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: root.tool = "highlight"
    }
    Shortcut {
        sequence: "R"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: root.tool = "redact"
    }
    Shortcut {
        sequence: "N"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: root.tool = "step"
    }
    Shortcut {
        sequence: "T"
        enabled: root.shortcutsAllowed && root.editing && !textDialog.opened
        onActivated: root.tool = "text"
    }

    FileDialog {
        id: openDialog
        title: "Open an image or recording"
        nameFilters: ["Media (*.png *.jpg *.jpeg *.webp *.bmp *.avif *.heic *.mp4 *.webm *.mkv *.mov *.m4v)", "Images (*.png *.jpg *.jpeg *.webp *.bmp)", "Recordings (*.mp4 *.webm *.mkv *.mov *.m4v)"]
        onAccepted: studio.open(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        title: "Save finished screenshots here"
        onAccepted: root.videoMode ? video.setOutputDirectory(selectedFolder) : studio.setOutputDirectory(selectedFolder)
    }
    Dialog {
        id: textDialog
        anchors.centerIn: parent
        title: "Add a label"
        modal: true
        width: 400
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            labelInput.text = "";
            labelInput.forceActiveFocus();
        }
        onAccepted: studio.edit("text", root.textX, root.textY, root.textX, root.textY, labelInput.text)
        TextField {
            id: labelInput
            width: parent.width
            placeholderText: "Something worth pointing out"
            maximumLength: 120
            onAccepted: textDialog.accept()
        }
    }
    Popup {
        id: captureMenu
        x: root.width - width - 26
        y: 70
        width: 360
        padding: 16
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: theme.alpha(theme.background, 1)
            radius: theme.radius
            border.width: 2
            border.color: theme.frame
        }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Text {
                text: "NEW CAPTURE"
                font.pixelSize: 10
                color: theme.muted
            }
            Choice {
                id: displayChoice
                Layout.fillWidth: true
                model: studio.monitors
                font.pixelSize: 12
            }
            StudioButton {
                text: "Select a region"
                glyph: "capture"
                primary: true
                Layout.fillWidth: true
                onClicked: {
                    captureMenu.close();
                    studio.capture(true, displayChoice.currentIndex);
                }
            }
            StudioButton {
                text: displayChoice.currentIndex === 0 ? "Entire active display" : "Entire display"
                glyph: "image"
                Layout.fillWidth: true
                onClicked: {
                    captureMenu.close();
                    studio.capture(false, displayChoice.currentIndex);
                }
            }
            Text {
                text: "Your screen freezes before you select."
                font.pixelSize: 11
                color: theme.faint
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            color: theme.alpha(theme.background, 1)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 12
                Rectangle {
                    width: 35
                    height: 35
                    radius: theme.radius
                    color: theme.accent
                    Glyph {
                        anchors.centerIn: parent
                        name: "capture"
                        ink: theme.onAccent
                        width: 22
                        height: 22
                    }
                }
                Text {
                    text: "omaframe"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: theme.text
                }
                Rectangle {
                    Layout.leftMargin: 3
                    width: 43
                    height: 21
                    radius: theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: theme.controlBorder
                    Text {
                        anchors.centerIn: parent
                        text: "0.2"
                        font.pixelSize: 10
                        color: theme.muted
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                StudioButton {
                    text: studio.quickMode ? "Back to finishes" : "Open media"
                    glyph: "image"
                    quiet: true
                    enabled: !root.working
                    hint: studio.quickMode ? "Keep edits and choose a finish · Esc" : "Open an image or recording · Ctrl+O"
                    onClicked: studio.quickMode ? studio.showFinishes() : openDialog.open()
                }
                StudioButton {
                    text: "New capture"
                    glyph: "capture"
                    enabled: !root.working
                    onClicked: captureMenu.open()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.separator
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            visible: !root.videoMode
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 28
                spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 5
                        Text {
                            text: root.editing ? "Edit screenshot" : "A little finish goes a long way."
                            font.pixelSize: root.width < 1150 ? 22 : 26
                            font.weight: Font.Medium
                            color: theme.text
                        }
                        RowLayout {
                            spacing: 8
                            Text {
                                text: studio.name
                                elide: Text.ElideMiddle
                                Layout.maximumWidth: 230
                                color: theme.muted
                                font.pixelSize: 12
                            }
                            Text {
                                text: "·  " + studio.dimensions
                                color: theme.faint
                                font.pixelSize: 12
                            }
                            Rectangle {
                                visible: studio.demo
                                width: 47
                                height: 19
                                radius: theme.radius
                                color: theme.selectedFill
                                Text {
                                    anchors.centerIn: parent
                                    text: "DEMO"
                                    font.pixelSize: 9
                                    color: theme.selectedText
                                }
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
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
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                implicitWidth: modeRow.implicitWidth + 8
                                implicitHeight: 42
                                radius: theme.radius
                                color: theme.controlFill
                                border.color: theme.controlBorder
                                RowLayout {
                                    id: modeRow
                                    anchors.centerIn: parent
                                    spacing: 2
                                    StudioButton {
                                        text: "Finish"
                                        glyph: "spark"
                                        quiet: !selected
                                        selected: !root.editing
                                        implicitHeight: 34
                                        onClicked: root.editing = false
                                    }
                                    StudioButton {
                                        text: "Edit"
                                        glyph: "crop"
                                        quiet: !selected
                                        selected: root.editing
                                        implicitHeight: 34
                                        onClicked: root.editing = true
                                    }
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: !root.editing
                                text: studio.style === 8 ? "ORIGINAL PIXELS" : studio.styles[studio.style].toUpperCase()
                                font.pixelSize: 10
                                color: theme.muted
                            }
                            StudioButton {
                                visible: root.editing
                                glyph: "undo"
                                quiet: true
                                enabled: studio.canUndo && !studio.rendering && !studio.busy
                                hint: "Undo · Ctrl+Z"
                                onClicked: studio.undo()
                            }
                            StudioButton {
                                visible: root.editing
                                glyph: "redo"
                                quiet: true
                                enabled: studio.canRedo && !studio.rendering && !studio.busy
                                hint: "Redo · Ctrl+Shift+Z"
                                onClicked: studio.redo()
                            }
                        }
                        Item {
                            id: canvasArea
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Image {
                                id: preview
                                anchors.fill: parent
                                anchors.margins: root.editing ? 10 : 18
                                source: studio.hasImage && studio.revision > 0 ? "image://frames/" + (root.editing ? "source" : "preview") + "?" + studio.revision : ""
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
                                    cursorShape: Qt.CrossCursor
                                    enabled: !studio.busy && !studio.rendering
                                    property real startX: 0
                                    property real startY: 0
                                    property real endX: 0
                                    property real endY: 0
                                    onPressed: function (mouse) {
                                        startX = mouse.x;
                                        startY = mouse.y;
                                        endX = mouse.x;
                                        endY = mouse.y;
                                        guide.requestPaint();
                                    }
                                    onPositionChanged: function (mouse) {
                                        if (pressed) {
                                            endX = Math.max(0, Math.min(width, mouse.x));
                                            endY = Math.max(0, Math.min(height, mouse.y));
                                            guide.requestPaint();
                                        }
                                    }
                                    onReleased: function (mouse) {
                                        if (root.tool === "text") {
                                            root.textX = startX / width;
                                            root.textY = startY / height;
                                            textDialog.open();
                                        } else
                                            studio.edit(root.tool, startX / width, startY / height, endX / width, endY / height);
                                        guide.requestPaint();
                                    }
                                    onCanceled: guide.requestPaint()
                                    Canvas {
                                        id: guide
                                        anchors.fill: parent
                                        onPaint: {
                                            let c = getContext("2d");
                                            c.reset();
                                            if (!drawArea.pressed)
                                                return;
                                            let x = drawArea.startX, y = drawArea.startY, w = drawArea.endX - x, h = drawArea.endY - y;
                                            c.strokeStyle = theme.accent;
                                            c.lineWidth = 2;
                                            c.setLineDash([5, 3]);
                                            if (root.tool === "arrow") {
                                                c.beginPath();
                                                c.moveTo(x, y);
                                                c.lineTo(x + w, y + h);
                                                c.stroke();
                                            } else if (root.tool !== "text" && root.tool !== "step") {
                                                c.fillStyle = root.tool === "redact" ? theme.alpha(theme.urgent, 0.22) : theme.alpha(theme.accent, 0.18);
                                                c.fillRect(x, y, w, h);
                                                c.strokeRect(x, y, w, h);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 4
                            visible: root.editing
                            Repeater {
                                model: [
                                    {
                                        key: "crop",
                                        label: "Crop · C"
                                    },
                                    {
                                        key: "arrow",
                                        label: "Arrow · A"
                                    },
                                    {
                                        key: "highlight",
                                        label: "Highlight · H"
                                    },
                                    {
                                        key: "redact",
                                        label: "Redact · R"
                                    },
                                    {
                                        key: "step",
                                        label: "Steps · N"
                                    },
                                    {
                                        key: "text",
                                        label: "Text · T"
                                    }
                                ]
                                StudioButton {
                                    required property var modelData
                                    glyph: modelData.key
                                    selected: root.tool === modelData.key
                                    quiet: !selected
                                    hint: modelData.label
                                    onClicked: root.tool = modelData.key
                                    implicitWidth: 43
                                    implicitHeight: 40
                                }
                            }
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            visible: root.editing
                            text: root.toolDescription
                            font.pixelSize: 11
                            color: theme.muted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            visible: !root.editing
                            text: studio.rendering ? "Refining the preview…" : "Full-resolution export  ·  " + studio.outputDimensions + "  ·  PNG"
                            font.pixelSize: 11
                            color: theme.faint
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        width: 6
                        height: 6
                        radius: theme.radius > 0 ? 3 : 0
                        color: theme.accent
                    }
                    Text {
                        text: root.editing ? "Your original stays untouched." : "Full-resolution export. Original preserved."
                        font.pixelSize: 11
                        color: theme.muted
                        Layout.fillWidth: true
                    }
                    StudioButton {
                        visible: !studio.quickMode
                        text: studio.demo ? "Try " + (studio.name === "Terminal sample" ? "light" : "dark") + " sample" : "View sample"
                        quiet: true
                        implicitHeight: 28
                        font.pixelSize: 11
                        enabled: !root.working
                        onClicked: studio.loadDemo(studio.name === "Terminal sample" ? 0 : 1)
                    }
                }
            }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: theme.separator
            }
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 282
                color: theme.alpha(theme.background, 1)
                ScrollView {
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: parent.width
                        spacing: 17
                        Item {
                            Layout.preferredHeight: 6
                        }
                        ColumnLayout {
                            Layout.leftMargin: 22
                            Layout.rightMargin: 22
                            spacing: 6
                            Layout.fillWidth: true
                            Text {
                                text: "THE FINISH"
                                font.pixelSize: 10
                                color: theme.muted
                            }
                            Text {
                                text: "Finish and canvas"
                                font.pixelSize: 17
                                font.weight: Font.Medium
                                color: theme.text
                            }
                        }
                        GridLayout {
                            Layout.leftMargin: 18
                            Layout.rightMargin: 18
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 12
                            Repeater {
                                model: 8
                                Item {
                                    id: finishChoice
                                    objectName: "finishChoice"
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 93
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        height: 70
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
                                        y: 77
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
                                        onClicked: {
                                            studio.style = index;
                                            root.editing = false;
                                        }
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: studio.styles[index] + " finish"
                                    Accessible.onPressAction: {
                                        studio.style = index;
                                        root.editing = false;
                                    }
                                    activeFocusOnTab: true
                                    Keys.onReturnPressed: {
                                        studio.style = index;
                                        root.editing = false;
                                        root.contentItem.forceActiveFocus();
                                    }
                                    Keys.onSpacePressed: {
                                        studio.style = index;
                                        root.editing = false;
                                        root.contentItem.forceActiveFocus();
                                    }
                                }
                            }
                        }
                        StudioButton {
                            Layout.leftMargin: 18
                            Layout.rightMargin: 18
                            Layout.fillWidth: true
                            text: "Raw"
                            glyph: "image"
                            selected: studio.style === 8
                            quiet: studio.style !== 8
                            implicitHeight: 35
                            hint: "Original pixels, with your edits. No border."
                            onClicked: {
                                studio.style = 8;
                                root.editing = false;
                            }
                        }
                        Rectangle {
                            Layout.leftMargin: 22
                            Layout.rightMargin: 22
                            Layout.fillWidth: true
                            height: 1
                            color: theme.separator
                        }
                        ColumnLayout {
                            Layout.leftMargin: 22
                            Layout.rightMargin: 22
                            Layout.fillWidth: true
                            spacing: 11
                            enabled: studio.style !== 8 && !studio.busy
                            opacity: enabled ? 1 : 0.4
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "Padding"
                                    font.pixelSize: 12
                                    color: theme.text
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: Math.round(studio.padding * 100) + "%"
                                    font.pixelSize: 11
                                    color: theme.selectedText
                                }
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 0.02
                                to: 0.18
                                stepSize: 0.01
                                value: studio.padding
                                implicitHeight: 22
                                onMoved: if (!pressed)
                                    studio.padding = value
                                onPressedChanged: if (!pressed)
                                    studio.padding = value
                                background: Rectangle {
                                    x: parent.leftPadding
                                    y: parent.topPadding + parent.availableHeight / 2 - height / 2
                                    width: parent.availableWidth
                                    height: 3
                                    radius: theme.radius > 0 ? 2 : 0
                                    color: theme.controlBorder
                                    Rectangle {
                                        width: parent.width * (studio.padding - 0.02) / 0.16
                                        height: 3
                                        radius: theme.radius > 0 ? 2 : 0
                                        color: theme.accent
                                    }
                                }
                                handle: Rectangle {
                                    x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                                    y: parent.topPadding + parent.availableHeight / 2 - height / 2
                                    width: 13
                                    height: 13
                                    radius: theme.radius > 0 ? 7 : 0
                                    color: theme.accent
                                    border.width: parent.activeFocus ? 2 : 0
                                    border.color: theme.text
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "Canvas"
                                    font.pixelSize: 12
                                    color: theme.text
                                    Layout.fillWidth: true
                                }
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
                        Text {
                            Layout.leftMargin: 22
                            Layout.rightMargin: 22
                            Layout.fillWidth: true
                            text: "Your last finish is remembered.\nNo account. No uploads."
                            font.pixelSize: 10
                            color: theme.faint
                            lineHeight: 1.6
                            wrapMode: Text.Wrap
                        }
                        Item {
                            Layout.preferredHeight: 8
                        }
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
            function pause() { if (item) item.pause() }
            onLoaded: item.shortcutsAllowed = Qt.binding(function() { return root.shortcutsAllowed })
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 88
            color: theme.alpha(theme.background, 1)
            Rectangle {
                width: parent.width
                height: 1
                color: theme.separator
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
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
                            text: root.currentDirectory.replace(/^\/home\/[^/]+/, "~")
                            color: theme.muted
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: 350
                        }
                        Text {
                            text: "Change"
                            color: theme.selectedText
                            font.pixelSize: 10
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: folderDialog.open()
                            }
                        }
                    }
                }
                StudioButton {
                    visible: root.currentSaved.length > 0
                    text: "Show file"
                    glyph: "folder"
                    quiet: true
                    onClicked: root.videoMode ? video.revealSaved() : studio.revealSaved()
                }
                Text {
                    visible: root.width > 1150 && !root.working && !root.videoMode
                    text: "↵"
                    color: theme.faint
                    font.pixelSize: 18
                }
                StudioButton {
                    text: root.working ? "Working…" : root.videoMode ? "Export clip" : "Copy and save"
                    glyph: root.videoMode ? "check" : "copy"
                    primary: true
                    implicitHeight: 46
                    implicitWidth: 181
                    enabled: !root.working && (root.videoMode ? video.source.toString().length > 0 : !studio.rendering)
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
