pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Dialog {
    id: root
    objectName: "taskCenter"
    required property var hostWindow
    property var controller: null
    title: qsTr("Task center")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(760, hostWindow.width - 48)
    height: Math.min(580, hostWindow.height - 48)
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 active tasks").arg(root.controller ? root.controller.activeCount : 0)
                textFormat: Text.PlainText
                font.weight: Font.DemiBold
            }
            Button {
                objectName: "clearFinishedTasks"
                text: qsTr("Clear finished")
                enabled: root.controller && root.controller.count > root.controller.activeCount
                onClicked: root.controller.clearFinished()
            }
        }
        ListView {
            id: tasks
            objectName: "taskList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.controller ? root.controller.model : null
            clip: true
            reuseItems: true
            spacing: 8
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                id: task
                objectName: "taskRow"
                required property string taskId
                required property string taskLabel
                required property string taskState
                required property string taskPath
                required property string taskCurrentFile
                required property real taskProgress
                required property string taskBytes
                required property string taskSpeed
                required property string taskElapsed
                required property string taskRemaining
                required property string taskError
                required property string taskDetails
                required property bool taskCanCancel
                width: tasks.width
                padding: 12
                contentItem: ColumnLayout {
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: task.taskLabel
                            textFormat: Text.PlainText
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Label { text: task.taskState; textFormat: Text.PlainText; color: task.taskError.length ? "#dc6966" : palette.text; opacity: 0.8 }
                        ToolButton {
                            objectName: "cancelTaskButton"
                            visible: task.taskCanCancel
                            text: qsTr("Cancel")
                            icon.source: "../icons/close.svg"
                            icon.color: palette.buttonText
                            onClicked: root.controller.cancelTask(task.taskId)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: task.taskPath
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        opacity: 0.65
                        font.pixelSize: 12
                        HoverHandler { id: pathHover }
                        PlainToolTip { visible: pathHover.hovered; text: task.taskPath }
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: Math.max(0, task.taskProgress)
                        indeterminate: task.taskCanCancel && task.taskProgress < 0
                        Accessible.name: task.taskLabel
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: task.taskCurrentFile.length > 0
                        text: task.taskCurrentFile
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        font.pixelSize: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label { text: task.taskBytes; textFormat: Text.PlainText; font.pixelSize: 12 }
                        Label { text: task.taskSpeed; textFormat: Text.PlainText; font.pixelSize: 12; opacity: 0.65 }
                        Item { Layout.fillWidth: true }
                        Label { text: task.taskElapsed; textFormat: Text.PlainText; font.pixelSize: 12; opacity: 0.65 }
                        Label { visible: task.taskRemaining.length > 0; text: task.taskRemaining; textFormat: Text.PlainText; font.pixelSize: 12; opacity: 0.65 }
                        ToolButton {
                            visible: task.taskError.length > 0
                            text: qsTr("Details")
                            onClicked: { failure.message = task.taskError; failure.details = task.taskDetails; failure.open() }
                        }
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                visible: tasks.count === 0
                spacing: 12
                AppIcon { name: "tasks"; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 40; Layout.preferredHeight: 40 }
                Label { text: qsTr("No tasks yet"); textFormat: Text.PlainText; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                Label { text: qsTr("Opening, extracting and creating archives appear here."); textFormat: Text.PlainText; wrapMode: Text.WordWrap; Layout.maximumWidth: 380; horizontalAlignment: Text.AlignHCenter; opacity: 0.65 }
            }
        }
    }
    standardButtons: Dialog.Close
    ErrorDetailsDialog { id: failure; hostWindow: root.hostWindow; title: qsTr("Task failed") }
}
