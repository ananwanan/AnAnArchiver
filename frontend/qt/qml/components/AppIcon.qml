pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property string name
    property bool onDarkSurface: ApplicationWindow.window
        ? ApplicationWindow.window.palette.window.r + ApplicationWindow.window.palette.window.g
            + ApplicationWindow.window.palette.window.b < 1.5 : false
    implicitWidth: 20
    implicitHeight: 20
    Image {
        anchors.fill: parent
        source: Qt.resolvedUrl("../icons/" + root.name + (root.onDarkSurface ? "-dark" : "") + ".svg")
        sourceSize: Qt.size(root.width, root.height)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
