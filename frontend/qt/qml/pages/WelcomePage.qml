pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Pane {
    id: root
    required property Action openAction
    required property Action compressAction
    property var desktopController: null
    property bool mobile: false
    readonly property bool compact: height < 420 || width < 820
    readonly property bool narrow: width < 520
    objectName: "welcomePage"
    implicitWidth: 600
    implicitHeight: 400
    padding: compact ? 14 : Math.min(width * 0.04, 40)
    background: Rectangle { color: root.palette.base }
    ScrollView {
        id: welcomeScroll
        objectName: "welcomeScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
        width: welcomeScroll.availableWidth
        height: Math.max(welcomeScroll.availableHeight, implicitHeight)
        spacing: root.compact ? 8 : 16
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: root.compact ? 0 : 16
            spacing: root.narrow ? 12 : 18
            Rectangle {
                Layout.preferredWidth: root.compact ? 44 : 56
                Layout.preferredHeight: root.compact ? 44 : 56
                color: root.palette.alternateBase
                radius: 12
                AppIcon { anchors.centerIn: parent; name: "archive"; width: 30; height: 30 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Label {
                    objectName: "welcomeTitle"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Your archives, organized.")
                    textFormat: Text.PlainText
                    font.pixelSize: root.compact ? 22 : 30
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Label {
                    visible: !root.compact
                    Layout.fillWidth: true
                    text: qsTr("Open an archive or bring files together in a new one.")
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    opacity: 0.65
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: root.compact ? 0 : 8
            columns: root.narrow ? 1 : 3
            columnSpacing: 10
            rowSpacing: 8
            Button {
                objectName: "welcomeOpenButton"
                Layout.fillWidth: root.narrow
                Layout.minimumHeight: root.mobile ? 48 : 0
                action: root.openAction
                highlighted: true
                icon.source: "../icons/open.svg"
                icon.color: palette.highlightedText
            }
            Button {
                objectName: "welcomeCreateButton"
                Layout.fillWidth: root.narrow
                Layout.minimumHeight: root.mobile ? 48 : 0
                action: root.compressAction
                icon.source: "../icons/create.svg"
                icon.color: palette.buttonText
            }
            Item { Layout.fillWidth: true; visible: !root.narrow }
        }
        Label {
            visible: !root.mobile
            text: qsTr("Drop an archive to open it, or drop files and folders to create one.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0.65
            font.pixelSize: 12
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: root.compact ? 0 : 10; color: root.palette.mid; opacity: 0.2 }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Recent archives"); textFormat: Text.PlainText; font.weight: Font.DemiBold; Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap }
            ToolButton {
                objectName: "clearRecentArchives"
                text: qsTr("Clear history")
                Layout.minimumHeight: root.mobile ? 48 : 0
                enabled: recent.count > 0
                onClicked: root.desktopController.clearRecent()
            }
        }
        ListView {
            id: recent
            objectName: "recentArchives"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.mobile ? 120 : 60
            clip: true
            spacing: 4
            reuseItems: true
            model: root.desktopController ? root.desktopController.recentArchives : []
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: recentRow
                required property var modelData
                width: recent.width
                height: 60
                padding: 10
                onClicked: root.desktopController.openRecent(recentRow.modelData.path)
                Accessible.name: recentRow.modelData.path
                background: Rectangle { radius: 6; color: recentRow.hovered ? recentRow.palette.alternateBase : "transparent" }
                contentItem: RowLayout {
                    spacing: 12
                    AppIcon { name: "archive" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { Layout.fillWidth: true; text: recentRow.modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; font.weight: Font.Medium }
                        Label { Layout.fillWidth: true; text: recentRow.modelData.path; textFormat: Text.PlainText; elide: Text.ElideMiddle; opacity: 0.6; font.pixelSize: 12 }
                    }
                    ToolButton {
                        id: removeRecentButton
                        objectName: "removeRecentArchive"
                        display: AbstractButton.IconOnly
                        text: qsTr("Remove from history")
                        icon.source: "../icons/close.svg"
                        icon.color: palette.buttonText
                        Accessible.name: text
                        PlainToolTip { visible: removeRecentButton.hovered; text: removeRecentButton.text }
                        onClicked: root.desktopController.removeRecent(recentRow.modelData.path)
                    }
                }
                PlainToolTip { visible: recentRow.hovered; text: recentRow.modelData.path }
            }
            Label {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 24)
                visible: recent.count === 0
                text: qsTr("Recently opened archives will appear here.")
                textFormat: Text.PlainText
                opacity: 0.6
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
        Label {
            objectName: "welcomeShortcutHints"
            visible: !root.mobile
            Layout.fillWidth: true
            text: qsTr("Open  Ctrl+O     Create  Ctrl+N     Search  Ctrl+F     Tasks  Ctrl+J")
            textFormat: Text.PlainText
            font.pixelSize: 12
            opacity: 0.55
            wrapMode: Text.WordWrap
        }
        }
    }
}
