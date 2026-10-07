pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    required property Action openAction
    required property Action cancelAction
    required property Action extractAction
    signal browseFolder()
    padding: 6
    RowLayout {
        anchors.fill: parent
        spacing: 6
        ToolButton { action: root.openAction; Accessible.name: text }
        Item {
            implicitWidth: compressButton.implicitWidth
            implicitHeight: compressButton.implicitHeight
            ToolButton {
                id: compressButton
                anchors.fill: parent
                text: qsTr("Compress")
                enabled: false
                opacity: 0.45
            }
            HoverHandler { id: compressHover }
            ToolTip.visible: compressHover.hovered
            ToolTip.text: qsTr("Creating archives is not available in this version.")
        }
        ToolButton { objectName: "extractButton"; action: root.extractAction; Accessible.name: text }
        ToolSeparator {}
        ToolButton {
            text: qsTr("Browse folder…")
            onClicked: root.browseFolder()
            Accessible.name: text
        }
        Item { Layout.fillWidth: true }
        ToolButton { action: root.cancelAction; visible: root.archiveController.busy; Accessible.name: text }
    }
}

