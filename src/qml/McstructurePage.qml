import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import "controls"

Item {
    id: page
    property var session: null
    readonly property var controller: session ? session.controller : null
    readonly property bool stacked: Theme.compact
    readonly property bool portrait: height > width
    objectName: "mcstructurePage"

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    SplitView {
        id: panes
        anchors.fill: parent
        objectName: "mcstructurePanes"
        orientation: page.stacked ? Qt.Vertical : Qt.Horizontal
        handle: AppSplitHandle {
            orientation: SplitView.view.orientation
        }

        NbtEditorSurface {
            visible: !page.portrait
            SplitView.minimumWidth: page.stacked ? 0 : 180
            SplitView.minimumHeight: page.stacked ? 100 : 0
            SplitView.preferredWidth: panes.width * 0.45
            SplitView.preferredHeight: panes.height * 0.4
            objectName: "mcstructureNbtBrowser"
            document: page.controller ? page.controller.document : null
            readOnly: true
            showItemList: false
        }

        VoxelPreview {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumWidth: page.stacked ? 0 : 220
            SplitView.minimumHeight: page.stacked ? 180 : 0
            objectName: "mcstructurePreview"
            controller: page.controller
        }
    }
}
