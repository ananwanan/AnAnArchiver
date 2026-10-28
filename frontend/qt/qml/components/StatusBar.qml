pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    required property var compressionController
    property var dragController: null
    property bool mobile: false
    readonly property bool compact: mobile || width < 600
    readonly property bool showingExport: root.dragController && (root.dragController.busy || root.dragController.ready)
    readonly property bool showingCompression: root.compressionController.hasTask
    readonly property var operation: showingCompression ? root.compressionController : root.archiveController
    implicitHeight: Math.max(38, statusContent.implicitHeight + 12)
    background: Rectangle { color: root.palette.window; Rectangle { width: parent.width; height: 1; color: root.palette.mid; opacity: 0.2 } }
    ColumnLayout {
        id: statusContent
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: 6
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
            Label { visible: !root.compact && root.dragController && root.dragController.ready; text: qsTr("Drag selected rows into a folder"); textFormat: Text.PlainText; font.pixelSize: 12; opacity: 0.7 }
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
        Label {
            textFormat: Text.PlainText
            text: root.operation.status
            Layout.fillWidth: root.compact
            Layout.minimumWidth: 0
            elide: Text.ElideRight
        }
        Label { textFormat: Text.PlainText;
            visible: !root.compact
            Layout.fillWidth: true
            text: root.operation.busy ? root.operation.currentEntry : ""
            elide: Text.ElideMiddle
            opacity: 0.65
        }
        Label { textFormat: Text.PlainText;
            visible: !root.compact || !root.showingCompression
            text: (!root.showingCompression && root.archiveController.entryModel.query.length > 0
                ? qsTr("%1 of %2 entries").arg(root.archiveController.entryModel.count).arg(root.archiveController.entryModel.totalCount)
                : qsTr("%1 entries").arg(root.showingCompression
                    ? root.compressionController.processedEntries : root.archiveController.entryCount))
                + (root.compact && root.archiveController.selectionCount > 0
                   ? " · " + qsTr("%1 selected").arg(root.archiveController.selectionCount) : "")
            Accessible.name: text
        }
        Label { textFormat: Text.PlainText;
            visible: !root.compact && !root.showingCompression && root.archiveController.selectionCount > 0
            text: qsTr("%1 selected").arg(root.archiveController.selectionCount)
        }
        Label { textFormat: Text.PlainText;
            visible: !root.compact && root.operation.elapsedMilliseconds > 0
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
        Label {
            Layout.fillWidth: true
            visible: root.compact && root.operation.busy && root.operation.currentEntry.length > 0
            text: root.operation.currentEntry
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            opacity: 0.65
        }
        GridLayout {
            Layout.fillWidth: true
            visible: root.showingCompression || root.archiveController.extracting
            columns: root.compact ? 2 : 4
            columnSpacing: 12
            rowSpacing: 6
            ProgressBar {
                Layout.fillWidth: true
                Layout.columnSpan: root.compact ? 2 : 1
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
                Layout.columnSpan: root.compact ? 2 : 1
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


