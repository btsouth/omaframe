import QtQuick

Canvas {
    id: icon
    property string name: "plus"
    property color ink: theme.text
    implicitWidth: 20
    implicitHeight: 20
    onNameChanged: requestPaint()
    onInkChanged: requestPaint()
    onPaint: {
        let c = getContext("2d");
        c.reset();
        c.scale(width / 24, height / 24);
        c.strokeStyle = ink;
        c.fillStyle = ink;
        c.lineWidth = 1.65;
        c.lineCap = "round";
        c.lineJoin = "round";
        function line(x, y, x2, y2) {
            c.moveTo(x, y);
            c.lineTo(x2, y2);
        }
        c.beginPath();
        if (name === "capture") {
            line(8, 3, 3, 3);
            line(3, 3, 3, 8);
            line(16, 3, 21, 3);
            line(21, 3, 21, 8);
            line(3, 16, 3, 21);
            line(3, 21, 8, 21);
            line(16, 21, 21, 21);
            line(21, 21, 21, 16);
            c.rect(7, 7, 10, 10);
        } else if (name === "image") {
            c.rect(3, 4, 18, 16);
            line(4, 17, 10, 11);
            line(10, 11, 15, 16);
            line(14, 15, 18, 11);
            line(18, 11, 21, 14);
            c.moveTo(17, 8);
            c.arc(16, 8, 1, 0, Math.PI * 2);
        } else if (name === "crop") {
            line(7, 3, 7, 17);
            line(7, 17, 21, 17);
            line(3, 7, 17, 7);
            line(17, 7, 17, 21);
        } else if (name === "cut") {
            c.moveTo(7, 7);
            c.arc(5, 7, 2, 0, Math.PI * 2);
            c.moveTo(7, 17);
            c.arc(5, 17, 2, 0, Math.PI * 2);
            line(7, 8, 20, 21);
            line(7, 16, 20, 3);
        } else if (name === "arrow") {
            line(4, 20, 20, 4);
            line(11, 4, 20, 4);
            line(20, 4, 20, 13);
        } else if (name === "select") {
            c.moveTo(5, 3);
            c.lineTo(5, 20);
            c.lineTo(10, 15);
            c.lineTo(14, 21);
            c.lineTo(17, 19);
            c.lineTo(13, 13);
            c.lineTo(20, 12);
            c.closePath();
        } else if (name === "line") {
            line(4, 20, 20, 4);
            c.rect(2, 18, 4, 4);
            c.rect(18, 2, 4, 4);
        } else if (name === "box") {
            c.rect(4, 5, 16, 14);
        } else if (name === "ellipse") {
            c.save();
            c.translate(12, 12);
            c.scale(8, 6);
            c.arc(0, 0, 1, 0, Math.PI * 2);
            c.restore();
        } else if (name === "highlight") {
            line(6, 16, 15, 3);
            line(15, 3, 21, 7);
            line(21, 7, 12, 20);
            line(12, 20, 6, 16);
            line(5, 18, 10, 22);
            line(3, 22, 21, 22);
        } else if (name === "redact") {
            c.fillRect(3, 7, 18, 10);
            line(3, 3, 8, 3);
            line(3, 3, 3, 5);
            line(21, 21, 16, 21);
            line(21, 21, 21, 19);
        } else if (name === "blur") {
            c.arc(12, 12, 3, 0, Math.PI * 2);
            c.moveTo(12, 3); c.arc(12, 12, 9, -Math.PI / 2, Math.PI / 2);
            c.moveTo(12, 5); c.arc(12, 12, 7, Math.PI / 2, Math.PI * 1.5);
        } else if (name === "pen") {
            line(5, 19, 17, 5);
            line(17, 5, 20, 8);
            line(20, 8, 8, 21);
            line(8, 21, 4, 21);
            line(4, 21, 5, 19);
        } else if (name === "text") {
            line(4, 4, 20, 4);
            line(12, 4, 12, 20);
            line(8, 20, 16, 20);
        } else if (name === "step") {
            c.arc(12, 12, 9, 0, Math.PI * 2);
            line(10, 9, 12, 7);
            line(12, 7, 12, 17);
            line(9, 17, 15, 17);
        } else if (name === "undo") {
            line(4, 5, 4, 11);
            line(4, 11, 10, 11);
            c.moveTo(4, 11);
            c.bezierCurveTo(10, 1, 24, 9, 18, 18);
        } else if (name === "redo") {
            line(20, 5, 20, 11);
            line(20, 11, 14, 11);
            c.moveTo(20, 11);
            c.bezierCurveTo(14, 1, 0, 9, 6, 18);
        } else if (name === "copy") {
            c.rect(8, 8, 12, 13);
            line(15, 4, 4, 4);
            line(4, 4, 4, 16);
        } else if (name === "pin") {
            line(8.5, 3, 15.5, 3);
            line(10, 3, 10, 9);
            line(14, 3, 14, 9);
            c.moveTo(10, 9);
            c.lineTo(6, 14);
            c.lineTo(18, 14);
            c.lineTo(14, 9);
            line(12, 14, 12, 21);
        } else if (name === "check") {
            line(5, 12, 10, 17);
            line(10, 17, 20, 6);
        } else if (name === "chevron") {
            line(7, 10, 12, 15);
            line(12, 15, 17, 10);
        } else if (name === "folder") {
            c.moveTo(3, 20);
            c.lineTo(3, 5);
            c.lineTo(9, 5);
            c.lineTo(11, 8);
            c.lineTo(21, 8);
            c.lineTo(21, 20);
            c.closePath();
        } else if (name === "spark") {
            c.moveTo(12, 2);
            c.lineTo(15, 9);
            c.lineTo(22, 12);
            c.lineTo(15, 15);
            c.lineTo(12, 22);
            c.lineTo(9, 15);
            c.lineTo(2, 12);
            c.lineTo(9, 9);
            c.closePath();
        } else if (name === "record") {
            c.arc(12, 12, 8.5, 0, Math.PI * 2);
            c.stroke();
            c.beginPath();
            c.arc(12, 12, 4, 0, Math.PI * 2);
            c.fill();
        } else if (name === "play") {
            c.moveTo(8, 5);
            c.lineTo(19, 12);
            c.lineTo(8, 19);
            c.closePath();
            c.fill();
        } else if (name === "pause") {
            c.rect(7, 5, 3, 14);
            c.rect(14, 5, 3, 14);
            c.fill();
        } else if (name === "volume" || name === "mute") {
            c.moveTo(3, 9);
            c.lineTo(7, 9);
            c.lineTo(12, 5);
            c.lineTo(12, 19);
            c.lineTo(7, 15);
            c.lineTo(3, 15);
            c.closePath();
            if (name === "mute") {
                line(16, 9, 21, 15);
                line(21, 9, 16, 15);
            } else {
                c.moveTo(15.5, 9);
                c.quadraticCurveTo(17.5, 12, 15.5, 15);
                c.moveTo(18, 6.5);
                c.quadraticCurveTo(22, 12, 18, 17.5);
            }
        } else if (name === "back") {
            line(20, 12, 4, 12);
            line(4, 12, 10, 6);
            line(4, 12, 10, 18);
        } else if (name === "close") {
            line(6, 6, 18, 18);
            line(18, 6, 6, 18);
        } else if (name === "display") {
            c.rect(3, 4, 18, 12);
            line(12, 16, 12, 20);
            line(8, 20, 16, 20);
        } else if (name === "mic") {
            c.moveTo(9, 6);
            c.arc(12, 6, 3, Math.PI, 0);
            c.lineTo(15, 12);
            c.arc(12, 12, 3, 0, Math.PI);
            c.closePath();
            c.moveTo(6, 12);
            c.quadraticCurveTo(6, 17.5, 12, 17.5);
            c.quadraticCurveTo(18, 17.5, 18, 12);
            line(12, 17.5, 12, 21);
        } else if (name === "settings") {
            c.moveTo(15, 12);
            c.arc(12, 12, 3, 0, Math.PI * 2);
            for (let i = 0; i < 8; ++i) {
                const a = i * Math.PI / 4;
                line(12 + Math.cos(a) * 6, 12 + Math.sin(a) * 6, 12 + Math.cos(a) * 9, 12 + Math.sin(a) * 9);
            }
            c.moveTo(18, 12);
            c.arc(12, 12, 6, 0, Math.PI * 2);
        } else if (name === "timer") {
            c.moveTo(20, 13);
            c.arc(12, 13, 8, 0, Math.PI * 2);
            line(12, 13, 12, 9);
            line(10, 2, 14, 2);
        } else if (name === "trash") {
            line(4, 6, 20, 6);
            line(9, 6, 9, 3);
            line(9, 3, 15, 3);
            line(15, 3, 15, 6);
            c.moveTo(6, 6);
            c.lineTo(7, 21);
            c.lineTo(17, 21);
            c.lineTo(18, 6);
        } else if (name === "keyboard") {
            c.rect(2, 6, 20, 12);
            line(6, 10, 7, 10);
            line(11, 10, 13, 10);
            line(17, 10, 18, 10);
            line(7, 14, 17, 14);
        } else if (name === "cursor") {
            c.moveTo(6, 3);
            c.lineTo(18, 13);
            c.lineTo(12, 14);
            c.lineTo(9, 20);
            c.closePath();
        } else {
            line(12, 4, 12, 20);
            line(4, 12, 20, 12);
        }
        c.stroke();
    }
}
