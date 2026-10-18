pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

SplitView {
    id: root
    required property var archiveController
    required property var directoryModel
    property var dragController: null
    property Action extractAction: null
    property Action exportAction: null
    readonly property bool archiveOpen: root.archiveController.currentFile.length > 0
    signal openRequested()
    orientation: Qt.Horizontal
    function focusSearch() { searchField.forceActiveFocus(); searchField.selectAll() }
    function sortBy(column) {
        const metadata = root.archiveController.entryModel
        if (metadata.sortColumn === column)
            metadata.sortDescending = !metadata.sortDescending
        else {
            metadata.sortDescending = false
            metadata.sortColumn = column
        }
    }
    handle: Rectangle { implicitWidth: 1; implicitHeight: 1; color: root.palette.mid; opacity: 0.25 }
    Pane {
        id: navigation
        SplitView.preferredWidth: 220
        SplitView.minimumWidth: 145
        SplitView.maximumWidth: 380
        padding: 0
        background: Rectangle { color: navigation.palette.window }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 10
                Layout.preferredHeight: 48
                AppIcon { name: "folder"; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: qsTr("Folders"); font.weight: Font.DemiBold; font.pixelSize: 12 }
            }
            Label {
                Layout.fillWidth: true
                leftPadding: 14
                rightPadding: 14
                bottomPadding: 14
                visible: !root.archiveOpen && root.directoryModel.rootPath.length === 0
                text: qsTr("Choose Browse folder to navigate local files.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                opacity: 0.65
                font.pixelSize: 12
            }
            ItemDelegate {
                id: archiveRootButton
                objectName: "archiveRootButton"
                Layout.fillWidth: true
                visible: root.archiveOpen
                text: qsTr("Archive root")
                highlighted: root.archiveController.currentFolder.length === 0
                background: Rectangle {
                    color: archiveRootButton.highlighted ? archiveRootButton.palette.highlight
                        : (archiveRootButton.hovered ? archiveRootButton.palette.alternateBase : "transparent")
                    border.width: archiveRootButton.activeFocus ? 1 : 0
                    border.color: archiveRootButton.palette.highlight
                }
                onClicked: root.archiveController.navigateToFolder("")
                contentItem: RowLayout {
                    spacing: 8
                    AppIcon { name: "archive"; onDarkSurface: archiveRootButton.highlighted || root.palette.window.hslLightness < 0.5; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                    Label {
                        Layout.fillWidth: true
                        text: archiveRootButton.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: archiveRootButton.highlighted ? palette.highlightedText : palette.buttonText
                        font.pixelSize: 13
                    }
                }
                Accessible.name: text
            }
            TreeView {
                id: archiveFolderTree
                objectName: "archiveFolderTree"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.archiveOpen
                clip: true
                model: root.archiveController.folderModel
                selectionModel: ItemSelectionModel { model: root.archiveController.folderModel }
                function activateCurrentFolder() {
                    const item = archiveFolderTree.itemAtCell(Qt.point(0, archiveFolderTree.currentRow)) as ArchiveFolderDelegate
                    if (item) root.archiveController.navigateToFolder(item.folderPath)
                }
                Keys.onReturnPressed: archiveFolderTree.activateCurrentFolder()
                Keys.onEnterPressed: archiveFolderTree.activateCurrentFolder()
                Keys.onSpacePressed: archiveFolderTree.activateCurrentFolder()
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: ArchiveFolderDelegate {}
                component ArchiveFolderDelegate: TreeViewDelegate {
                    id: archiveFolderRow
                    objectName: "archiveFolderRow"
                    required property string folderPath
                    required property string folderName
                    text: folderName
                    implicitWidth: archiveFolderTree.width
                    implicitHeight: 32
                    highlighted: root.archiveController.currentFolder === folderPath
                    onClicked: { root.archiveController.navigateToFolder(folderPath); archiveFolderTree.forceActiveFocus() }
                    Accessible.name: folderPath
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: archiveFolderTree.activeFocus && archiveFolderRow.current ? 1 : 0
                        border.color: archiveFolderRow.palette.highlight
                    }
                    PlainToolTip { visible: archiveFolderRow.hovered; text: archiveFolderRow.folderPath }
                    contentItem: RowLayout {
                        spacing: 8
                        AppIcon { name: "folder"; onDarkSurface: archiveFolderRow.highlighted || root.palette.window.hslLightness < 0.5; Layout.preferredWidth: 17; Layout.preferredHeight: 17 }
                        Label {
                            Layout.fillWidth: true
                            text: archiveFolderRow.folderName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: archiveFolderRow.highlighted ? palette.highlightedText : palette.buttonText
                            font.pixelSize: 13
                        }
                    }
                }
            }
            TreeView {
                id: directoryTree
                objectName: "localDirectoryTree"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !root.archiveOpen
                clip: true
                model: root.directoryModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: TreeViewDelegate {
                    id: treeRow
                    required property string fullPath
                    required property bool isDirectory
                    text: treeRow.model.display
                    implicitWidth: directoryTree.width
                    implicitHeight: 32
                    icon.source: isDirectory ? "../icons/folder.svg" : "../icons/file.svg"
                    icon.color: treeRow.highlighted ? palette.highlightedText : palette.buttonText
                    PlainToolTip { visible: treeRow.hovered; text: treeRow.fullPath }
                    contentItem: Label {
                        textFormat: Text.PlainText
                        text: treeRow.text
                        elide: Text.ElideRight
                        color: treeRow.highlighted ? treeRow.palette.highlightedText : treeRow.palette.buttonText
                        visible: !treeRow.editing
                        font.pixelSize: 13
                    }
                    onDoubleClicked: { if (!isDirectory) root.archiveController.openFile(fullPath) }
                }
            }
        }
    }
    Pane {
        id: archivePane
        SplitView.fillWidth: true
        padding: 0
        background: Rectangle { color: archivePane.palette.base }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 14
                spacing: 8
                visible: root.archiveOpen
                ToolButton {
                    id: archiveUpButton
                    objectName: "archiveUpButton"
                    text: qsTr("Up one folder")
                    enabled: root.archiveController.currentFolder.length > 0
                    onClicked: root.archiveController.navigateUp()
                    contentItem: AppIcon { name: "chevron"; implicitWidth: 18; implicitHeight: 18 }
                    Accessible.name: text
                    PlainToolTip { visible: archiveUpButton.hovered; text: archiveUpButton.text }
                }
                Label {
                    id: currentArchiveFolder
                    objectName: "currentArchiveFolder"
                    Layout.fillWidth: true
                    text: root.archiveController.currentFolder.length > 0
                        ? "/" + root.archiveController.currentFolder : qsTr("Archive root")
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    font.pixelSize: 13
                    Accessible.name: qsTr("Current folder")
                    HoverHandler { id: folderPathHover }
                    PlainToolTip { visible: folderPathHover.hovered; text: currentArchiveFolder.text }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8
                AppIcon { name: "search"; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                TextField {
                    id: searchField
                    objectName: "archiveSearch"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search files and folders in this folder…")
                    text: root.archiveController.entryModel.query
                    enabled: root.archiveController.currentFile.length > 0 && !root.archiveController.busy
                    selectByMouse: true
                    Accessible.name: qsTr("Search current folder")
                    onTextEdited: searchDelay.restart()
                    Keys.onEscapePressed: function(event) {
                        searchDelay.stop()
                        searchField.clear()
                        root.archiveController.entryModel.query = ""
                        entries.forceActiveFocus()
                        event.accepted = true
                    }
                    Timer { id: searchDelay; interval: 180; onTriggered: root.archiveController.entryModel.query = searchField.text }
                }
                BusyIndicator {
                    running: root.archiveController.entryModel.querying
                    visible: running
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                ToolButton {
                    id: clearSearchButton
                    objectName: "clearArchiveSearch"
                    text: qsTr("Clear search")
                    display: AbstractButton.IconOnly
                    icon.source: "../icons/close.svg"
                    icon.color: palette.buttonText
                    visible: searchField.text.length > 0
                    onClicked: { searchDelay.stop(); searchField.clear(); root.archiveController.entryModel.query = ""; searchField.forceActiveFocus() }
                    Accessible.name: text
                    PlainToolTip { visible: clearSearchButton.hovered; text: clearSearchButton.text }
                }
                ToolButton {
                    id: resetSortButton
                    objectName: "resetArchiveSort"
                    text: qsTr("Archive order")
                    visible: root.archiveController.entryModel.sortColumn >= 0
                    enabled: !root.archiveController.busy
                    onClicked: root.archiveController.entryModel.sortColumn = -1
                    PlainToolTip { visible: resetSortButton.hovered; text: qsTr("Restore the original archive order") }
                }
            }
            Rectangle {
                id: headers
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: archivePane.palette.window
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12
                    SortHeader { objectName: "sortName"; label: qsTr("Name"); column: 0; entryModel: root.archiveController.entryModel; Layout.fillWidth: true; enabled: root.archiveController.canExtract; onSortRequested: function(column) { root.sortBy(column) } }
                    SortHeader { objectName: "sortSize"; label: qsTr("Size"); column: 1; entryModel: root.archiveController.entryModel; Layout.preferredWidth: 100; enabled: root.archiveController.canExtract; onSortRequested: function(column) { root.sortBy(column) } }
                    SortHeader { objectName: "sortType"; label: qsTr("Type"); column: 2; entryModel: root.archiveController.entryModel; Layout.preferredWidth: 90; visible: archivePane.width > 650; enabled: root.archiveController.canExtract; onSortRequested: function(column) { root.sortBy(column) } }
                    SortHeader { objectName: "sortModified"; label: qsTr("Modified"); column: 3; entryModel: root.archiveController.entryModel; Layout.preferredWidth: 150; visible: archivePane.width > 520; enabled: root.archiveController.canExtract; onSortRequested: function(column) { root.sortBy(column) } }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: archivePane.palette.mid; opacity: 0.2 }
            }
            ListView {
                id: entries
                objectName: "archiveEntries"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.archiveController.entryModel
                reuseItems: true
                focus: true
                keyNavigationEnabled: false
                Connections {
                    target: root.archiveController.entryModel
                    function onCountChanged() { entries.currentIndex = -1; entries.positionViewAtBeginning() }
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Backspace && root.archiveOpen) {
                        root.archiveController.navigateUp()
                        event.accepted = true
                        return
                    }
                    if (!root.archiveController.canExtract || count === 0)
                        return
                    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                    if (event.matches(StandardKey.SelectAll)) {
                        root.archiveController.selectAll()
                    } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                               || event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                        const next = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1
                            : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                        currentIndex = next
                        positionViewAtIndex(next, ListView.Contain)
                        if (!ctrl || shift)
                            root.archiveController.selectRow(next, ctrl, shift)
                    } else if (event.key === Qt.Key_Space) {
                        root.archiveController.selectRow(Math.max(0, currentIndex), true, shift)
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (!root.archiveController.navigateToRow(Math.max(0, currentIndex)) && root.extractAction)
                            root.extractAction.trigger()
                    } else if (event.key === Qt.Key_Escape) {
                        root.archiveController.clearSelection()
                    } else {
                        return
                    }
                    event.accepted = true
                }
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: entryRow
                    objectName: "archiveEntryRow"
                    required property int index
                    required property string entryPath
                    required property string entryName
                    required property string entrySize
                    required property string entryType
                    required property string entryModified
                    required property bool entrySelected
                    required property bool entryDirectory
                    width: entries.width
                    height: 36
                    leftPadding: 16
                    rightPadding: 16
                    highlighted: entrySelected
                    background: Rectangle {
                        color: entryRow.highlighted ? entryRow.palette.highlight
                            : (entryRow.hovered ? entryRow.palette.alternateBase : "transparent")
                        border.width: entryRow.activeFocus ? 1 : 0
                        border.color: entryRow.palette.highlight
                    }
                    TapHandler {
                        onTapped: function(eventPoint, button) {
                            entries.currentIndex = entryRow.index
                            entries.forceActiveFocus()
                            root.archiveController.selectRow(entryRow.index,
                                (point.modifiers & Qt.ControlModifier) !== 0,
                                (point.modifiers & Qt.ShiftModifier) !== 0)
                        }
                        onDoubleTapped: {
                            if (entryRow.entryDirectory) {
                                root.archiveController.navigateToRow(entryRow.index)
                                entries.forceActiveFocus()
                            }
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            if (!entryRow.entrySelected) root.archiveController.selectRow(entryRow.index)
                            entries.currentIndex = entryRow.index
                            entryMenu.popup()
                        }
                    }
                    DragHandler {
                        id: exportGesture
                        target: null
                        enabled: root.dragController && root.dragController.ready && entryRow.entrySelected
                        onActiveChanged: { if (active) root.dragController.startDrag() }
                    }
                    Accessible.name: entryPath
                    PlainToolTip { visible: entryRow.hovered; text: entryRow.entryPath }
                    contentItem: RowLayout {
                        spacing: 12
                        AppIcon {
                            name: entryRow.entryDirectory ? "folder" : "file"
                            onDarkSurface: entryRow.highlighted || (ApplicationWindow.window
                                && ApplicationWindow.window.palette.window.r + ApplicationWindow.window.palette.window.g
                                    + ApplicationWindow.window.palette.window.b < 1.5)
                            Layout.preferredWidth: 17
                            Layout.preferredHeight: 17
                        }
                        Label {
                            Layout.fillWidth: true
                            text: entryRow.entryName
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: entryRow.highlighted ? palette.highlightedText : palette.text
                            font.pixelSize: 13
                        }
                        Label { textFormat: Text.PlainText; Layout.preferredWidth: 100; text: entryRow.entrySize; color: entryRow.highlighted ? palette.highlightedText : palette.text; font.pixelSize: 12 }
                        Label { textFormat: Text.PlainText; Layout.preferredWidth: 90; text: entryRow.entryType; visible: archivePane.width > 650; color: entryRow.highlighted ? palette.highlightedText : palette.text; font.pixelSize: 12; opacity: entryRow.highlighted ? 1 : 0.7 }
                        Label { textFormat: Text.PlainText; Layout.preferredWidth: 150; text: entryRow.entryModified; visible: archivePane.width > 520; elide: Text.ElideRight; color: entryRow.highlighted ? palette.highlightedText : palette.text; font.pixelSize: 12; opacity: entryRow.highlighted ? 1 : 0.7 }
                    }
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 460)
                    visible: root.archiveController.currentFile.length === 0
                    spacing: 16
                    AppIcon { name: "archive"; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 40; Layout.preferredHeight: 40 }
                    Label { Layout.fillWidth: true; text: qsTr("Browse your archives"); textFormat: Text.PlainText; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 24; font.weight: Font.DemiBold }
                    Label { Layout.fillWidth: true; text: qsTr("Open a ZIP, 7z, RAR, TAR or another supported archive to view its files."); textFormat: Text.PlainText; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; opacity: 0.65 }
                    Button { Layout.alignment: Qt.AlignHCenter; text: qsTr("Open archive…"); onClicked: root.openRequested() }
                }
                Label {
                    anchors.centerIn: parent
                    visible: root.archiveController.currentFile.length > 0 && entries.count === 0 && !root.archiveController.busy && !root.archiveController.entryModel.querying
                    text: root.archiveController.errorMessage.length > 0 ? qsTr("Unable to display archive contents")
                        : root.archiveController.entryModel.query.length > 0 ? qsTr("No matching files or folders") : qsTr("No files or folders in this folder")
                    textFormat: Text.PlainText
                    opacity: 0.65
                }
            }
        }
    }
    Menu {
        id: entryMenu
        MenuItem { action: root.extractAction; enabled: root.extractAction && root.extractAction.enabled }
        MenuItem { action: root.exportAction; enabled: root.exportAction && root.exportAction.enabled; visible: root.exportAction !== null }
        MenuSeparator {}
        MenuItem { text: qsTr("Select all"); enabled: root.archiveController.canExtract; onTriggered: root.archiveController.selectAll() }
        MenuItem { text: qsTr("Clear selection"); enabled: root.archiveController.selectionCount > 0; onTriggered: root.archiveController.clearSelection() }
    }
}
