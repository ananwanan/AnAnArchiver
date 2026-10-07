pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SplitView {
    id: root
    required property var archiveController
    required property var directoryModel
    signal openRequested()
    orientation: Qt.Horizontal

    Pane {
        SplitView.preferredWidth: 240
        SplitView.minimumWidth: 160
        SplitView.maximumWidth: 420
        padding: 0
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true
                padding: 12
                text: qsTr("Folders")
                font.bold: true
            }
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true
                leftPadding: 12
                rightPadding: 12
                bottomPadding: 12
                visible: root.directoryModel.rootPath.length === 0
                text: qsTr("Choose Browse folder to navigate local files.")
                wrapMode: Text.WordWrap
                opacity: 0.7
            }
            TreeView {
                id: treeView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.directoryModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: TreeViewDelegate {
                    id: treeRow
                    required property string fullPath
                    required property bool isDirectory
                    text: treeRow.model.display
                    implicitWidth: treeView.width
                    implicitHeight: 32
                    PlainToolTip {
                        visible: treeRow.hovered
                        text: treeRow.fullPath
                    }
                    contentItem: Label { textFormat: Text.PlainText;
                        text: treeRow.text
                        elide: Text.ElideRight
                        color: treeRow.highlighted ? treeRow.palette.highlightedText : treeRow.palette.buttonText
                        visible: !treeRow.editing
                    }
                    onDoubleClicked: {
                        if (!isDirectory)
                            root.archiveController.openFile(fullPath)
                    }
                }
            }
        }
    }
    Pane {
        id: archivePane
        SplitView.fillWidth: true
        padding: 0
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                color: palette.alternateBase
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12
                    Label { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Name"); font.bold: true }
                    Label { textFormat: Text.PlainText; Layout.preferredWidth: 100; text: qsTr("Size"); font.bold: true }
                    Label { textFormat: Text.PlainText; Layout.preferredWidth: 100; text: qsTr("Type"); font.bold: true; visible: archivePane.width > 650 }
                    Label { textFormat: Text.PlainText; Layout.preferredWidth: 160; text: qsTr("Modified"); font.bold: true; visible: archivePane.width > 520 }
                }
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
                Keys.onPressed: function(event) {
                    if (!root.archiveController.canExtract)
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
                        root.archiveController.selectRow(currentIndex, true, shift)
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
                    required property string entrySize
                    required property string entryType
                    required property string entryModified
                    required property bool entrySelected
                    width: entries.width
                    height: 34
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
                    }
                    Accessible.name: entryPath
                    PlainToolTip {
                        visible: entryRow.hovered
                        text: entryRow.entryPath
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Label { textFormat: Text.PlainText;
                            Layout.fillWidth: true
                            text: entryRow.entryPath
                            elide: Text.ElideRight
                            color: entryRow.highlighted ? palette.highlightedText : palette.text
                        }
                        Label { textFormat: Text.PlainText; Layout.preferredWidth: 100; text: entryRow.entrySize; color: entryRow.highlighted ? palette.highlightedText : palette.text }
                        Label { textFormat: Text.PlainText; Layout.preferredWidth: 100; text: entryRow.entryType; visible: archivePane.width > 650; color: entryRow.highlighted ? palette.highlightedText : palette.text }
                        Label { textFormat: Text.PlainText; Layout.preferredWidth: 160; text: entryRow.entryModified; visible: archivePane.width > 520; elide: Text.ElideRight; color: entryRow.highlighted ? palette.highlightedText : palette.text }
                    }
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 460)
                    visible: root.archiveController.currentFile.length === 0
                    spacing: 16
                    Label { textFormat: Text.PlainText;
                        Layout.fillWidth: true
                        text: qsTr("Browse your archives")
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 24
                        font.bold: true
                    }
                    Label { textFormat: Text.PlainText;
                        Layout.fillWidth: true
                        text: qsTr("Open a ZIP, 7z, RAR, TAR or another supported archive to view its files.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        opacity: 0.75
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Open archive…")
                        onClicked: root.openRequested()
                    }
                }
                Label { textFormat: Text.PlainText;
                    anchors.centerIn: parent
                    visible: root.archiveController.currentFile.length > 0 && root.archiveController.entryCount === 0 && !root.archiveController.busy
                    text: root.archiveController.errorMessage.length > 0 ? qsTr("Unable to display archive contents") : qsTr("No entries")
                    opacity: 0.7
                }
            }
        }
    }
}


