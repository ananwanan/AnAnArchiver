pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    required property var compressionController
    required property Action openAction
    required property Action cancelAction
    required property Action extractAction
    required property Action compressAction
    property Action exportAction: null
    property Action taskAction: null
    property var taskController: null
    property var dragController: null
    signal browseFolder()
    padding: 8
    implicitHeight: 54
    background: Rectangle { color: root.palette.base; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.palette.mid; opacity: 0.2 } }
    RowLayout {
        anchors.fill: parent
        spacing: 4
        ToolButton {
            id: openButton
            opacity: enabled ? 1 : 0.45
            action: root.openAction
            text: qsTr("Open")
            icon.source: "../icons/open.svg"
            icon.color: palette.buttonText
            Accessible.name: root.openAction.text
            PlainToolTip { visible: openButton.hovered; text: root.openAction.text + " (Ctrl+O)" }
        }
        ToolButton {
            id: createButton
            opacity: enabled ? 1 : 0.45
            objectName: "compressButton"
            action: root.compressAction
            text: qsTr("Create")
            icon.source: "../icons/create.svg"
            icon.color: palette.buttonText
            Accessible.name: root.compressAction.text
            PlainToolTip { visible: createButton.hovered; text: root.compressAction.text + " (Ctrl+N)" }
        }
        ToolButton {
            id: extractButton
            opacity: enabled ? 1 : 0.45
            objectName: "extractButton"
            action: root.extractAction
            text: qsTr("Extract")
            icon.source: "../icons/extract.svg"
            icon.color: palette.buttonText
            Accessible.name: root.extractAction.text
            PlainToolTip { visible: extractButton.hovered; text: root.extractAction.text + " (Ctrl+E)" }
        }
        ToolButton {
            id: exportButton
            opacity: enabled ? 1 : 0.45
            objectName: "exportButton"
            visible: root.exportAction !== null
            action: root.exportAction
            text: root.dragController && root.dragController.ready ? qsTr("Drag out") : qsTr("Export")
            display: root.width < 820 ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            icon.source: "../icons/export.svg"
            icon.color: palette.buttonText
            Accessible.name: root.exportAction ? root.exportAction.text : ""
            PlainToolTip { visible: exportButton.hovered; text: root.exportAction ? root.exportAction.text : "" }
            DragHandler {
                target: null
                enabled: root.dragController && root.dragController.ready
                onActiveChanged: { if (active) root.dragController.startDrag() }
            }
        }
        ToolSeparator {}
        ToolButton {
            id: foldersButton
            text: qsTr("Folders")
            display: root.width < 820 ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            icon.source: "../icons/folder.svg"
            icon.color: palette.buttonText
            onClicked: root.browseFolder()
            Accessible.name: qsTr("Browse folder…")
            PlainToolTip { visible: foldersButton.hovered; text: qsTr("Browse folder…") }
        }
        Item { Layout.fillWidth: true }
        ToolButton {
            id: tasksButton
            objectName: "taskCenterButton"
            visible: root.taskAction !== null
            action: root.taskAction
            text: root.taskController && root.taskController.activeCount > 0
                ? qsTr("Tasks (%1)").arg(root.taskController.activeCount) : qsTr("Tasks")
            icon.source: "../icons/tasks.svg"
            icon.color: palette.buttonText
            Accessible.name: qsTr("Task center")
            PlainToolTip { visible: tasksButton.hovered; text: qsTr("Task center") + " (Ctrl+J)" }
        }
        ToolButton {
            action: root.cancelAction
            visible: root.cancelAction.enabled
            icon.source: "../icons/close.svg"
            icon.color: palette.buttonText
            Accessible.name: text
        }
    }
}
