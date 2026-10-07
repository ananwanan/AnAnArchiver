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
    signal browseFolder()
    padding: 6
    RowLayout {
        anchors.fill: parent
        spacing: 6
        ToolButton { action: root.openAction; Accessible.name: text }
        ToolButton { objectName: "compressButton"; action: root.compressAction; Accessible.name: text }
        ToolButton { objectName: "extractButton"; action: root.extractAction; Accessible.name: text }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Browse folder…")
            onClicked: root.browseFolder()
            Accessible.name: text
        }
        Item { Layout.fillWidth: true }
        ToolButton { action: root.cancelAction; visible: root.archiveController.busy || root.compressionController.busy; Accessible.name: text }
    }
}

