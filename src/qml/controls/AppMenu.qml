import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Window

Menu {
    id: root
    // Popup visibility and the generated submenu entry have separate lifetimes.
    property bool menuItemVisible: true

    width: Math.max(Theme.ui(190), contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : Theme.ui(190))
    height: contentItem ? Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Screen.height - Theme.ui(
                                       48)) : Theme.controlHeight
    padding: Theme.ui(6)
    margins: 0
    overlap: 1
    leftInset: 0
    topInset: 0
    rightInset: 0
    bottomInset: 0

    // A nested Menu becomes a submenu row in the parent Menu. Route that
    // generated row through the shared item styling; explicit AppMenuItem
    // children are still used directly.
    delegate: AppMenuItem {
        visible: !subMenu || subMenu.menuItemVisible
    }

    // Own the list layout instead of inheriting the platform style. Some
    // styles add their own row spacing, which makes generated submenu rows
    // appear farther from adjacent items.
    contentItem: ListView {
        implicitHeight: contentHeight
        model: root.contentModel
        currentIndex: root.currentIndex
        spacing: 0
        clip: true
        interactive: Window.window ? contentHeight + root.topPadding + root.bottomPadding > root.height : false

        ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }
}
