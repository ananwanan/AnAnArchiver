pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ScrollView {
    id: root
    required property var hostWindow

    function revealFocusedControl() {
        const control = root.hostWindow.activeFocusItem
        const viewport = root.contentItem as Flickable
        if (!control || !viewport)
            return
        let ancestor = control
        while (ancestor && ancestor !== viewport.contentItem)
            ancestor = ancestor.parent
        if (!ancestor)
            return
        const top = control.mapToItem(viewport.contentItem, 0, 0).y
        if (top < viewport.contentY)
            viewport.contentY = top
        else if (top + control.height > viewport.contentY + viewport.height)
            viewport.contentY = Math.max(0, top + control.height - viewport.height)
    }
}
