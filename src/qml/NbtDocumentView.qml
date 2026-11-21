pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Item {
    id: root
    property var document
    property bool compact: Theme.compact
    property string itemListTitle: qsTr("Records")
    property bool showItemList: true
    property bool detailsHidden: false
    property string filterText
    property bool filterRegex: false
    property real itemListWidth: 190
    property int selectedRow: -1
    property var filteredItems: filterItems()
    readonly property int compactToolbarHeight: Theme.controlHeight
    readonly property int rootCount: root.document ? root.document.rootCount : 0
    // Record navigation depends on the available editor width, independently
    // of the shell mode and font scaling used by the toolbars.
    readonly property bool singleColumn: !detailsHidden && width < 420
    signal editRequested(int row)
    signal contextRequested(int row, real sceneX, real sceneY)

    component RecordLabel: Item {
        id: recordLabel
        property string label
        property string icon
        property real leftPadding: 0
        property real rightPadding: 0
        property color textColor: Theme.text
        property alias font: recordText.font
        readonly property bool hasIcon: icon.length > 0
        readonly property bool usesThemeIcon: hasIcon && !icon.startsWith("qrc:/")
        readonly property real iconSpace: hasIcon ? Theme.iconSmallSize + Theme.gapMedium : 0
        implicitHeight: Math.max(Theme.iconSmallSize, recordText.implicitHeight)
        implicitWidth: leftPadding + rightPadding + recordText.implicitWidth + iconSpace

        Row {
            anchors.fill: parent
            anchors.leftMargin: recordLabel.leftPadding
            anchors.rightMargin: recordLabel.rightPadding
            spacing: Theme.gapMedium

            AppIcon {
                id: themedRecordIcon
                width: Theme.iconSmallSize
                height: width
                anchors.verticalCenter: parent.verticalCenter
                visible: recordLabel.usesThemeIcon
                name: visible ? recordLabel.icon : ""
                iconSize: width
                iconColor: recordLabel.textColor
            }
            Image {
                id: recordIcon
                width: Theme.iconSmallSize
                height: width
                anchors.verticalCenter: parent.verticalCenter
                visible: recordLabel.hasIcon && !recordLabel.usesThemeIcon
                source: visible ? recordLabel.icon : ""
                sourceSize.width: width * 2
                sourceSize.height: height * 2
                fillMode: Image.PreserveAspectFit
            }
            Text {
                id: recordText
                width: Math.max(0, parent.width - x)
                anchors.verticalCenter: parent.verticalCenter
                text: recordLabel.label
                color: recordLabel.textColor
                elide: Text.ElideRight
            }
        }
    }

    function clampItemListWidth(value) {
        return Math.max(120, Math.min(value, Math.max(120, width - 260)))
    }
    function filterItems() {
        const labels = root.document ? root.document.itemLabels : []
        const icons = root.document ? root.document.itemIcons : []
        const pattern = root.filterText.trim()
        const needle = pattern.toLowerCase()
        const result = []
        let expression = null
        if (root.filterRegex && pattern.length > 0) {
            try {
                expression = new RegExp(pattern, "i")
            } catch (error) {
                return result
            }
        }
        for (let i = 0; i < labels.length; ++i) {
            const label = String(labels[i])
            if (needle.length === 0 || (expression ? expression.test(label) : label.toLowerCase().indexOf(needle) >= 0))
                result.push({
                                label: labels[i],
                                icon: icons[i] || "",
                                sourceIndex: i
                            })
        }
        return result
    }
    function currentFilteredIndex() {
        if (!root.document)
            return -1
        for (let i = 0; i < root.filteredItems.length; ++i)
            if (root.filteredItems[i].sourceIndex === root.document.currentIndex)
                return i
        return -1
    }
    function openAdvancedRecordSelection() {
        advancedRecordDialog.openFor(root.document, root.filteredItems.map(item => ({
            text: item.label,
            value: item.sourceIndex
        })))
    }
    onWidthChanged: itemListWidth = clampItemListWidth(itemListWidth)

    Connections {
        target: root.document
        function onCurrentIndexChanged() {
            root.selectedRow = -1
        }
    }
    Connections {
        target: root.document ? root.document.treeModel : null
        function onModelReset() {
            root.selectedRow = -1
        }
        function onRowsInserted(parent, first, last) {
            if (root.selectedRow >= first)
                root.selectedRow += last - first + 1
        }
        function onRowsRemoved(parent, first, last) {
            if (root.selectedRow > last)
                root.selectedRow -= last - first + 1
            else if (root.selectedRow >= first)
                root.selectedRow = -1
        }
    }

    NbtRecordSelectionDialog {
        id: advancedRecordDialog
        title: qsTr("Select records")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.gapSmall

        RowLayout {
            visible: root.singleColumn && root.showItemList && !!root.document && root.document.hasDocument
                     && root.document.rootCount > 0
            Layout.fillWidth: true
            Layout.preferredHeight: root.compactToolbarHeight
            Layout.minimumHeight: root.compactToolbarHeight
            Layout.maximumHeight: root.compactToolbarHeight
            spacing: Theme.gapSmall

            AppComboBox {
                id: recordSelector
                visible: root.showItemList && !!root.document && root.document.rootCount > 0
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: 100
                Layout.preferredHeight: root.compactToolbarHeight
                Layout.minimumHeight: root.compactToolbarHeight
                Layout.maximumHeight: root.compactToolbarHeight
                Layout.alignment: Qt.AlignVCenter
                model: root.filteredItems
                textRole: "label"
                currentIndex: root.currentFilteredIndex()
                contentItem: RecordLabel {
                    leftPadding: Theme.gapLarge
                    rightPadding: Theme.gapLarge
                    label: recordSelector.displayText
                    icon: recordSelector.currentIndex >= 0 && recordSelector.currentIndex < root.filteredItems.length
                          ? root.filteredItems[recordSelector.currentIndex].icon : ""
                    font: recordSelector.font
                }
                delegate: ItemDelegate {
                    id: recordOption
                    required property int index
                    required property var modelData
                    width: recordSelector.width
                    implicitHeight: Theme.controlHeight
                    highlighted: recordSelector.highlightedIndex === index
                    text: modelData.label
                    contentItem: RecordLabel {
                        leftPadding: Theme.gapLarge
                        rightPadding: Theme.gapLarge
                        label: recordOption.text
                        icon: recordOption.modelData.icon
                        font.pixelSize: Theme.fontMedium
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: recordOption.highlighted ? Theme.surfaceAlt : "transparent"
                    }
                }
                onActivated: if (root.document && currentIndex >= 0)
                root.document.selectItem(root.filteredItems[currentIndex].sourceIndex)
            }
            AppIconButton {
                iconName: "tools"
                label: qsTr("Select multiple records")
                buttonSize: root.compactToolbarHeight
                Layout.preferredWidth: controlSize
                Layout.preferredHeight: controlSize
                onClicked: root.openAdvancedRecordSelection()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Text {
                anchors.centerIn: parent
                visible: !root.document || root.document.rootCount === 0
                text: qsTr("No records")
                color: Theme.muted
                font.pixelSize: Theme.fontMedium
            }

            RowLayout {
                anchors.fill: parent
                spacing: Theme.gapTiny
                visible: root.document && root.document.rootCount > 0

                AppCard {
                    visible: !root.singleColumn && root.showItemList
                    Layout.fillWidth: root.detailsHidden
                    Layout.preferredWidth: root.itemListWidth
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.gapLarge
                        spacing: Theme.gapSmall
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gapSmall

                            Text {
                                Layout.fillWidth: true
                                text: root.itemListTitle + " (" + root.rootCount + ")"
                                color: Theme.text
                                font.pixelSize: Theme.fontMedium
                                elide: Text.ElideRight
                            }
                            AppIconButton {
                                iconName: "tools"
                                label: qsTr("Select multiple records")
                                buttonSize: Theme.iconButtonSmallSize
                                Layout.preferredWidth: controlSize
                                Layout.preferredHeight: controlSize
                                onClicked: root.openAdvancedRecordSelection()
                            }
                        }
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: root.filteredItems
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }
                            delegate: Rectangle {
                                id: recordRow
                                required property int index
                                required property var modelData
                                width: ListView.view.width
                                height: Theme.menuItemHeight
                                radius: Theme.radiusSmall
                                readonly property bool current: root.document && modelData.sourceIndex
                                                                === root.document.currentIndex
                                color: current ? Theme.accentSoft : (itemMouse.pressed ? Theme.surfaceAlt :
                                                                                         "transparent")
                                RecordLabel {
                                    anchors.fill: parent
                                    leftPadding: Theme.gapMedium
                                    rightPadding: Theme.gapMedium
                                    label: recordRow.modelData.label
                                    icon: recordRow.modelData.icon
                                    textColor: recordRow.current ? Theme.accent : Theme.text
                                    font.family: "JetBrains Mono"
                                    font.pixelSize: Theme.fontMedium
                                }
                                MouseArea {
                                    id: itemMouse
                                    anchors.fill: parent
                                    onClicked: if (root.document)
                                    root.document.selectItem(recordRow.modelData.sourceIndex)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: splitter
                    visible: !root.detailsHidden && !root.singleColumn && root.showItemList
                    Layout.preferredWidth: 6
                    Layout.fillHeight: true
                    color: "transparent"
                    HoverHandler {
                        cursorShape: Qt.SplitHCursor
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: Theme.ui(3)
                        height: Math.max(1, parent.height - Theme.gapSmall * 2)
                        radius: width / 2
                        color: drag.active || hover.hovered ? Theme.accent : Theme.border
                    }
                    HoverHandler {
                        id: hover
                    }
                    DragHandler {
                        id: drag
                        target: null
                        property real startWidth: 0
                        onActiveChanged: if (active)
                        startWidth = root.itemListWidth
                        onTranslationChanged: root.itemListWidth = root.clampItemListWidth(startWidth + translation.x)
                    }
                }

                AppCard {
                    visible: !root.detailsHidden
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: root.compact ? 0 : Theme.radiusLarge
                    color: root.compact ? "transparent" : Theme.surface
                    border.width: root.compact ? 0 : Theme.borderWidth
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: root.compact ? 0 : Theme.gapSmall
                        spacing: Theme.gapSmall
                        GridLayout {
                            id: nodeToolbar
                            readonly property bool twoColumn: width >= 120 * Theme.metricScale
                                                              + treeActions.implicitWidth + columnSpacing
                            Layout.fillWidth: true
                            columns: twoColumn ? 2 : 1
                            columnSpacing: Theme.gapSmall
                            rowSpacing: Theme.gapSmall

                            AppTextField {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 0
                                Layout.preferredHeight: root.compactToolbarHeight
                                placeholderText: qsTr("Search nodes")
                                text: root.document ? root.document.treeModel.searchText : ""
                                onTextEdited: if (root.document)
                                root.document.treeModel.searchText = text
                            }
                            RowLayout {
                                id: treeActions
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                spacing: Theme.gapSmall

                                AppCheckBox {
                                    text: qsTr("Hex")
                                    checked: root.document ? root.document.treeModel.hexMode : false
                                    enabled: root.document && root.document.hasDocument
                                    onToggled: if (root.document)
                                    root.document.treeModel.hexMode = checked
                                }
                                AppIconButton {
                                    iconText: "▾"
                                    label: qsTr("Expand all")
                                    buttonSize: root.compactToolbarHeight
                                    Layout.preferredWidth: controlSize
                                    Layout.preferredHeight: controlSize
                                    enabled: root.document && root.document.hasDocument
                                    onClicked: if (root.document)
                                    root.document.treeModel.expandAll()
                                }
                                AppIconButton {
                                    iconText: "▸"
                                    label: qsTr("Collapse all")
                                    buttonSize: root.compactToolbarHeight
                                    Layout.preferredWidth: controlSize
                                    Layout.preferredHeight: controlSize
                                    enabled: root.document && root.document.hasDocument
                                    onClicked: if (root.document)
                                    root.document.treeModel.collapseAll()
                                }
                            }
                        }
                        NbtTreeView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: root.document ? root.document.treeModel : null
                            selectedRow: root.selectedRow
                            onSelectedRowChanged: root.selectedRow = selectedRow
                            onEditRequested: function (row) {
                                root.editRequested(row)
                            }
                            onContextRequested: function (row, sceneX, sceneY) {
                                root.contextRequested(row, sceneX, sceneY)
                            }
                        }
                    }
                }
            }
        }
    }
}
