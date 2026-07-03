import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar{

    height: 6

    RowLayout{

        anchors.fill: parent

        anchors.margins:8

        Label{
            text:qsTr("就绪")
        }

        Item{
            Layout.fillWidth:true
        }

        Label{
            text:qsTr("0 个文件")
        }

    }

}