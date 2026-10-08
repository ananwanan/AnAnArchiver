pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolButton {
    id: root
    required property string label
    required property int column
    required property var entryModel
    readonly property bool sorted: entryModel.sortColumn === column
    signal sortRequested(int column)
    implicitHeight: 36
    leftPadding: 0
    rightPadding: 4
    onClicked: sortRequested(column)
    Accessible.name: label
    contentItem: RowLayout {
        spacing: 6
        Label {
            Layout.fillWidth: true
            text: root.label
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: root.sorted ? root.palette.highlight : root.palette.text
            opacity: root.sorted ? 1 : 0.7
        }
        AppIcon {
            name: "chevron"
            visible: root.sorted
            rotation: root.entryModel.sortDescending ? 180 : 0
            implicitWidth: 12
            implicitHeight: 12
        }
    }
    background: Rectangle {
        radius: 4
        color: root.hovered ? root.palette.button : "transparent"
    }
}
