pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "./pages"

ApplicationWindow {
    id: window
    required property var archiveController
    required property var directoryModel
    width: 1180
    height: 740
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: window.archiveController.currentFile.length > 0
           ? window.archiveController.currentFile.split(/[\\/]/).pop() + " — AnAnArchiver"
           : "AnAnArchiver"

    property int themeMode: 0
    readonly property bool darkTheme: themeMode === 2
        || (themeMode === 0 && systemPalette.window.r + systemPalette.window.g + systemPalette.window.b < 1.5)
    SystemPalette { id: systemPalette }
    palette.window: darkTheme ? "#191c22" : "#f5f6f8"
    palette.base: darkTheme ? "#22262e" : "#ffffff"
    palette.alternateBase: darkTheme ? "#292e38" : "#f0f2f5"
    palette.button: darkTheme ? "#303641" : "#e8ebf0"
    palette.text: darkTheme ? "#e9ecf2" : "#232833"
    palette.windowText: palette.text
    palette.buttonText: palette.text
    palette.highlight: darkTheme ? "#4d79bf" : "#3568ad"
    palette.highlightedText: "#ffffff"
    color: palette.window

    Action {
        id: openAction
        text: qsTr("Open archive…")
        shortcut: StandardKey.Open
        onTriggered: archiveDialog.open()
    }
    Action {
        id: closeAction
        text: qsTr("Close archive")
        shortcut: StandardKey.Close
        enabled: window.archiveController.currentFile.length > 0
        onTriggered: window.archiveController.close()
    }
    Action {
        id: cancelAction
        text: qsTr("Cancel")
        shortcut: "Escape"
        enabled: window.archiveController.busy
        onTriggered: window.archiveController.cancel()
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            MenuItem { action: openAction }
            MenuItem { action: closeAction }
            MenuItem { action: cancelAction }
            MenuSeparator {}
            MenuItem {
                action: Action {
                    text: qsTr("Quit")
                    shortcut: StandardKey.Quit
                    onTriggered: Qt.quit()
                }
            }
        }
        Menu {
            title: qsTr("Appearance")
            ActionGroup { id: themeActions }
            MenuItem {
                action: Action {
                    text: qsTr("Follow system")
                    checkable: true
                    checked: window.themeMode === 0
                    ActionGroup.group: themeActions
                    onTriggered: window.themeMode = 0
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Light")
                    checkable: true
                    checked: window.themeMode === 1
                    ActionGroup.group: themeActions
                    onTriggered: window.themeMode = 1
                }
            }
            MenuItem {
                action: Action {
                    text: qsTr("Dark")
                    checkable: true
                    checked: window.themeMode === 2
                    ActionGroup.group: themeActions
                    onTriggered: window.themeMode = 2
                }
            }
        }
        Menu {
            title: qsTr("Help")
            MenuItem {
                text: qsTr("About AnAnArchiver")
                onTriggered: aboutDialog.open()
            }
        }
    }

    FileDialog {
        id: archiveDialog
        title: qsTr("Open archive")
        nameFilters: [qsTr("Archives (*.zip *.zipx *.7z *.rar *.tar *.gz *.tgz *.bz2 *.xz *.zst *.lz4 *.cab *.cpio *.ar *.iso *.lha *.lzh *.xar)"),
                      qsTr("All files (*)")]
        onAccepted: window.archiveController.openUrl(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Browse folder")
        onAccepted: window.directoryModel.loadDirectoryUrl(selectedFolder)
    }

    HomePage {
        anchors.fill: parent
        archiveController: window.archiveController
        directoryModel: window.directoryModel
        openAction: openAction
        cancelAction: cancelAction
        onBrowseFolder: folderDialog.open()
    }

    Dialog {
        id: errorDialog
        title: qsTr("Unable to open archive")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(580, window.width - 48)
        standardButtons: Dialog.Ok
        onClosed: window.archiveController.dismissError()
        contentItem: ColumnLayout {
            spacing: 12
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: window.archiveController.errorMessage
                wrapMode: Text.WordWrap
                font.bold: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, details.implicitHeight + 16)
                TextArea {
                    id: details
                    text: window.archiveController.errorDetails
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    Accessible.name: qsTr("Error details")
                }
            }
        }
    }
    Connections {
        target: window.archiveController
        function onChanged() {
            if (window.archiveController.errorMessage.length > 0 && !errorDialog.opened)
                errorDialog.open()
        }
    }
    Connections {
        target: window.directoryModel
        function onScanFailed(details) {
            folderError.details = details
            folderError.open()
        }
    }
    Dialog {
        id: folderError
        property string details: ""
        title: qsTr("Unable to read folder")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, window.width - 48)
        standardButtons: Dialog.Ok
        Label { textFormat: Text.PlainText; width: parent.width; text: folderError.details; wrapMode: Text.WordWrap }
    }
    Dialog {
        id: aboutDialog
        title: qsTr("About AnAnArchiver")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(460, window.width - 48)
        standardButtons: Dialog.Ok
        contentItem: ColumnLayout {
            Label { textFormat: Text.PlainText; text: "AnAnArchiver"; font.pixelSize: 24; font.bold: true }
            Label { textFormat: Text.PlainText; text: qsTr("Version %1").arg(Qt.application.version) }
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: qsTr("A modern archive browser. Open an archive to inspect its contents.")
                wrapMode: Text.WordWrap
            }
        }
    }
}


