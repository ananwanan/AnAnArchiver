import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../components"

ColumnLayout{

    anchors.fill: parent

    spacing:0
    

    Ribbon{
        Layout.fillWidth: true
    }

    Toolbar{
        Layout.fillWidth: true
    }

    FileTable{
        Layout.fillWidth:true
        Layout.fillHeight:true
    }

    StatusBar{
        Layout.fillWidth: true
    }

}