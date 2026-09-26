import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Window {
    id: setup
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint
    title: "Omaframe recording"
    property bool hotkeyOnly: false
    onVisibleChanged: if(visible) keys.forceActiveFocus()
    onClosing: function(event) { if(visible) {event.accepted=false;recorder.cancel()} }
    Rectangle {anchors.fill: parent; color: theme.scrim}
    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: recorder.cancel()
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(680,setup.width-40)
            height: Math.min(recorder.micAudio ? 620 : 570,setup.height-40)
            radius: theme.radius
            color: theme.alpha(theme.background, 1)
            border.width: 2
            border.color: theme.frame
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        Text {text: "Record your screen"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 20; font.weight: Font.Medium}
                        Text {text: "Choose an area. Record. Trim when you’re done."; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12}
                    }
                    Item {Layout.fillWidth: true}
                    StudioButton {glyph: "close"; quiet: true; hint: "Cancel"; onClicked: recorder.cancel()}
                }
                Rectangle {Layout.fillWidth: true; height: 1; color: theme.separator}
                RowLayout {
                    Layout.fillWidth: true
                    StudioButton {text: "Select area"; glyph: "capture"; enabled: recorder.state!=="loading"; onClicked: recorder.chooseRegion()}
                    Text {text: "or"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12; Layout.leftMargin: 4; Layout.rightMargin: 4}
                    Choice {
                        Layout.fillWidth: true
                        model: recorder.displays
                        displayText: currentIndex>=0 ? currentText : "Display"
                        onActivated: recorder.selectDisplay(currentIndex)
                    }
                    StudioButton {text: "Use display"; enabled: recorder.displays.length>0; onClicked: recorder.selectDisplay(0); visible: recorder.targetLabel==="Select an area or display"}
                }
                Text {text: recorder.targetLabel; color: recorder.targetLabel==="Select an area or display" ? theme.muted : theme.selectedText; font.family: theme.fontFamily; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight}
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    RecordToggle {text: "Desktop audio"; checked: recorder.desktopAudio; onToggled: recorder.desktopAudio=checked; }
                    RecordToggle {text: "Microphone"; checked: recorder.micAudio; onToggled: recorder.micAudio=checked; }
                    Item {Layout.fillWidth: true}
                }
                Choice {
                    Layout.fillWidth: true
                    visible: recorder.micAudio
                    model: recorder.microphones
                    textRole: "label"
                    currentIndex: recorder.microphone
                    onActivated: recorder.microphone=currentIndex
                    displayText: currentIndex<0 ? "Choose an available microphone" : currentText
                }
                RowLayout {
                    Layout.fillWidth: true
                    RecordToggle {text: "Show cursor"; checked: recorder.cursor; onToggled: recorder.cursor=checked; }
                    Item {Layout.fillWidth: true}
                    Text {text: "Countdown"; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12}
                    Choice {model: ["Off","3 seconds","5 seconds"]; currentIndex: recorder.countdown===0?0:recorder.countdown===3?1:2; onActivated: recorder.countdown=[0,3,5][currentIndex]}
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: safeText.implicitHeight+28
                    radius: theme.radius
                    readonly property color tone: recorder.targetLabel==="Select an area or display" ? theme.text : recorder.safeStop ? theme.accent : theme.urgent
                    color: theme.alpha(tone, 0.08)
                    border.width: 1
                    border.color: theme.alpha(tone, 0.35)
                    Text {
                        id: safeText
                        anchors.fill: parent; anchors.margins: 14
                        text: recorder.controlLocation
                        color: recorder.targetLabel==="Select an area or display" ? theme.muted : recorder.safeStop ? theme.text : theme.urgent
                        font.family: theme.fontFamily
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                    }
                }
                RecordToggle {
                    visible: !recorder.safeStop && recorder.canStart
                    text: "Use Alt+Print to stop this recording"
                    checked: setup.hotkeyOnly
                    onToggled: setup.hotkeyOnly=checked
                    
                }
                Text {text: recorder.status; color: recorder.state==="failed" ? theme.urgent : theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; font.family: theme.fontFamily; font.pixelSize: 12}
                Item {Layout.fillHeight: true}
                RowLayout {
                    Layout.fillWidth: true
                    Text {text: "60 fps · MP4 · Local files"; color: theme.faint; font.family: theme.fontFamily; font.pixelSize: 11; Layout.fillWidth: true}
                    StudioButton {
                        text: recorder.targetLabel==="Select an area or display" ? "Select recording area" : recorder.countdown>0 ? "Record in "+recorder.countdown+"s" : "Start recording"
                        primary: true
                        enabled: recorder.state!=="loading" && !recorder.active && (recorder.targetLabel==="Select an area or display" || (recorder.canStart && (recorder.safeStop || setup.hotkeyOnly)))
                        onClicked: recorder.targetLabel==="Select an area or display" ? recorder.chooseRegion() : recorder.start()
                    }
                }
            }
        }
    }
}
