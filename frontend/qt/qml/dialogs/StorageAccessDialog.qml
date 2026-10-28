pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Dialog {
    id: root
    objectName: "storageAccessDialog"
    required property var hostWindow
    property var controller: null
    title: qsTr("File access")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(440, hostWindow.width - 32)
    height: Math.min(implicitHeight, hostWindow.height - 32)
    visible: controller ? controller.promptVisible : false
    onRejected: if (controller) controller.dismissPrompt()

    contentItem: ScrollView {
        implicitHeight: explanation.implicitHeight
        contentWidth: availableWidth
        clip: true
        Label {
            id: explanation
            objectName: "storageAccessExplanation"
            width: parent.width
            text: root.controller && root.controller.granted
                ? qsTr("File access is enabled. You can browse, create and extract archives in shared storage.")
                : root.controller && root.controller.requiresAllFilesAccess
                    ? qsTr("Allow LiteArc to manage files so it can browse, create and extract archives in shared storage. On the next Android settings screen, enable Allow access to manage all files. You can change this permission later in system settings.")
                    : qsTr("Allow LiteArc to read and write shared storage so it can browse, create and extract archives. Android will ask for file access on the next screen.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "deferStorageAccess"
            implicitHeight: Math.max(48, implicitContentHeight + topPadding + bottomPadding)
            text: root.controller && root.controller.granted ? qsTr("Close") : qsTr("Not now")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            objectName: "grantStorageAccess"
            implicitHeight: Math.max(48, implicitContentHeight + topPadding + bottomPadding)
            visible: root.controller && !root.controller.granted
            enabled: root.controller && !root.controller.requestInProgress
            text: root.controller && root.controller.requiresAllFilesAccess ? qsTr("Open settings") : qsTr("Allow access")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        onAccepted: if (root.controller) root.controller.requestAccess()
        onRejected: if (root.controller) root.controller.dismissPrompt()
    }
}
