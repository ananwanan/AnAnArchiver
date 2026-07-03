import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    title: qsTr("关于")

    modal: true
    anchors.centerIn: Overlay.overlay

    width: 450
    standardButtons: Dialog.Ok

    contentItem: RowLayout {

        spacing: 20

        Image {
            source: "qrc:/images/logo.png"   // 没有Logo可以先删掉
            width: 64
            height: 64
            fillMode: Image.PreserveAspectFit
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: qsTr("Qt 解压软件")
                font.pixelSize: 22
                font.bold: true
            }

            Label {
                text: qsTr("Version 1.0.0")
            }

            Label {
                text: qsTr("基于 Qt 6 开发的现代压缩与解压工具。")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#D0D0D0"
            }

            Label {
                text: qsTr("© 2026 AnAn Company")
                color: "#808080"
            }
        }
    }
}