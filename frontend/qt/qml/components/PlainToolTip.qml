pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// Archive and filesystem names are data, never rich text or image references.
ToolTip {
    id: control
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        wrapMode: Text.Wrap
        color: control.palette.toolTipText
    }
}
