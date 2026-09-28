import QtQuick

// Draws, selects, moves and resizes the marks of a MarkDocument over an image
// shown underneath at the same size. Labels are typed in place.
Item {
    id: editSurface
    property var doc
    property string tool: "select"
    // No new marks while the owner is saving or loading.
    property bool locked: false
    // The image being marked, after any crop, and before it, in pixels.
    property size workingSize
    property size sourceSize
    // The playhead on a video, in seconds. Marks not showing there are not
    // outlined. Negative for a screenshot.
    property real time: -1
    readonly property bool typing: textEditor.active
    readonly property bool dragging: drawArea.pressed
    readonly property bool hovered: drawArea.containsMouse
    signal toolRequested(string key)
    // A click with the select tool that hit no mark.
    signal emptyClicked(bool hadSelection)
    function showing(mark) {
        return time < 0 || mark.start === undefined || (time >= mark.start && time < mark.end);
    }
    function commitText() { textEditor.commit(); }
    function cancelText() { textEditor.cancel(); }
    function editSelectedText() { textEditor.editSelected(); }
    // Drops the drag in progress without applying it.
    function cancelDrag() {
        drawArea.interaction = "none";
        guide.requestPaint();
    }
    MouseArea {
        id: drawArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !editSurface.locked
        cursorShape: hoverHandle >= 0 ? Qt.SizeFDiagCursor
            : hoverMark.type !== undefined && editSurface.tool !== "crop" ? Qt.SizeAllCursor
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
        property bool pressedWithSelection: false
        readonly property bool moved: Math.hypot(endX - startX, endY - startY) > 3
        readonly property bool selectionShown: editSurface.tool !== "crop" && !textEditor.active && editSurface.showing(editSurface.doc.selectedAnnotation)
        function handleAt(px, py) {
            const selected = editSurface.doc.selectedAnnotation;
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
                interaction = "resize";
                guide.requestPaint();
                return;
            }
            if (editSurface.tool === "crop") {
                interaction = "draw";
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
                guide.requestPaint();
                return;
            }
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
        onPositionChanged: function (mouse) {
            if (!pressed) {
                hoverHandle = handleAt(mouse.x, mouse.y);
                hoverMark = editSurface.tool === "crop" || hoverHandle >= 0 ? ({}) : editSurface.doc.hitAt(mouse.x / width, mouse.y / height, editSurface.tool !== "select");
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
                editSurface.doc.resizeSelected(handle, endX / width, endY / height);
            else if (interaction === "move") {
                if (moved)
                    editSurface.doc.moveSelected((endX - startX) / width, (endY - startY) / height);
                else if (pressedType === "text" && editSurface.tool === "text")
                    textEditor.editSelected();
            } else if (interaction === "stroke") {
                strokePoints = strokePoints.concat([{ x: endX / width, y: endY / height }]);
                editSurface.doc.addStroke(strokePoints);
                strokePoints = [];
            } else if (interaction === "newText")
                textEditor.create(startX / width, startY / height);
            else if (interaction === "draw")
                editSurface.doc.edit(editSurface.tool, startX / width, startY / height, endX / width, endY / height);
            else if (pressedEmpty && !moved)
                editSurface.emptyClicked(pressedWithSelection);
            pressedEmpty = false;
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
                    const mark = editSurface.doc.selectedAnnotation;
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
    Rectangle {
        visible: editSurface.tool === "crop" && editSurface.doc.hasCrop
        x: editSurface.doc.cropBounds.x * parent.width
        y: editSurface.doc.cropBounds.y * parent.height
        width: editSurface.doc.cropBounds.width * parent.width
        height: editSurface.doc.cropBounds.height * parent.height
        color: "transparent"
        border.width: 2
        border.color: theme.accent
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
        readonly property var mark: editSurface.doc.selectedAnnotation
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
        readonly property real viewScale: editSurface.width / Math.max(1, editSurface.workingSize.width)
        readonly property int fontPx: creating ? editSurface.doc.newTextPixels : (mark.fontPx || editSurface.doc.newTextPixels)
        readonly property real inset: Math.max(4, fontPx * 0.27) * viewScale
        readonly property bool boxStyle: creating || mark.textStyle !== "shadow"
        readonly property color ink: creating ? "#ffffff" : (mark.color || "#ffffff")
        readonly property color fill: creating ? "#151a20" : (mark.background || "#151a20")
        readonly property real fillOpacity: creating || mark.backgroundOpacity === undefined ? 1 : mark.backgroundOpacity
        readonly property real maxLine: Math.max(60, editSurface.sourceSize.width * 0.85 * viewScale - inset * 2)
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
            const m = editSurface.doc.selectedAnnotation;
            if (m.type !== "text")
                return;
            creating = false;
            mark = m;
            anchorX = m.boundX;
            anchorY = m.boundY;
            field.text = m.text;
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
