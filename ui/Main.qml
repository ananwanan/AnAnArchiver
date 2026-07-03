import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "./pages"

ApplicationWindow {

    id: window

    width: 1280
    height: 760

    visible: true

    title: qsTr("安安解压")

    minimumWidth: 1000
    minimumHeight: 650

    color: "#F5F6F8"

    HomePage{
        anchors.fill: parent
    }

}