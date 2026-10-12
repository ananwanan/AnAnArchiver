pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "./pages"
import "./dialogs"
import "./components"

ApplicationWindow {
    id: window
    required property var archiveController
    required property var compressionController
    required property var directoryModel
    property var desktopController: null
    property var taskController: null
    property var dragController: null
    property var buildInfo: null
    function showShellCompression(destination) {
        compressionDialog.suggestDestination(destination)
        compressionDialog.open()
    }
    function showShellExtractionPassword(destination) {
        errorDialog.close()
        window.archiveController.dismissError()
        destinationField.text = destination
        allEntries.checked = true
        overwritePolicy.currentIndex = 0
        linkPolicy.currentIndex = 0
        extractionPassword.clear()
        extractionDialog.open()
        Qt.callLater(function() { extractionPassword.forceActiveFocus() })
    }
    width: 1180
    height: 740
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: window.archiveController.currentFile.length > 0
           ? window.archiveController.currentFile.split(/[\\/]/).pop() + " — AnAnArchiver"
           : "AnAnArchiver"

    property int themeMode: desktopController ? desktopController.themeMode : 0
    onThemeModeChanged: {
        if (desktopController && desktopController.themeMode !== themeMode)
            desktopController.themeMode = themeMode
    }
    readonly property bool darkTheme: themeMode === 2
        || (themeMode === 0 && systemPalette.window.r + systemPalette.window.g + systemPalette.window.b < 1.5)
    SystemPalette { id: systemPalette }
    font.pixelSize: 14
    palette.window: darkTheme ? "#1b2029" : "#f3f5f9"
    palette.base: darkTheme ? "#242a35" : "#ffffff"
    palette.alternateBase: darkTheme ? "#2e3644" : "#edf1f8"
    palette.button: darkTheme ? "#343e50" : "#e7ecf5"
    palette.mid: darkTheme ? "#78869c" : "#687891"
    palette.text: darkTheme ? "#e8edf5" : "#26334b"
    palette.placeholderText: darkTheme ? "#9ba8bb" : "#718098"
    palette.windowText: darkTheme ? "#e8edf5" : "#26334b"
    palette.buttonText: darkTheme ? "#e8edf5" : "#26334b"
    palette.highlight: darkTheme ? "#557edc" : "#4268cf"
    palette.highlightedText: "#ffffff"
    color: palette.window

    Connections {
        target: window.desktopController
        function onSettingsErrorChanged() {
            if (window.desktopController.settingsError.length > 0 && !settingsErrorDialog.opened)
                settingsErrorDialog.open()
        }
    }
    Component.onCompleted: {
        if (window.desktopController && window.desktopController.settingsError.length > 0)
            settingsErrorDialog.open()
    }
    ErrorDetailsDialog {
        id: settingsErrorDialog
        objectName: "settingsErrorDialog"
        hostWindow: window
        title: qsTr("Settings error")
        message: qsTr("Preferences or recent history could not be saved.")
        details: window.desktopController ? window.desktopController.settingsError : ""
    }

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
            || (window.dragController && window.dragController.busy)
        onTriggered: {
            if (window.dragController && window.dragController.busy)
                window.dragController.cancel()
            else if (window.compressionController.busy)
                window.compressionController.cancel()
            else
                window.archiveController.cancel()
        }
    }

    Action {
        id: searchAction
        objectName: "searchAction"
        text: qsTr("Search archive")
        shortcut: StandardKey.Find
        enabled: window.archiveController.currentFile.length > 0 && !window.archiveController.busy
        onTriggered: homePage.focusSearch()
    }
    Action {
        id: taskAction
        objectName: "taskCenterAction"
        text: qsTr("Task center")
        shortcut: "Ctrl+J"
        enabled: window.taskController !== null
        onTriggered: taskCenter.open()
    }
    Action {
        id: exportAction
        objectName: "exportAction"
        text: window.dragController && window.dragController.ready ? qsTr("Drag prepared files out") : qsTr("Prepare selected files to drag out…")
        shortcut: "Ctrl+Shift+E"
        enabled: window.dragController && !window.dragController.busy
            && window.archiveController.canExtract && window.archiveController.selectionCount > 0
        onTriggered: {
            if (window.dragController.ready) window.dragController.startDrag()
            else exportDialog.open()
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
            MenuItem { action: exportAction; visible: window.dragController !== null }
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
            title: qsTr("View")
            MenuItem { action: searchAction }
            MenuItem { action: taskAction }
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
        id: homePage
        anchors.fill: parent
        archiveController: window.archiveController
        compressionController: window.compressionController
        directoryModel: window.directoryModel
        openAction: openAction
        cancelAction: cancelAction
        extractAction: extractAction
        compressAction: compressAction
        exportAction: window.dragController ? exportAction : null
        taskAction: window.taskController ? taskAction : null
        desktopController: window.desktopController
        taskController: window.taskController
        dragController: window.dragController
        onBrowseFolder: folderDialog.open()
    }

    TaskCenter { id: taskCenter; hostWindow: window; controller: window.taskController }
    DropArea {
        id: dropArea
        objectName: "archiveDropArea"
        anchors.fill: parent
        enabled: window.dragController !== null
        onEntered: function(drag) {
            drag.accepted = drag.hasUrls && !window.compressionController.busy
                && !window.archiveController.busy && !window.dragController.busy
        }
        onDropped: function(drop) {
            if (drop.hasUrls && window.dragController) {
                window.dragController.dropUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            color: window.palette.base
            opacity: 0.96
            border.width: 2
            border.color: window.palette.highlight
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 480)
                spacing: 18
                AppIcon { name: "open"; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 48; Layout.preferredHeight: 48 }
                Label { Layout.fillWidth: true; text: qsTr("Drop to open or create an archive"); textFormat: Text.PlainText; font.pixelSize: 23; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
                Label { Layout.fillWidth: true; text: qsTr("An archive opens here. Files and folders are added to a new archive."); textFormat: Text.PlainText; opacity: 0.65; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
            }
        }
    }
    Connections {
        target: window.dragController
        function onCreateRequested() { compressionDialog.open() }
        function onChanged() {
            if (window.dragController.errorMessage.length > 0 && !dragErrorDialog.opened)
                dragErrorDialog.open()
        }
    }
    ErrorDetailsDialog {
        id: dragErrorDialog
        objectName: "dragErrorDialog"
        hostWindow: window
        title: qsTr("File transfer failed")
        message: window.dragController ? window.dragController.errorMessage : ""
        details: window.dragController ? window.dragController.errorDetails : ""
        onClosed: if (window.dragController) window.dragController.dismissError()
    }
    Dialog {
        id: exportDialog
        objectName: "exportDialog"
        title: qsTr("Prepare files to drag out")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(540, window.width - 48)
        onClosed: exportPassword.clear()
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 selected entries will be extracted to temporary storage. When ready, drag the selected rows into a folder.").arg(window.archiveController.selectionCount)
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }
            Label { text: qsTr("Password (if required)"); textFormat: Text.PlainText }
            TextField {
                id: exportPassword
                objectName: "exportPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Password (if required)")
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "prepareExport"; text: qsTr("Prepare"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; enabled: exportAction.enabled }
            onAccepted: { window.dragController.prepareExport(exportPassword.text); exportPassword.clear(); exportDialog.close() }
            onRejected: exportDialog.close()
        }
    }

    CompressionDialog {
        id: compressionDialog
        controller: window.compressionController
        hostWindow: window
    }
    ErrorDetailsDialog {
        id: compressionErrorDialog
        objectName: "compressionErrorDialog"
        hostWindow: window
        title: qsTr("Archive creation failed")
        message: window.compressionController.errorMessage
        details: window.compressionController.errorDetails
        onClosed: window.compressionController.dismissError()
    }
    Connections {
        target: window.compressionController
        function onChanged() {
            if (window.compressionController.errorMessage.length > 0 && !compressionErrorDialog.opened)
                compressionErrorDialog.open()
        }
    }

    ErrorDetailsDialog {
        id: errorDialog
        objectName: "errorDialog"
        hostWindow: window
        title: qsTr("Archive operation failed")
        message: window.archiveController.errorMessage
        details: window.archiveController.errorDetails
        onClosed: window.archiveController.dismissError()
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
        height: Math.min(450, window.height - 48)
        onOpened: destinationField.forceActiveFocus()
        onClosed: extractionPassword.clear()
        contentItem: ScrollView {
            id: extractionForm
            clip: true
            contentWidth: availableWidth
            rightPadding: effectiveScrollBarWidth + 4
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            function revealFocusedControl() {
                const control = window.activeFocusItem
                const viewport = contentItem as Flickable
                if (!control || !viewport) return
                let ancestor = control
                while (ancestor && ancestor !== viewport.contentItem) ancestor = ancestor.parent
                if (!ancestor) return
                const top = control.mapToItem(viewport.contentItem, 0, 0).y
                if (top < viewport.contentY) viewport.contentY = top
                else if (top + control.height > viewport.contentY + viewport.height)
                    viewport.contentY = Math.max(0, top + control.height - viewport.height)
            }
            ColumnLayout {
            width: parent.width
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
    Connections {
        target: window
        function onActiveFocusItemChanged() {
            if (extractionDialog.opened) Qt.callLater(extractionForm.revealFocusedControl)
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
                maximumLineCount: 6
                elide: Text.ElideRight
                HoverHandler { id: conflictPathHover }
                PlainToolTip { visible: conflictPathHover.hovered; text: window.archiveController.conflictPath }
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
    ErrorDetailsDialog {
        id: folderError
        hostWindow: window
        title: qsTr("Unable to read folder")
        message: qsTr("Unable to read folder")
    }
    Dialog {
        id: aboutDialog
        objectName: "aboutDialog"
        title: qsTr("About AnAnArchiver")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, window.width - 48)
        standardButtons: Dialog.Ok
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Image {
                    objectName: "aboutAppIcon"
                    source: window.buildInfo ? "qrc:/branding/ananarchiver.png" : ""
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 128
                    sourceSize.height: 128
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: "AnAnArchiver"
                        font.pixelSize: 24
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("Version %1").arg(Qt.application.version)
                        wrapMode: Text.WordWrap
                    }
                }
            }
            Label { textFormat: Text.PlainText;
                Layout.fillWidth: true
                text: qsTr("A modern archive browser. Open an archive to inspect its contents.")
                wrapMode: Text.WordWrap
            }
            Label {
                objectName: "aboutBuildTime"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: qsTr("Build time: %1").arg(window.buildInfo ? window.buildInfo.buildTime : qsTr("Unknown"))
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: qsTr("GitHub repository")
                font.bold: true
            }
            Button {
                id: repositoryLink
                objectName: "aboutRepositoryLink"
                Layout.fillWidth: true
                flat: true
                enabled: window.buildInfo !== null
                text: window.buildInfo ? window.buildInfo.repositoryUrl.toString() : ""
                Accessible.name: qsTr("Open GitHub repository")
                Accessible.description: text
                contentItem: Label {
                    text: repositoryLink.text
                    textFormat: Text.PlainText
                    color: window.palette.highlight
                    font.underline: true
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: Qt.openUrlExternally(window.buildInfo.repositoryUrl)
            }
        }
    }
}


