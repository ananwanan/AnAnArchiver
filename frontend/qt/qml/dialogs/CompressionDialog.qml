pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Dialog {
    id: root
    objectName: "compressionDialog"
    required property var controller
    required property var hostWindow
    readonly property var format: formatChoice.currentIndex >= 0 ? controller.formats[formatChoice.currentIndex] : null
    readonly property var method: format && methodChoice.currentIndex >= 0 ? format.methods[methodChoice.currentIndex] : null
    title: qsTr("Create archive")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, hostWindow.width - 48)
    height: Math.min(660, hostWindow.height - 48)
    onOpened: {
        replaceExisting.checked = false;
        addFiles.forceActiveFocus();
    }

    Connections {
        target: root.hostWindow
        function onActiveFocusItemChanged() {
            if (root.opened)
                Qt.callLater(formScroll.revealFocusedControl);
        }
    }

    FileDialog {
        id: sourceFiles
        objectName: "compressionSourceFiles"
        title: qsTr("Add files")
        fileMode: FileDialog.OpenFiles
        onAccepted: root.controller.addUrls(selectedFiles)
    }
    FolderDialog {
        id: sourceFolder
        objectName: "compressionSourceFolder"
        title: qsTr("Add folder")
        onAccepted: root.controller.addUrls([selectedFolder])
    }
    FileDialog {
        id: destinationDialog
        objectName: "compressionSaveDialog"
        title: qsTr("Save archive")
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.format ? root.format.extension : ""
        nameFilters: root.format ? [root.format.label + " (*." + root.format.extension + ")"] : []
        onAccepted: destination.text = root.controller.localPath(selectedFile)
    }

    contentItem: ScrollView {
        id: formScroll
        objectName: "compressionFormScroll"
        clip: true
        rightPadding: effectiveScrollBarWidth + 4
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        function revealFocusedControl() {
            const control = root.hostWindow.activeFocusItem;
            if (!control)
                return;
            const viewport = contentItem as Flickable;
            if (!viewport)
                return;
            let ancestor = control;
            while (ancestor && ancestor !== viewport.contentItem)
                ancestor = ancestor.parent;
            if (!ancestor)
                return;
            const top = control.mapToItem(viewport.contentItem, 0, 0).y;
            if (top < viewport.contentY)
                viewport.contentY = top;
            else if (top + control.height > viewport.contentY + viewport.height)
                viewport.contentY = Math.max(0, top + control.height - viewport.height);
        }
        ColumnLayout {
            width: parent.width
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Input files and folders (%1)").arg(root.controller.inputCount)
                    textFormat: Text.PlainText
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    id: addFiles
                    objectName: "compressionAddFiles"
                    text: qsTr("Add files…")
                    onClicked: sourceFiles.open()
                }
                Button {
                    objectName: "compressionAddFolder"
                    text: qsTr("Add folder…")
                    onClicked: sourceFolder.open()
                }
            }
            Frame {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                padding: 0
                ListView {
                    id: inputs
                    objectName: "compressionInputs"
                    anchors.fill: parent
                    clip: true
                    model: root.controller.inputs
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        id: sourceRow
                        required property int index
                        required property string modelData
                        width: inputs.width
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            text: sourceRow.modelData
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            Accessible.name: text
                            HoverHandler {
                                id: sourceHover
                            }
                            PlainToolTip {
                                visible: sourceHover.hovered
                                text: sourceRow.modelData
                            }
                        }
                        ToolButton {
                            text: qsTr("Remove")
                            Accessible.name: qsTr("Remove %1").arg(sourceRow.modelData)
                            onClicked: root.controller.removeInput(sourceRow.index)
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: root.controller.inputCount === 0
                        text: qsTr("Add files or folders to create an archive.")
                        textFormat: Text.PlainText
                    }
                }
            }
            Label {
                text: qsTr("Output archive")
                textFormat: Text.PlainText
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: destination
                    objectName: "compressionDestination"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Choose or enter an absolute archive path")
                    selectByMouse: true
                    Accessible.name: qsTr("Output archive")
                }
                Button {
                    objectName: "compressionBrowseOutput"
                    text: qsTr("Browse…")
                    onClicked: destinationDialog.open()
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                Label {
                    text: qsTr("Format")
                    textFormat: Text.PlainText
                }
                ComboBox {
                    id: formatChoice
                    objectName: "compressionFormat"
                    Layout.fillWidth: true
                    model: root.controller.formats
                    textRole: "label"
                    Accessible.name: qsTr("Format")
                }
                Label {
                    text: qsTr("Compression method")
                    textFormat: Text.PlainText
                }
                ComboBox {
                    id: methodChoice
                    objectName: "compressionMethod"
                    Layout.fillWidth: true
                    model: root.format ? root.format.methods : []
                    currentIndex: root.format ? Math.min(count - 1, root.format.defaultMethod) : -1
                    textRole: "label"
                    Accessible.name: qsTr("Compression method")
                }
                Label {
                    text: qsTr("Compression level")
                    textFormat: Text.PlainText
                }
                RowLayout {
                    SpinBox {
                        id: levelChoice
                        objectName: "compressionLevel"
                        editable: true
                        enabled: root.method ? root.method.supportsLevel : false
                        from: root.method ? root.method.minimumLevel : 0
                        to: root.method ? root.method.maximumLevel : 0
                        value: root.method ? Math.max(from, Math.min(to, root.method.defaultLevel)) : 0
                        Accessible.name: qsTr("Compression level")
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.method && root.method.supportsLevel ? qsTr("Higher levels favor smaller archives.") : qsTr("This method has no level setting.")
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                    }
                }
            }
            CheckBox {
                id: replaceExisting
                objectName: "compressionReplaceExisting"
                text: qsTr("Replace an existing output archive after successful creation")
                checked: false
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Folders include their contents. Choose output outside selected folders. Existing output is kept if creation fails or is cancelled.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                opacity: 0.7
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "startCompression"
            text: qsTr("Create")
            enabled: root.controller.inputCount > 0 && !root.controller.busy && destination.text.trim().length > 0 && root.method !== null
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        onAccepted: {
            if (root.controller.create(destination.text, formatChoice.currentIndex, methodChoice.currentIndex, levelChoice.value, replaceExisting.checked))
                root.close();
        }
        onRejected: root.close()
    }
}
