import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Dialog {
    id: root
    property int mode: 0
    property int sourceRow: -1
    property int tagType: 1
    property int lockedChildType: -1
    property bool typeLocked: false
    property string tagKey: ""
    property string tagValue: ""
    readonly property var typeIds: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]
    readonly property var typeNames: ["Byte", "Short", "Int", "Long", "Float", "Double", "Byte Array", "String", "List",
        "Compound", "Int Array", "Long Array"]
    signal commitRequested(int type, string key, string value)

    component TypeLabel: Item {
        id: labelItem
        property string label
        property int tagType: 0
        property real leftPadding: 0
        property real rightPadding: 0
        property alias font: labelText.font

        implicitWidth: leftPadding + rightPadding + labelText.implicitWidth + Theme.iconSmallSize + Theme.ui(6)
        implicitHeight: Math.max(Theme.iconSmallSize, labelText.implicitHeight)

        Row {
            anchors.fill: parent
            anchors.leftMargin: labelItem.leftPadding
            anchors.rightMargin: labelItem.rightPadding
            spacing: Theme.ui(6)

            NbtTagTypeIcon {
                anchors.verticalCenter: parent.verticalCenter
                tagType: labelItem.tagType
            }
            Text {
                id: labelText
                width: Math.max(0, parent.width - x)
                anchors.verticalCenter: parent.verticalCenter
                text: labelItem.label
                color: Theme.text
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    parent: Overlay.overlay
    width: Math.min(440, parent ? parent.width - 32 : 440)
    height: parent ? Math.min(implicitHeight, parent.height - 32) : implicitHeight
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Theme.gapSmall
    title: mode === 0 ? qsTr("Add NBT tag") : qsTr("Edit NBT tag")

    function openCreate(row, childType) {
        mode = 0
        sourceRow = row
        lockedChildType = childType
        typeLocked = childType >= 0
        tagType = childType >= 0 ? childType : 1
        tagKey = ""
        tagValue = ""
        open()
        keyField.forceActiveFocus()
    }
    function openModify(row, info) {
        mode = 1
        sourceRow = row
        lockedChildType = -1
        typeLocked = true
        tagType = info.type
        tagKey = info.key
        tagValue = String(info.value)
        open()
        keyField.forceActiveFocus()
    }
    function typeIndexForId(id) {
        for (let i = 0; i < typeIds.length; ++i)
            if (typeIds[i] === id)
                return i
        return 0
    }

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    // The Basic style supplies a square-backed header by default. Keep the
    // title but use a transparent header so the rounded dialog background is
    // visible at the top corners.
    header: Item {
        implicitHeight: Theme.controlHeight + Theme.gapSmall
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapSmall
            anchors.right: parent.right
            anchors.rightMargin: Theme.gapSmall
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontLabel
            elide: Text.ElideRight
        }
    }

    contentItem: ColumnLayout {
        spacing: Math.max(2, Math.round(Theme.gapSmall / 2))
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Key")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                Layout.preferredWidth: 54
            }
            AppTextField {
                id: keyField
                Layout.fillWidth: true
                text: root.tagKey
                onTextEdited: root.tagKey = text
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !root.typeLocked
            Text {
                text: qsTr("Type")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                Layout.preferredWidth: 54
            }
            AppComboBox {
                id: typeCombo
                Layout.fillWidth: true
                model: root.typeNames
                currentIndex: root.typeIndexForId(root.tagType)
                enabled: !root.typeLocked
                onActivated: root.tagType = root.typeIds[currentIndex]
                contentItem: TypeLabel {
                    leftPadding: Theme.ui(9)
                    rightPadding: Theme.iconSmallSize + Theme.ui(8)
                    label: typeCombo.displayText
                    tagType: root.typeIds[typeCombo.currentIndex]
                    font: typeCombo.font
                }
                delegate: ItemDelegate {
                    id: typeOption
                    required property int index
                    required property var modelData
                    width: typeCombo.width
                    implicitHeight: Theme.controlHeight
                    highlighted: typeCombo.highlightedIndex === index
                    text: modelData
                    contentItem: TypeLabel {
                        leftPadding: Theme.ui(9)
                        rightPadding: Theme.ui(9)
                        label: typeOption.text
                        tagType: root.typeIds[typeOption.index]
                        font.pixelSize: Theme.fontSubtitle
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: typeOption.highlighted ? Theme.surfaceAlt : "transparent"
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.tagType !== 9 && root.tagType !== 10
            Text {
                Layout.preferredWidth: 54
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Math.round(Theme.gapSmall / 2)
                text: qsTr("Value")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(64, Math.round(Theme.fontBody * 3.6))
                padding: 0
                clip: true
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceAlt
                }
                TextArea {
                    id: valueField
                    text: root.tagValue
                    enabled: root.tagType !== 9 && root.tagType !== 10
                    font.family: "JetBrains Mono"
                    font.pixelSize: Theme.fontBody
                    color: Theme.text
                    placeholderText: root.tagType === 9 || root.tagType === 10 ? qsTr(
                                                                                     "Container children are edited in the tree") :
                                                                                 qsTr("Enter a value; arrays accept spaces or commas")
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    onTextChanged: root.tagValue = text
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceAlt
                        border.width: Theme.borderWidth
                        border.color: valueField.activeFocus ? Theme.accent : Theme.border
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                primary: true
                text: root.mode === 0 ? qsTr("Add") : qsTr("Apply")
                onClicked: {
                    root.tagKey = keyField.text
                    root.tagValue = valueField.text
                    root.commitRequested(root.tagType, root.tagKey, root.tagValue)
                }
            }
        }
    }
}
