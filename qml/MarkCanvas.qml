import QtQuick

// Draws, selects, moves and resizes the marks of a MarkDocument over an image
// shown underneath at the same size. Labels are typed in place.
Item {
    id: editSurface
    property var doc
    property string tool: "select"
    // Screenshot crops keep a new area of the current view. Video crops
    // continue to edit their existing frame on the original video.
    property bool cropCurrentView: false
    // No new marks while the owner is saving or loading.
    property bool locked: false
    // The image being marked, after any crop, and before it, in pixels.
    property size workingSize
    property size sourceSize
    // The playhead on a video, in seconds. Marks not showing there are not
    // outlined. Negative for a screenshot.
    property real time: -1
    // The length of the video, so a mark that runs to its end still shows on
    // the last frame.
    property real duration: 0
    readonly property bool typing: textEditor.active
    readonly property bool dragging: drawArea.pressed
    readonly property bool hovered: drawArea.containsMouse
    readonly property int cursorShape: drawArea.cursorShape
    // The viewport can pan on empty space without taking a mark's drag or
    // resize handle. Coordinates are local to this canvas.
    function canPanAt(x, y) {
        if (typing) return false;
        if (x < 0 || y < 0 || x > width || y > height) return true;
        return drawArea.handleAt(x, y) < 0 && doc.hitAt(x / width, y / height, false).index === undefined;
    }
    function updateHoverAt(x, y) {
        if (x < 0 || y < 0 || x > width || y > height) {
            drawArea.hoverMark = ({});
            drawArea.hoverHandle = -1;
        } else drawArea.updateHover(x, y);
    }
    signal toolRequested(string key)
    // A click with the select tool that hit no mark.
    signal emptyClicked(bool hadSelection)
    function showing(mark) {
        return time < 0 || mark.start === undefined || (time >= mark.start && (time < mark.end || mark.end >= duration));
    }
    function commitText() { textEditor.commit(); }
    function cancelText() { textEditor.cancel(); }
    function editSelectedText() { textEditor.editSelected(); }
    // Drops the drag in progress without applying it.
    function cancelDrag() {
        transformTimer.stop();
        if (editSurface.doc) editSurface.doc.endTransform(false);
        drawArea.interaction = "none";
        guide.requestPaint();
    }
    onLockedChanged: if (locked) cancelDrag()
    onToolChanged: { cancelDrag(); if (tool === "text" && doc) doc.refreshLabelStyle(); }
    onVisibleChanged: if (!visible) cancelDrag()
    Connections {
        target: editSurface.doc
        function onChanged() {
            // Undo, delete and crop can remove the mark under a still cursor.
            drawArea.hoverMark = ({});
            drawArea.hoverHandle = -1;
        }
    }
    MouseArea {
        id: drawArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !editSurface.locked
        readonly property int cursorHandle: pressed && interaction === "resize" ? handle : hoverHandle
        cursorShape: pressed && (interaction === "move" || interaction === "cropMove") ? Qt.SizeAllCursor : cursorHandle >= 0 ? (transformMark.type === "line" || transformMark.type === "arrow" ? Qt.CrossCursor : cursorHandle === 4 || cursorHandle === 6 ? Qt.SizeVerCursor : cursorHandle === 5 || cursorHandle === 7 ? Qt.SizeHorCursor : cursorHandle === 1 || cursorHandle === 3 ? Qt.SizeBDiagCursor : Qt.SizeFDiagCursor)
            : hoverMark.type !== undefined ? Qt.SizeAllCursor
            : editSurface.tool === "select" ? Qt.ArrowCursor
            : editSurface.tool === "text" ? Qt.IBeamCursor : Qt.CrossCursor
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
        property bool pressedEmpty: false
        // A filled mark under a drawing tool's press: a click selects it, a drag draws.
        property int pressedInside: -1
        property bool pressedWithSelection: false
        readonly property bool moved: Math.hypot(endX - startX, endY - startY) > 3
        property var initialMark: ({})
        readonly property var transformMark: editSurface.tool === "crop"
            ? !editSurface.cropCurrentView && editSurface.doc.hasCrop ? ({ type: "crop", boundX: editSurface.doc.cropBounds.x, boundY: editSurface.doc.cropBounds.y,
                 boundW: editSurface.doc.cropBounds.width, boundH: editSurface.doc.cropBounds.height }) : ({})
            : editSurface.doc.selectedAnnotation
        readonly property bool selectionShown: !textEditor.active && editSurface.showing(transformMark)
        function handles(mark) {
            if (!mark.type) return [];
            if (mark.type === "line" || mark.type === "arrow")
                return [{ id: 0, x: mark.x1 * width, y: mark.y1 * height },
                        { id: 1, x: mark.x2 * width, y: mark.y2 * height }];
            const l = mark.boundX * width, t = mark.boundY * height;
            const r = l + mark.boundW * width, b = t + mark.boundH * height;
            const points = [[l,t], [r,t], [r,b], [l,b], [(l+r)/2,t], [r,(t+b)/2], [(l+r)/2,b], [l,(t+b)/2]];
            const compact = r-l < 48 || b-t < 28;
            return points.map((point, id) => ({id: id, x: point[0], y: point[1]}))
                .filter(point => !compact || point.id === 2 || (point.id >= 4 &&
                    (point.id % 2 === 0 ? r-l >= 32 : b-t >= 16)));
        }
        function updatePointer(mouse) {
            endX = Math.max(0, Math.min(width, mouse.x));
            endY = Math.max(0, Math.min(height, mouse.y));
            if ((interaction === "move" || interaction === "cropMove") && (mouse.modifiers & Qt.ShiftModifier)) {
                if (Math.abs(endX-startX) >= Math.abs(endY-startY)) endY = startY;
                else endX = startX;
            }
        }
        function cropRect() {
            let l = initialMark.boundX * width, t = initialMark.boundY * height;
            let r = l + initialMark.boundW * width, b = t + initialMark.boundH * height;
            if (interaction === "cropMove") {
                const dx = Math.max(-l, Math.min(width-r, endX-startX));
                const dy = Math.max(-t, Math.min(height-b, endY-startY));
                l += dx; r += dx; t += dy; b += dy;
            } else {
                if (handle === 0 || handle === 3 || handle === 7) l = Math.min(endX, r-4);
                if (handle === 1 || handle === 2 || handle === 5) r = Math.max(endX, l+4);
                if (handle === 0 || handle === 1 || handle === 4) t = Math.min(endY, b-4);
                if (handle === 2 || handle === 3 || handle === 6) b = Math.max(endY, t+4);
            }
            return Qt.rect(l, t, r-l, b-t);
        }
        function previewTransform() {
            if (interaction === "resize" && initialMark.type !== "crop")
                editSurface.doc.previewTransform(handle, endX / width, endY / height);
            else if (interaction === "move")
                editSurface.doc.previewTransform(-1, moved ? (endX-startX) / width : 0, moved ? (endY-startY) / height : 0);
        }
        function handleAt(px, py) {
            if (!selectionShown) return -1;
            let closest = -1, distance = 12;
            for (const point of handles(transformMark)) {
                const d = Math.hypot(px-point.x, py-point.y);
                if (d < distance) { closest = point.id; distance = d; }
            }
            return closest;
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
            pressedEmpty = false;
            pressedWithSelection = editSurface.doc.selectedAnnotation.type !== undefined;
            hoverMark = ({});
            hoverHandle = -1;
            startX = endX = mouse.x;
            startY = endY = mouse.y;
            const nx = mouse.x / width, ny = mouse.y / height;
            if (mouse.button === Qt.RightButton) {
                interaction = "none";
                if (editSurface.tool !== "crop") {
                    editSurface.toolRequested("select");
                    editSurface.doc.selectAt(nx, ny);
                }
                return;
            }
            handle = handleAt(mouse.x, mouse.y);
            if (handle >= 0) {
                initialMark = transformMark;
                interaction = "resize";
                if (initialMark.type !== "crop") editSurface.doc.beginTransform();
                guide.requestPaint();
                return;
            }
            if (editSurface.tool === "crop") {
                initialMark = transformMark;
                const crop = editSurface.doc.cropBounds;
                interaction = !editSurface.cropCurrentView && editSurface.doc.hasCrop && nx > crop.x && nx < crop.x+crop.width && ny > crop.y && ny < crop.y+crop.height ? "cropMove" : "draw";
                guide.requestPaint();
                return;
            }
            // Existing marks stay editable with any tool. Drawing tools
            // pick up filled areas only by their edge, so a new mark can
            // still start inside one.
            const hit = editSurface.doc.hitAt(nx, ny, editSurface.tool !== "select");
            if (hit.index !== undefined) {
                editSurface.doc.select(hit.index);
                pressedType = hit.type;
                interaction = "move";
                editSurface.doc.beginTransform();
                guide.requestPaint();
                return;
            }
            const inside = editSurface.doc.hitAt(nx, ny, false);
            pressedInside = inside.index !== undefined ? inside.index : -1;
            editSurface.doc.clearSelection();
            if (editSurface.tool === "select") {
                interaction = "none";
                pressedEmpty = true;
            }
            else if (editSurface.tool === "text")
                interaction = "newText";
            else if (editSurface.tool === "pen") {
                strokePoints = [{ x: nx, y: ny }];
                interaction = "stroke";
            } else
                interaction = "draw";
            guide.requestPaint();
        }
        function updateHover(x, y) {
            hoverHandle = handleAt(x, y);
            if (hoverHandle >= 0) hoverMark = ({});
            else if (editSurface.tool === "crop") {
                const crop = editSurface.doc.cropBounds;
                hoverMark = !editSurface.cropCurrentView && editSurface.doc.hasCrop && x/width > crop.x && x/width < crop.x+crop.width && y/height > crop.y && y/height < crop.y+crop.height ? ({type: "crop"}) : ({});
            } else hoverMark = editSurface.doc.hitAt(x / width, y / height, editSurface.tool !== "select");
        }
        onPositionChanged: function (mouse) {
            if (!pressed) {
                updateHover(mouse.x, mouse.y);
                return;
            }
            updatePointer(mouse);
            if ((interaction === "resize" || interaction === "move") && !transformTimer.running) transformTimer.start();
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
            updatePointer(mouse);
            transformTimer.stop();
            if (interaction === "cropMove" || (interaction === "resize" && initialMark.type === "crop")) {
                const box = cropRect();
                editSurface.doc.edit("crop", box.x/width, box.y/height, (box.x+box.width)/width, (box.y+box.height)/height);
            } else if (interaction === "resize" || interaction === "move") {
                previewTransform();
                editSurface.doc.endTransform(true);
                if (interaction === "move" && !moved && pressedType === "text" && editSurface.tool === "text")
                    textEditor.editSelected();
            } else if (interaction === "stroke") {
                strokePoints = strokePoints.concat([{ x: endX / width, y: endY / height }]);
                editSurface.doc.addStroke(strokePoints);
                strokePoints = [];
            } else if (interaction === "newText")
                textEditor.create(startX / width, startY / height);
            // Step places its number with a click, so it never selects instead.
            else if (interaction === "draw" && pressedInside >= 0 && !moved && editSurface.tool !== "step")
                editSurface.doc.select(pressedInside);
            else if (interaction === "draw") {
                if (editSurface.tool === "crop" && editSurface.cropCurrentView) {
                    if (editSurface.doc.cropCurrentView(startX / width, startY / height, endX / width, endY / height))
                        editSurface.toolRequested("select");
                } else editSurface.doc.edit(editSurface.tool, startX / width, startY / height, endX / width, endY / height);
            }
            else if (pressedEmpty && !moved)
                editSurface.emptyClicked(pressedWithSelection);
            pressedEmpty = false;
            pressedInside = -1;
            interaction = "none";
            hoverMark = ({});
            hoverHandle = handleAt(endX, endY);
            guide.requestPaint();
        }
        onDoubleClicked: function (mouse) {
            const hit = editSurface.doc.hitAt(mouse.x / width, mouse.y / height);
            if (hit.type === "text") {
                interaction = "none";
                editSurface.doc.select(hit.index);
                textEditor.editSelected();
            }
        }
        onCanceled: { editSurface.cancelDrag(); strokePoints = []; }
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
                } else if (drawArea.interaction === "cropMove" || (drawArea.interaction === "resize" && drawArea.initialMark.type === "crop")) {
                    const box = drawArea.cropRect();
                    c.strokeRect(box.x, box.y, box.width, box.height);
                } else if (drawArea.interaction === "resize") {
                    return; // The actual mark previews the resize, with its real text layout.
                } else if (drawArea.interaction === "move" || drawArea.interaction === "newText") {
                    return;
                } else if (editSurface.tool === "arrow" || editSurface.tool === "line") {
                    c.beginPath();
                    c.moveTo(x, y);
                    c.lineTo(x + w, y + h);
                    c.stroke();
                } else if (editSurface.tool === "ellipse") {
                    if (Math.abs(w) > 1 && Math.abs(h) > 1) {
                        c.beginPath();
                        c.save();
                        c.translate(x + w / 2, y + h / 2);
                        c.scale(Math.abs(w) / 2, Math.abs(h) / 2);
                        c.arc(0, 0, 1, 0, Math.PI * 2);
                        c.restore();
                        c.stroke();
                    }
                } else if (editSurface.tool !== "step") {
                    if (editSurface.tool !== "box") {
                        c.fillStyle = editSurface.tool === "redact" ? theme.alpha(theme.urgent, 0.22) : theme.alpha(theme.accent, 0.18);
                        c.fillRect(x, y, w, h);
                    }
                    c.strokeRect(x, y, w, h);
                }
            }
        }
    }
    Timer {
        id: transformTimer
        interval: 40
        onTriggered: drawArea.previewTransform()
    }
    // Hover: a quiet dashed outline says "this can be picked up".
    Canvas {
        id: hoverOutline
        readonly property var mark: drawArea.hoverMark
        readonly property bool shown: mark.type !== undefined && !drawArea.pressed && drawArea.selectionShown && mark.index !== undefined && (editSurface.doc.selectedAnnotation.type === undefined || mark.x !== editSurface.doc.selectedAnnotation.boundX || mark.y !== editSurface.doc.selectedAnnotation.boundY)
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
        readonly property var mark: drawArea.transformMark
        visible: drawArea.selectionShown && mark.type !== undefined && drawArea.interaction !== "cropMove" && !(drawArea.interaction === "resize" && mark.type === "crop")
        x: (mark.boundX || 0) * parent.width
        y: (mark.boundY || 0) * parent.height
        width: Math.max(1, (mark.boundW || 0) * parent.width)
        height: Math.max(1, (mark.boundH || 0) * parent.height)
        Rectangle {
            visible: selectedOutline.mark.type !== "line" && selectedOutline.mark.type !== "arrow"
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.width: 2
            border.color: theme.accent
        }
        Repeater {
            model: drawArea.handles(selectedOutline.mark)
            Rectangle {
                required property var modelData
                width: modelData.id >= 4 ? 10 : 12
                height: width
                radius: modelData.id >= 4 ? 2 : width / 2
                color: theme.background
                border.width: 2
                border.color: theme.accent
                x: modelData.x - selectedOutline.x - width / 2
                y: modelData.y - selectedOutline.y - height / 2
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
        readonly property real viewScale: editSurface.width / Math.max(1, editSurface.workingSize.width)
        readonly property var defaults: editSurface.doc.labelDefaults
        readonly property int fontPx: creating ? (defaults.fontPx || editSurface.doc.newTextPixels) : (mark.fontPx || editSurface.doc.newTextPixels)
        readonly property real inset: Math.max(4, fontPx * 0.27) * viewScale
        readonly property bool boxStyle: creating ? defaults.textStyle !== "shadow" : mark.textStyle !== "shadow"
        readonly property color ink: creating ? (defaults.color || "#ffffff") : (mark.color || "#ffffff")
        readonly property color fill: creating ? (defaults.background || "#151a20") : (mark.background || "#151a20")
        readonly property real fillOpacity: creating ? (defaults.backgroundOpacity ?? 1) : (mark.backgroundOpacity ?? 1)
        readonly property string textAlign: creating ? (defaults.textAlign || "center") : (mark.textAlign || "center")
        readonly property real maxLine: Math.max(1, Math.min(editSurface.width, !creating && mark.textBoxWidth > 0 ? mark.textBoxWidth * editSurface.width : editSurface.width * 0.85) - inset * 2)
        visible: active
        z: 30
        x: Math.max(0, Math.min(anchorX * editSurface.width, editSurface.width - width))
        y: Math.max(0, Math.min(anchorY * editSurface.height, editSurface.height - height))
        width: box.width
        height: box.height
        function create(nx, ny) {
            creating = true;
            mark = ({});
            anchorX = nx;
            anchorY = ny;
            field.text = "";
            textScroll.contentY = 0;
            active = true;
            field.forceActiveFocus();
        }
        function editSelected() {
            const m = editSurface.doc.selectedAnnotation;
            if (m.type !== "text")
                return;
            creating = false;
            mark = m;
            anchorX = m.boundX;
            anchorY = m.boundY;
            field.text = m.text;
            textScroll.contentY = 0;
            editSurface.doc.beginTextEdit();
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
                    editSurface.doc.edit("text", anchorX, anchorY, anchorX, anchorY, text);
            } else
                editSurface.doc.endTextEdit(text, true);
            drawArea.forceActiveFocus();
        }
        function cancel() {
            if (!active)
                return;
            active = false;
            if (!creating)
                editSurface.doc.endTextEdit("", false);
        }
        Rectangle {
            id: box
            width: !textEditor.creating && textEditor.mark.textBoxWidth > 0 ? textEditor.mark.textBoxWidth * editSurface.width : Math.min(textEditor.maxLine, Math.max(measure.contentWidth, placeholder.contentWidth) + 4) + textEditor.inset * 2
            height: Math.min(editSurface.height, Math.max(Math.max(field.contentHeight, measure.contentHeight) + textEditor.inset * 2, (textEditor.mark.textBoxHeight || 0) * editSurface.height))
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
            Flickable {
                id: textScroll
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: field.height
                interactive: false
                function revealCursor() {
                    const caret = field.cursorRectangle;
                    if (caret.y < contentY)
                        contentY = Math.max(0, caret.y);
                    else if (caret.y + caret.height > contentY + height)
                        contentY = Math.max(0, Math.min(contentHeight - height, caret.y + caret.height - height));
                }
                TextEdit {
                    id: field
                    width: textScroll.width
                    height: Math.max(textScroll.height, contentHeight)
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
                    verticalAlignment: TextEdit.AlignVCenter
                    wrapMode: TextEdit.Wrap
                    horizontalAlignment: textEditor.textAlign === "center" || !textEditor.textAlign ? TextEdit.AlignHCenter : textEditor.textAlign === "right" ? TextEdit.AlignRight : TextEdit.AlignLeft
                    selectByMouse: true
                    Accessible.name: "Label text"
                    onTextChanged: if (length > 240) remove(240, length)
                    onCursorRectangleChanged: textScroll.revealCursor()
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
            visible: textEditor.y >= height + 8 || textEditor.y + box.height + height + 8 <= editSurface.height
            x: Math.max(-textEditor.x, Math.min(0, editSurface.width - textEditor.x - width))
            y: textEditor.y + box.height + height + 8 > editSurface.height ? -height - 8 : box.height + 8
            width: Math.min(hintText.implicitWidth + 16, editSurface.width)
            height: hintText.implicitHeight + 12
            radius: theme.radius
            color: theme.alpha(theme.background, 0.94)
            border.width: 1
            border.color: theme.controlBorder
            Text {
                id: hintText
                anchors.fill: parent
                anchors.margins: 6
                text: (field.length >= 220 ? field.length + "/240 · " : "") + "Enter: new line · Esc: finish"
                color: theme.muted
                font.family: theme.fontFamily
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
