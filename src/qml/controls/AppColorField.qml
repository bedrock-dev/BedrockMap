import BedrockMap 1.0
import QtQuick
import QtQuick.Dialogs

Row {
    id: root
    property string value
    signal edited(string color)

    width: 224
    height: Theme.controlHeight
    spacing: 6

    AppTextField {
        width: root.width - pickerButton.width - root.spacing
        height: root.height
        text: root.value
        placeholderText: qsTr("Color")
        onEditingFinished: root.edited(text)
    }

    AppIconButton {
        id: pickerButton
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: Theme.touch ? 36 : 28
        iconName: "color_picker"
        label: qsTr("Choose color")
        onClicked: {
            colorDialog.selectedColor = root.value.length > 0 ? root.value : "#ffffff"
            colorDialog.open()
        }
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("Choose color")
        options: ColorDialog.ShowAlphaChannel
        onAccepted: root.edited(selectedColor.toString())
    }
}
