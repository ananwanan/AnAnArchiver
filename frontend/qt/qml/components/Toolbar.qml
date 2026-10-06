pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: root
    required property var archiveController
    required property Action openAction
    required property Action cancelAction
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
        Item {
            implicitWidth: extractButton.implicitWidth
            implicitHeight: extractButton.implicitHeight
            ToolButton {
                id: extractButton
                anchors.fill: parent
                text: qsTr("Extract")
                enabled: false
                opacity: 0.45
            }
            HoverHandler { id: extractHover }
            ToolTip.visible: extractHover.hovered
            ToolTip.text: qsTr("Extraction is not available in this version.")
        }
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

