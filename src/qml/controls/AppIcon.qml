import BedrockMap 1.0
import QtQuick
import QtQuick.Effects

Item {
    id: root
    property string name
    property string fallback
    property color iconColor: Theme.text
    property real iconSize: Theme.iconSize

    implicitWidth: iconSize
    implicitHeight: iconSize
    opacity: enabled ? 1.0 : 0.5

    Image {
        id: sourceImage
        width: root.iconSize
        height: root.iconSize
        source: root.name.length > 0 ? "qrc:/bedrockmap/icons/ui/" + root.name + ".svg" : ""
        sourceSize.width: Math.max(1, Math.round(root.iconSize * 2))
        sourceSize.height: Math.max(1, Math.round(root.iconSize * 2))
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: sourceImage
        visible: sourceImage.status === Image.Ready
        colorizationColor: root.iconColor
        colorization: 1.0
        // The source SVGs are black. Normalize them to white first because
        // colorization uses luminance, leaving black pixels black.
        brightness: 1.0
        autoPaddingEnabled: false
    }

    Text {
        anchors.centerIn: parent
        visible: sourceImage.status !== Image.Ready && root.fallback.length > 0
        text: root.fallback
        color: root.iconColor
        font.pixelSize: Math.max(8, root.iconSize)
    }
}
