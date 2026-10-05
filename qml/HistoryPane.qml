import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: pane
    spacing: 12
    property bool popupOpen: actions.opened || confirmation.opened
    signal homeRequested()
    function visibleRows() {
        if (!visible || list.count === 0) { history.setVisibleRange(0, -1); return; }
        const first = Math.max(0, list.indexAt(8, list.contentY + 2));
        let last = list.indexAt(8, list.contentY + list.height - 2);
        if (last < 0) last = Math.min(list.count - 1, first + Math.ceil(list.height / 106));
        history.setVisibleRange(first, last);
    }
    function select(index) {
        list.currentIndex = Math.max(0, Math.min(list.count - 1, index));
        list.positionViewAtIndex(list.currentIndex, ListView.Contain);
        list.forceActiveFocus();
    }
    function act(action) { history.action(list.currentIndex, action, history.keyAt(list.currentIndex)); }
    function confirmDelete() {
        if (list.currentIndex < 0) return;
        confirmation.row = list.currentIndex;
        confirmation.key = history.keyAt(list.currentIndex);
        confirmation.isDraft = history.isDraftAt(list.currentIndex);
        confirmation.message = (confirmation.isDraft ? "Delete this editable draft? Saved exports stay in place.\n" : "Move this one saved file to Trash? Drafts and camera files stay in place.\n") + history.pathAt(list.currentIndex);
        confirmation.open();
    }
    onVisibleChanged: {
        if (visible) { history.refresh(); list.forceActiveFocus(); }
        else history.setVisibleRange(0, -1);
    }
    Connections {
        target: history
        function onModelReset() { list.currentIndex = Math.min(Math.max(0, list.currentIndex), list.count - 1); Qt.callLater(pane.visibleRows); }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "History"; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Medium }
        Item { Layout.fillWidth: true }
        StudioButton { text: "Refresh"; quiet: true; enabled: !history.busy; onClicked: history.refresh() }
        StudioButton { text: "Back"; quiet: true; onClicked: pane.homeRequested() }
    }
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: ["All", "Screenshots", "Recordings", "Drafts"]
            StudioButton {
                required property string modelData
                text: modelData
                selected: history.filter === modelData
                quiet: !selected
                implicitHeight: 32
                onClicked: history.filter = modelData
            }
        }
        Item { Layout.fillWidth: true }
        TextField {
            Layout.preferredWidth: Math.max(120, Math.min(280, pane.width / 3))
            placeholderText: "Search filenames"
            Accessible.name: "Search History filenames"
            onTextChanged: history.search = text
            Keys.onDownPressed: pane.select(0)
        }
    }
    Text { visible: history.busy || history.status.length > 0; text: history.busy ? "Looking for saved captures…" : history.status; color: theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
    ListView {
        id: list
        objectName: "historyList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: history
        cacheBuffer: 0
        reuseItems: true
        spacing: 6
        activeFocusOnTab: true
        Accessible.name: "Saved captures and editable drafts"
        keyNavigationEnabled: false
        section.property: "day"
        section.criteria: ViewSection.FullString
        section.delegate: Text {
            required property string section
            width: list.width; height: 32
            text: section; color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }
        onContentYChanged: Qt.callLater(pane.visibleRows)
        onHeightChanged: Qt.callLater(pane.visibleRows)
        Keys.onPressed: event => {
            if (pane.popupOpen) return;
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) pane.select(currentIndex + 1);
            else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) pane.select(currentIndex - 1);
            else if (event.key === Qt.Key_Home) pane.select(0);
            else if (event.key === Qt.Key_End) pane.select(count - 1);
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) pane.act("edit");
            else if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) pane.act("copy");
            else if (event.key === Qt.Key_Delete) pane.confirmDelete();
            else if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) actions.showFor(currentIndex);
            else if (event.key === Qt.Key_Escape) { currentIndex = -1; pane.homeRequested(); }
            else return;
            event.accepted = true;
        }
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: card
            required property int index
            required property string captureName
            required property string kind
            required property string when
            required property string detail
            required property string thumbnail
            required property bool incomplete
            required property bool draft
            required property bool hasDraft
            width: list.width - 14
            height: 100
            radius: theme.radius
            color: list.currentIndex === index ? theme.selectedFill : mouse.containsMouse ? theme.hoverFill : theme.controlFill
            border.width: list.currentIndex === index && list.activeFocus ? 2 : 1
            border.color: list.currentIndex === index ? theme.focusBorder : theme.controlBorder
            Accessible.role: Accessible.ListItem
            Accessible.name: captureName + ", " + kind + ", " + when + (incomplete ? ", Incomplete" : "") + (hasDraft ? ", Editable draft available" : "")
            Accessible.onPressAction: { pane.select(index); pane.act("edit"); }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: event => { pane.select(card.index); if (event.button === Qt.RightButton) actions.showFor(card.index); }
                onDoubleClicked: { pane.select(card.index); pane.act("edit"); }
            }
            RowLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 14
                Rectangle {
                    Layout.preferredWidth: 128; Layout.fillHeight: true; color: theme.well; radius: theme.radius
                    Image { id: preview; anchors.fill: parent; source: card.thumbnail; cache: false; asynchronous: true; fillMode: Image.PreserveAspectFit }
                    Glyph { anchors.centerIn: parent; visible: preview.status !== Image.Ready; name: card.kind === "Screenshot" ? "image" : "record"; width: 24; height: 24; ink: theme.muted }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 4
                    Text { Layout.fillWidth: true; text: card.captureName; elide: Text.ElideMiddle; color: theme.text; font.family: theme.fontFamily; font.pixelSize: 13 }
                    Text { text: card.kind + " · " + card.detail + (card.incomplete ? " · Incomplete" : "") + (card.hasDraft ? " · Draft" : ""); color: theme.muted; font.pixelSize: 11 }
                    Text { text: card.when; color: theme.faint; font.pixelSize: 11 }
                }
                StudioButton { text: card.draft ? "Resume" : "Open"; quiet: true; onClicked: { pane.select(card.index); pane.act("edit"); } }
                StudioButton { text: "Actions"; quiet: true; Accessible.name: "Actions for " + card.captureName; onClicked: { pane.select(card.index); actions.showFor(card.index); } }
            }
        }
        Text {
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 40)
            visible: list.count === 0 && !history.busy
            text: history.search.length > 0 ? "No matching captures." : "Saved captures appear here. Clipboard-only copies are not added."
            color: theme.muted; font.family: theme.fontFamily; font.pixelSize: 15; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
        }
    }
    Menu {
        id: actions
        property int row: -1
        property string key: ""
        property bool draft: false
        property bool hasDraft: false
        function showFor(index) {
            if (index < 0) return;
            row = index; key = history.keyAt(index); draft = history.isDraftAt(index); hasDraft = history.hasDraftAt(index);
            popup(pane, Math.max(0, pane.width - width - 20), 130);
        }
        function act(action) { history.action(row, action, key); }
        MenuItem { text: actions.draft ? "Resume" : "Open in editor"; onTriggered: actions.act("edit") }
        MenuItem { text: "Resume draft"; visible: !actions.draft && actions.hasDraft; height: visible ? implicitHeight : 0; onTriggered: actions.act("resume") }
        MenuItem { text: "Copy again"; enabled: !actions.draft; onTriggered: actions.act("copy") }
        MenuItem { text: "Open externally"; enabled: !actions.draft; onTriggered: actions.act("external") }
        MenuItem { text: "Reveal in folder"; enabled: !actions.draft; onTriggered: actions.act("reveal") }
        MenuItem { text: actions.draft ? "Delete draft…" : "Move to Trash…"; onTriggered: pane.confirmDelete() }
        onClosed: list.forceActiveFocus()
    }
    ConfirmDialog {
        id: confirmation
        property int row: -1
        property string key: ""
        property bool isDraft: false
        title: isDraft ? "Delete draft?" : "Move to Trash?"
        confirmText: isDraft ? "Delete draft" : "Move to Trash"
        onConfirmed: { history.action(row, isDraft ? "delete-draft" : "trash", key); list.forceActiveFocus(); }
        onClosed: list.forceActiveFocus()
    }
}
