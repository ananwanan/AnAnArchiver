import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import App 1.0

import "./pages"


ApplicationWindow {

    id: window

    width: 1280
    height: 760

    visible: true

    title: AppContext.windowTitle

    minimumWidth: 1000
    minimumHeight: 650

    color: "#F5F6F8"

    HomePage{
        anchors.fill: parent
    }

}