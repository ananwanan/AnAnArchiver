pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Dialog {
    id: root
    required property var hostWindow
    property string message: ""
    property string details: ""
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, hostWindow.width - 48)
    height: Math.min(450, hostWindow.height - 48)
    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            AppIcon { name: "warning"; Layout.alignment: Qt.AlignTop; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Label {
                Layout.fillWidth: true
                text: root.message
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 4
                elide: Text.ElideRight
                font.weight: Font.DemiBold
                Accessible.name: text
            }
        }
        Label {
            text: qsTr("Error details")
            textFormat: Text.PlainText
            opacity: 0.65
            font.pixelSize: 12
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: detailText
                objectName: "errorDetailsText"
                text: root.message + (root.details.length > 0 ? "\n\n" + root.details : "")
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                Accessible.name: qsTr("Error details")
            }
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "copyErrorDetails"
            text: qsTr("Copy details")
            icon.source: "../icons/copy.svg"
            icon.color: palette.buttonText
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: { detailText.selectAll(); detailText.copy(); detailText.deselect() }
        }
        Button { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        onAccepted: root.accept()
    }
}
