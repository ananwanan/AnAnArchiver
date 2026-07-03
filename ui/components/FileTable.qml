import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SplitView {

    orientation: Qt.Horizontal

    //======================
    // 左侧目录树
    //======================
    Rectangle {

        SplitView.preferredWidth: 220

        color: palette.base
        border.color: palette.alternateBase

        ListView {

            anchors.fill: parent
            clip: true

            model: [
                { icon: "📁", name: "压缩包" },
                { icon: "📂", name: "Documents" },
                { icon: "📂", name: "Images" },
                { icon: "📂", name: "Music" },
                { icon: "📂", name: "Videos" },
                { icon: "📂", name: "Temp" }
            ]

            delegate: ItemDelegate {

                width: ListView.view.width
                height: 36

                background: Rectangle {
                    color: hovered ? palette.highlight : "transparent"
                }

                contentItem: Row {

                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Text {
                        text: modelData.icon
                    }

                    Text {
                        text: modelData.name
                    }
                }
            }
        }
    }

    //======================
    // 右侧文件列表
    //======================
    Rectangle {

        SplitView.fillWidth: true
        
        color: palette.base
        border.color: palette.alternateBase

        ListView {

            anchors.fill: parent

            clip: true
            spacing: 0

            model: 1024

            delegate: Rectangle {

                width: ListView.view.width
                height: 56
                
                color: mouse.containsMouse
                 ? palette.highlight
                 : palette.base

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                }

                Row {

                    anchors.fill: parent
                    anchors.margins: 8

                    spacing: 10

                    Rectangle {
                        width: 32
                        height: 32
                        radius: 4
                        color: "#FFD54F"
                    }

                    Column {

                        anchors.verticalCenter: parent.verticalCenter

                        spacing: 2

                        Text {
                            text: "test" + index + ".zip"
                            font.bold: true
                        }

                        Text {
                            text: "15.3 MB    2026-07-02"
                            font.pixelSize: 11
                            color: "#808080"
                        }
                    }
                }
            }
        }
    }
}