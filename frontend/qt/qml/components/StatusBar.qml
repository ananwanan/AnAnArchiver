pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    required property var compressionController
    readonly property bool showingCompression: root.compressionController.hasTask
    readonly property var operation: showingCompression ? root.compressionController : root.archiveController
    implicitHeight: root.showingCompression || root.archiveController.extracting ? 76 : 38
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4
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
            text: qsTr("%1 entries").arg(root.showingCompression
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


