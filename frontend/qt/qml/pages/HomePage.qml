pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

ColumnLayout {
    id: root
    required property var archiveController
    required property var directoryModel
    required property Action openAction
    required property Action cancelAction
    required property Action extractAction
    signal browseFolder()
    spacing: 0

    Toolbar {
        Layout.fillWidth: true
        archiveController: root.archiveController
        openAction: root.openAction
        cancelAction: root.cancelAction
        extractAction: root.extractAction
        onBrowseFolder: root.browseFolder()
    }
    ToolBar {
        Layout.fillWidth: true
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Label { textFormat: Text.PlainText;
                id: archivePath
                Layout.fillWidth: true
                text: root.archiveController.currentFile.length > 0 ? root.archiveController.currentFile : qsTr("Home")
                elide: Text.ElideMiddle
                Accessible.name: qsTr("Current archive")
                HoverHandler { id: archivePathHover }
                PlainToolTip {
                    visible: archivePathHover.hovered
                    text: archivePath.text
                }
            }
            Label { textFormat: Text.PlainText; text: root.archiveController.formatName; font.bold: true }
        }
    }
    FileTable {
        Layout.fillWidth: true
        Layout.fillHeight: true
        archiveController: root.archiveController
        directoryModel: root.directoryModel
        onOpenRequested: root.openAction.trigger()
    }
    StatusBar { Layout.fillWidth: true; archiveController: root.archiveController }
}


