import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar{

    RowLayout{

        // anchors.fill: parent

        // anchors.margins: 8

        // spacing:8

        ToolButton{
            text:"📂 打开"
        }

        ToolButton{
            text:"📦 压缩"
        }

        ToolButton{
            text:"📤 解压"
        }

        ToolButton{
            text:"🗑 删除"
        }
    }

}