pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    implicitHeight: root.archiveController.extracting ? 62 : 38
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12
        RowLayout {
        Layout.fillWidth: true
        BusyIndicator {
            running: root.archiveController.busy
            visible: running
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
        }
        Label { textFormat: Text.PlainText; text: root.archiveController.status }
        Label { textFormat: Text.PlainText;
            Layout.fillWidth: true
            text: root.archiveController.busy ? root.archiveController.currentEntry : ""
            elide: Text.ElideMiddle
            opacity: 0.65
        }
        Label { textFormat: Text.PlainText;
            text: qsTr("%1 entries").arg(root.archiveController.entryCount)
            Accessible.name: text
        }
        Label { textFormat: Text.PlainText;
            visible: root.archiveController.selectionCount > 0
            text: qsTr("%1 selected").arg(root.archiveController.selectionCount)
        }
        Label { textFormat: Text.PlainText;
            visible: root.archiveController.elapsedMilliseconds > 0
            text: qsTr("%1 s").arg((root.archiveController.elapsedMilliseconds / 1000).toFixed(1))
            opacity: 0.7
        }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.archiveController.extracting
            spacing: 12
            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                indeterminate: root.archiveController.totalBytes < 0 && root.archiveController.busy
                from: 0
                to: Math.max(1, root.archiveController.totalBytes)
                value: root.archiveController.processedBytes
                Accessible.name: qsTr("Extraction progress")
            }
            Label { textFormat: Text.PlainText;
                text: root.archiveController.totalBytes < 0
                    ? qsTr("%1 MiB processed").arg((root.archiveController.processedBytes / 1048576).toFixed(1))
                    : qsTr("%1 / %2 MiB").arg((root.archiveController.processedBytes / 1048576).toFixed(1))
                        .arg((root.archiveController.totalBytes / 1048576).toFixed(1))
            }
            Label { textFormat: Text.PlainText;
                text: qsTr("%1 MiB/s").arg((root.archiveController.bytesPerSecond / 1048576).toFixed(1))
            }
            Label { textFormat: Text.PlainText;
                visible: root.archiveController.remainingMilliseconds >= 0 && root.archiveController.busy
                text: qsTr("%1 s remaining").arg(Math.ceil(root.archiveController.remainingMilliseconds / 1000))
            }
        }
    }
}


