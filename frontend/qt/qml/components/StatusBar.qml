pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    implicitHeight: 38
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12
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
            visible: root.archiveController.elapsedMilliseconds > 0
            text: qsTr("%1 s").arg((root.archiveController.elapsedMilliseconds / 1000).toFixed(1))
            opacity: 0.7
        }
    }
}


