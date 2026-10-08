pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    required property var compressionController
    property var dragController: null
    readonly property bool showingExport: root.dragController && (root.dragController.busy || root.dragController.ready)
    readonly property bool showingCompression: root.compressionController.hasTask
    readonly property var operation: showingCompression ? root.compressionController : root.archiveController
    implicitHeight: (root.showingCompression || root.archiveController.extracting ? 76 : 38) + (root.showingExport ? 44 : 0)
    background: Rectangle { color: root.palette.window; Rectangle { width: parent.width; height: 1; color: root.palette.mid; opacity: 0.2 } }
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            visible: root.showingExport
            BusyIndicator { running: root.dragController && root.dragController.busy; visible: running; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
            AppIcon { name: "export"; visible: root.dragController && root.dragController.ready }
            Label {
                Layout.fillWidth: true
                text: root.dragController ? root.dragController.status : ""
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: root.palette.highlight
            }
            Label { visible: root.dragController && root.dragController.ready; text: qsTr("Drag selected rows into a folder"); textFormat: Text.PlainText; font.pixelSize: 12; opacity: 0.7 }
            ToolButton {
                objectName: "dragPreparedFiles"
                visible: root.dragController && root.dragController.ready
                text: qsTr("Drag out")
                icon.source: "../icons/export.svg"
                icon.color: palette.buttonText
                DragHandler { target: null; onActiveChanged: { if (active) root.dragController.startDrag() } }
                Accessible.name: qsTr("Drag prepared files out")
            }
            ToolButton { text: root.dragController && root.dragController.busy ? qsTr("Cancel") : qsTr("Dismiss"); onClicked: root.dragController.cancel() }
        }
        RowLayout {
        Layout.fillWidth: true
        BusyIndicator {
            running: root.operation.busy
            visible: running
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
        }
        Label { textFormat: Text.PlainText; text: root.operation.status }
        Label { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: root.operation.busy ? root.operation.currentEntry : ""
            elide: Text.ElideMiddle
            opacity: 0.65
        }
        Label { textFormat: Text.PlainText;
            text: !root.showingCompression && root.archiveController.entryModel.query.length > 0
                ? qsTr("%1 of %2 entries").arg(root.archiveController.entryModel.count).arg(root.archiveController.entryModel.totalCount)
                : qsTr("%1 entries").arg(root.showingCompression
                    ? root.compressionController.processedEntries : root.archiveController.entryCount)
            Accessible.name: text
        }
        Label { textFormat: Text.PlainText;
            visible: !root.showingCompression && root.archiveController.selectionCount > 0
            text: qsTr("%1 selected").arg(root.archiveController.selectionCount)
        }
        Label { textFormat: Text.PlainText;
            visible: root.operation.elapsedMilliseconds > 0
            text: qsTr("%1 s").arg((root.operation.elapsedMilliseconds / 1000).toFixed(1))
            opacity: 0.7
        }
        ToolButton {
            objectName: "openCreatedArchive"
            visible: root.showingCompression && root.compressionController.canOpenCreated
            text: qsTr("Open archive")
            onClicked: {
                root.archiveController.openFile(root.compressionController.destination)
                root.compressionController.dismissProgress()
            }
        }
        ToolButton {
            visible: root.showingCompression && !root.compressionController.busy
            text: qsTr("Dismiss")
            onClicked: root.compressionController.dismissProgress()
        }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.showingCompression || root.archiveController.extracting
            spacing: 12
            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                objectName: "taskProgress"
                indeterminate: root.operation.busy && (root.operation.totalBytes < 0
                    || (root.showingCompression && root.compressionController.finalizing))
                from: 0
                to: Math.max(1, root.operation.totalBytes)
                value: root.operation.processedBytes
                Accessible.name: root.showingCompression ? qsTr("Compression progress") : qsTr("Extraction progress")
            }
            Label { textFormat: Text.PlainText;
                text: root.operation.totalBytes < 0
                    ? qsTr("%1 MiB processed").arg((root.operation.processedBytes / 1048576).toFixed(1))
                    : qsTr("%1 / %2 MiB").arg((root.operation.processedBytes / 1048576).toFixed(1))
                        .arg((root.operation.totalBytes / 1048576).toFixed(1))
            }
            Label { textFormat: Text.PlainText;
                text: qsTr("%1 MiB/s").arg((root.operation.bytesPerSecond / 1048576).toFixed(1))
            }
            Label { textFormat: Text.PlainText;
                visible: root.operation.remainingMilliseconds >= 0 && root.operation.busy
                text: qsTr("%1 s remaining").arg(Math.ceil(root.operation.remainingMilliseconds / 1000))
            }
        }
    }
}


