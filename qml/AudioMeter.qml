import QtQuick
import QtQuick.Layouts

Item {
    id: meter
    property var channel: ({state: "Off", device: "", db: -60, held: -60, clip: false})
    property string label: "Microphone"
    property bool detailed: false
    property bool tiny: false
    readonly property string status: channel.clip ? "Clip risk"
        : channel.state === "Quiet" ? "Quiet"
        : channel.state === "Level" ? Math.round(channel.db) + " dB"
        : channel.state
    implicitWidth: tiny ? 64 : 210
    implicitHeight: detailed ? (channel.state === "Off" ? 36 : 64) : 20
    readonly property bool clipRisk: channel.clip
    onClipRiskChanged: if (clipRisk) Accessible.announce(label + " clip risk", Accessible.Polite)
    Accessible.role: Accessible.ProgressBar
    Accessible.name: label + " level" + (channel.device ? ", " + channel.device : "")
    Accessible.description: status + (channel.clip ? ", " + Math.round(channel.db) + " dBFS" : "") + (channel.state === "Quiet" && label === "Computer sound" ? ". No sound on this output" : "")
    ColumnLayout {
        anchors.fill: parent
        spacing: 3
        Text {
            visible: meter.detailed
            Layout.fillWidth: true
            text: meter.label + (meter.channel.device ? " · " + meter.channel.device : "")
            elide: Text.ElideRight
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Text {
                visible: !meter.tiny && !meter.detailed
                text: meter.label === "Microphone" ? "Mic" : "Sound"
                color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 11
                Layout.preferredWidth: 36
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.minimumWidth: 12
                height: 8
                radius: theme.radius > 0 ? 3 : 0
                color: theme.well
                border.width: 1; border.color: theme.controlBorder
                Rectangle {
                    x: 1; y: 1; height: 6
                    width: Math.max(0, (parent.width - 2) * (meter.channel.db + 60) / 60)
                    radius: parent.radius
                    color: meter.channel.clip ? theme.urgent : theme.accent
                    visible: meter.channel.state === "Level"
                }
                Rectangle {
                    x: Math.min(parent.width - 2, Math.max(1, (parent.width - 2) * (meter.channel.held + 60) / 60))
                    y: 1; width: 1; height: 6
                    color: meter.channel.clip ? theme.urgent : theme.text
                    visible: meter.channel.state === "Level"
                }
            }
            Text {
                text: meter.status
                Layout.preferredWidth: meter.tiny ? 0 : meter.detailed ? 82 : 70
                visible: !meter.tiny
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                color: meter.channel.clip ? theme.urgent : theme.muted
                font.family: theme.fontFamily; font.pixelSize: 11
            }
        }
        Text {
            visible: meter.tiny
            Layout.fillWidth: true
            text: meter.status
            elide: Text.ElideRight
            color: meter.channel.clip ? theme.urgent : theme.muted
            font.family: theme.fontFamily; font.pixelSize: 9
        }
        Item {
            visible: meter.detailed && meter.channel.state !== "Off"
            Layout.fillWidth: true
            implicitHeight: 12
            Repeater {
                model: [-60, -30, -12, 0]
                Text {
                    required property int modelData
                    x: Math.max(0, Math.min(parent.width - width, parent.width * (modelData + 60) / 60 - width / 2))
                    text: modelData === 0 ? "0 dBFS" : modelData
                    color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 9
                }
            }
        }
    }
}
