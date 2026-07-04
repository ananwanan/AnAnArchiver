import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import App 1.0

SplitView {

    orientation: Qt.Horizontal

    //======================
    // 左侧目录树
    //======================
    Rectangle {

        SplitView.preferredWidth: 220

        color: palette.base
        border.color: palette.alternateBase

        TreeView {
            id: treeView

            anchors.fill: parent
            clip: true

            model: AppContext.treeModel

            selectionModel: ItemSelectionModel {
                model: treeView.model
            }

            delegate: TreeViewDelegate {
                implicitHeight: 28

                background: Rectangle {
                    color: selected
                           ? palette.highlight
                           : (current ? palette.midlight : "transparent")
                }


                onCurrentChanged: {
                    console.log("current:", row, current)
                }

                onSelectedChanged: {
                    console.log("selected:", row, selected)
                }
                TapHandler {
                    onTapped: console.log("tap", row)
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
                            color: palette.windowText
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