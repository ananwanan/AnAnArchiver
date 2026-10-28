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
    property bool mobile: false
    property var storageAccessController: null
    readonly property bool browsing: root.archiveController.currentFile.length > 0 || root.directoryModel.rootPath.length > 0
    signal browseFolder()
    signal moreRequested()
    spacing: 0
    function focusSearch() { if (browsing) fileTable.focusSearch() }

    Pane {
        id: storageNotice
        objectName: "storageAccessNotice"
        Layout.fillWidth: true
        visible: root.storageAccessController && root.storageAccessController.available
            && !root.storageAccessController.granted
        padding: 12
        background: Rectangle { color: storageNotice.palette.alternateBase }
        contentItem: ColumnLayout {
            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "storageAccessNoticeText"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("File access is needed for shared storage.")
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                }
                Button {
                    objectName: "requestStorageAccess"
                    Layout.minimumHeight: root.mobile ? 48 : 0
                    text: qsTr("Allow access")
                    enabled: root.storageAccessController && !root.storageAccessController.requestInProgress
                    onClicked: root.storageAccessController.showPrompt()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.storageAccessController ? root.storageAccessController.error : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }
        }
    }

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
        mobile: root.mobile
        onBrowseFolder: root.browseFolder()
        onMoreRequested: root.moreRequested()
    }
    ToolBar {
        id: navigationBar
        Layout.fillWidth: true
        implicitHeight: root.mobile ? 52 : 44
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
                Layout.minimumWidth: root.mobile ? 48 : 0
                Layout.minimumHeight: root.mobile ? 48 : 0
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
            mobile: root.mobile
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
            mobile: root.mobile
        }
    }
    StatusBar {
        Layout.fillWidth: true
        archiveController: root.archiveController
        compressionController: root.compressionController
        dragController: root.dragController
        mobile: root.mobile
    }
}
