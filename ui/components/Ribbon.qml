import QtQuick
import QtQuick.Controls
import "dialogs"

MenuBar{

    id: root

    AboutDialog {
        id: aboutDialog
    }

    Menu{
        title:qsTr("文件")

        Action{text: qsTr("打开")}
        Action{text: qsTr("关闭")}
        MenuSeparator{}
        Action{
            text: qsTr("退出")
            onTriggered: Qt.exit(0)
        }
    }

    Menu{
        title: qsTr("工具")
    }

    Menu{
        title: qsTr("设置")
    }

    Menu{
        title: qsTr("帮助")

        Action{
            text: qsTr("关于")
            onTriggered: aboutDialog.open()
        }
    }

}