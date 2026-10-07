pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "./pages"
import "./dialogs"

ApplicationWindow {
    id: window
    required property var archiveController
    required property var compressionController
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
        onTriggered: { window.compressionController.dismissProgress(); archiveDialog.open() }
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
        objectName: "cancelAction"
        text: qsTr("Cancel")
        shortcut: "Escape"
        enabled: window.archiveController.busy || window.compressionController.busy
        onTriggered: {
            if (window.compressionController.busy)
                window.compressionController.cancel()
            else
                window.archiveController.cancel()
        }
    }
    Action {
        id: compressAction
        objectName: "compressAction"
        text: qsTr("Create archive…")
        shortcut: "Ctrl+N"
        enabled: !window.compressionController.busy && window.compressionController.formats.length > 0
        onTriggered: compressionDialog.open()
    }
    Action {
        id: extractAction
        objectName: "extractAction"
        text: qsTr("Extract…")
        shortcut: "Ctrl+E"
        enabled: window.archiveController.canExtract && !window.compressionController.busy
        onTriggered: {
            window.compressionController.dismissProgress()
            selectedOnly.checked = window.archiveController.selectionCount > 0
            allEntries.checked = !selectedOnly.checked
            extractionDialog.open()
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            MenuItem { action: openAction }
            MenuItem { action: compressAction }
            MenuItem { action: closeAction }
            MenuItem { action: extractAction }
            MenuItem {
                text: qsTr("Select all")
                enabled: window.archiveController.canExtract
                onTriggered: window.archiveController.selectAll()
            }
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
        compressionController: window.compressionController
        directoryModel: window.directoryModel
        openAction: openAction
        cancelAction: cancelAction
        extractAction: extractAction
        compressAction: compressAction
        onBrowseFolder: folderDialog.open()
    }

    CompressionDialog {
        id: compressionDialog
        controller: window.compressionController
        hostWindow: window
    }
    Dialog {
        id: compressionErrorDialog
        objectName: "compressionErrorDialog"
        title: qsTr("Archive creation failed")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(580, window.width - 48)
        standardButtons: Dialog.Ok
        onClosed: window.compressionController.dismissError()
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: window.compressionController.errorMessage
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.bold: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(220, compressionDetails.implicitHeight + 16)
                TextArea {
                    id: compressionDetails
                    text: window.compressionController.errorDetails
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
        target: window.compressionController
        function onChanged() {
            if (window.compressionController.errorMessage.length > 0 && !compressionErrorDialog.opened)
                compressionErrorDialog.open()
        }
    }

    Dialog {
        id: errorDialog
        objectName: "errorDialog"
        title: qsTr("Archive operation failed")
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
            if (window.archiveController.passwordRequested && !indexPasswordDialog.opened)
                indexPasswordDialog.open()
            if (window.archiveController.awaitingConflict && !overwriteDialog.opened)
                overwriteDialog.open()
            if (!window.archiveController.awaitingConflict && overwriteDialog.opened)
                overwriteDialog.close()
            if (window.archiveController.errorMessage.length > 0
                && !window.archiveController.passwordRequested && !errorDialog.opened)
                errorDialog.open()
        }
    }
    FolderDialog {
        id: destinationDialog
        title: qsTr("Choose extraction destination")
        onAccepted: destinationField.text = window.archiveController.localPath(selectedFolder)
    }
    Dialog {
        id: extractionDialog
        objectName: "extractionDialog"
        title: qsTr("Extract archive")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(580, window.width - 48)
        onClosed: extractionPassword.clear()
        contentItem: ColumnLayout {
            spacing: 12
            Label { textFormat: Text.PlainText; text: qsTr("Destination folder") }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: destinationField
                    objectName: "extractionDestination"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Choose or enter an absolute folder path")
                    selectByMouse: true
                    Accessible.name: qsTr("Destination folder")
                }
                Button { text: qsTr("Browse…"); onClicked: destinationDialog.open() }
            }
            RowLayout {
                ButtonGroup { id: scopeGroup }
                RadioButton {
                    id: allEntries
                    objectName: "extractAllScope"
                    text: qsTr("All entries")
                    checked: true
                    ButtonGroup.group: scopeGroup
                }
                RadioButton {
                    id: selectedOnly
                    objectName: "extractSelectedScope"
                    text: qsTr("Selected entries (%1)").arg(window.archiveController.selectionCount)
                    enabled: window.archiveController.selectionCount > 0
                    ButtonGroup.group: scopeGroup
                }
            }
            Label { textFormat: Text.PlainText; text: qsTr("Password (if required)") }
            TextField {
                id: extractionPassword
                objectName: "extractionPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Password (if required)")
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                Label { textFormat: Text.PlainText; text: qsTr("Existing files") }
                ComboBox {
                    id: overwritePolicy
                    objectName: "overwritePolicy"
                    Layout.fillWidth: true
                    model: [qsTr("Ask each time"), qsTr("Overwrite"), qsTr("Skip"), qsTr("Rename"), qsTr("Overwrite if newer")]
                }
                Label { textFormat: Text.PlainText; text: qsTr("Links") }
                ComboBox {
                    id: linkPolicy
                    Layout.fillWidth: true
                    model: [qsTr("Safe links"), qsTr("Preserve links inside destination"), qsTr("Skip links")]
                }
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button {
                objectName: "startExtraction"
                text: qsTr("Extract")
                enabled: destinationField.text.trim().length > 0 && window.archiveController.canExtract
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            onAccepted: {
                window.archiveController.extract(destinationField.text, selectedOnly.checked,
                    extractionPassword.text, overwritePolicy.currentIndex, linkPolicy.currentIndex)
                extractionPassword.clear()
                extractionDialog.close()
            }
            onRejected: extractionDialog.close()
        }
    }
    Dialog {
        id: overwriteDialog
        objectName: "overwriteDialog"
        title: qsTr("File already exists")
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: Overlay.overlay
        width: Math.min(580, window.width - 48)
        onOpened: applyToAll.checked = false
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                objectName: "conflictDestination"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: window.archiveController.conflictPath
                wrapMode: Text.WrapAnywhere
            }
            ComboBox {
                id: conflictChoice
                objectName: "conflictChoice"
                Layout.fillWidth: true
                model: [qsTr("Overwrite"), qsTr("Skip"), qsTr("Rename"), qsTr("Overwrite if newer")]
            }
            CheckBox { id: applyToAll; objectName: "applyToAll"; text: qsTr("Apply to all remaining conflicts") }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "cancelExtraction"
                text: qsTr("Cancel extraction")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button { objectName: "resolveOverwrite"; text: qsTr("Apply"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            onAccepted: window.archiveController.resolveConflict(conflictChoice.currentIndex + 1, applyToAll.checked)
            onRejected: window.archiveController.cancel()
        }
    }
    Dialog {
        id: indexPasswordDialog
        objectName: "indexPasswordDialog"
        title: qsTr("Archive password")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(480, window.width - 48)
        onAccepted: window.archiveController.retryOpenPassword(indexPassword.text)
        onRejected: window.archiveController.dismissPassword()
        onClosed: indexPassword.clear()
        contentItem: ColumnLayout {
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: window.archiveController.errorMessage
                wrapMode: Text.WordWrap
            }
            TextField {
                id: indexPassword
                objectName: "indexPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Archive password")
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: qsTr("Unlock"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            onAccepted: indexPasswordDialog.accept()
            onRejected: indexPasswordDialog.reject()
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


