pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

ColumnLayout {
    id: root
    required property var archiveController
    required property var compressionController
    required property var directoryModel
    required property Action openAction
    required property Action cancelAction
    required property Action extractAction
    required property Action compressAction
    property Action exportAction: null
    property Action taskAction: null
    property var desktopController: null
    property var taskController: null
    property var dragController: null
    readonly property bool browsing: root.archiveController.currentFile.length > 0 || root.directoryModel.rootPath.length > 0
    signal browseFolder()
    spacing: 0
    function focusSearch() { if (browsing) fileTable.focusSearch() }

    Toolbar {
        Layout.fillWidth: true
        archiveController: root.archiveController
        compressionController: root.compressionController
        openAction: root.openAction
        cancelAction: root.cancelAction
        extractAction: root.extractAction
        compressAction: root.compressAction
        exportAction: root.exportAction
        taskAction: root.taskAction
        taskController: root.taskController
        dragController: root.dragController
        onBrowseFolder: root.browseFolder()
    }
    ToolBar {
        id: navigationBar
        Layout.fillWidth: true
        implicitHeight: 44
        background: Rectangle { color: navigationBar.palette.window }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8
            ToolButton {
                id: homeButton
                objectName: "homeButton"
                text: qsTr("Home")
                display: AbstractButton.IconOnly
                icon.source: "../icons/home.svg"
                icon.color: palette.buttonText
                enabled: root.browsing
                onClicked: { root.archiveController.close(); root.directoryModel.clear() }
                Accessible.name: text
                PlainToolTip { visible: homeButton.hovered; text: qsTr("Close archive") + " (Ctrl+W)" }
            }
            AppIcon { name: "separator"; Layout.preferredWidth: 12; Layout.preferredHeight: 12 }
            Label {
                id: archivePath
                objectName: "currentArchivePath"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.archiveController.currentFile.length > 0 ? root.archiveController.currentFile : qsTr("Home")
                elide: Text.ElideMiddle
                font.pixelSize: 13
                Accessible.name: qsTr("Current archive")
                HoverHandler { id: archivePathHover }
                PlainToolTip { visible: archivePathHover.hovered; text: archivePath.text }
            }
            Label {
                textFormat: Text.PlainText
                text: root.archiveController.formatName
                font.weight: Font.DemiBold
                font.pixelSize: 12
                opacity: 0.7
            }
        }
    }
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        FileTable {
            id: fileTable
            anchors.fill: parent
            visible: root.browsing
            archiveController: root.archiveController
            directoryModel: root.directoryModel
            dragController: root.dragController
            extractAction: root.extractAction
            exportAction: root.exportAction
            onOpenRequested: root.openAction.trigger()
        }
        WelcomePage {
            anchors.fill: parent
            visible: !root.browsing
            openAction: root.openAction
            compressAction: root.compressAction
            desktopController: root.desktopController
        }
    }
    StatusBar {
        Layout.fillWidth: true
        archiveController: root.archiveController
        compressionController: root.compressionController
        dragController: root.dragController
    }
}
