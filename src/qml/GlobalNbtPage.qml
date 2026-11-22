pragma ComponentBehavior: Bound

import BedrockMap 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "controls"

Item {
    id: page
    property var section
    property string itemFilter: ""
    property bool itemFilterRegex: false
    property var document: section ? section.document : null
    readonly property var structurePreview: section ? section.structurePreview : null
    readonly property bool hasStructurePreview: !!structurePreview
    readonly property var mapPreview: section ? section.mapPreview : null
    readonly property bool hasMapPreview: !!mapPreview
    readonly property bool singleRecord: !!document && document.singleRecord
    readonly property bool compact: Theme.compact
    property bool detailsHidden: false

    onSectionChanged: detailsHidden = !!section && (!!section.structurePreview || !!section.mapPreview)

    function reportMapChange(controller, ok) {
        if (!ok)
            App.notifications.error(controller.lastError)
    }
    function openMapImageSave() {
        const recordIndex = page.document ? page.document.currentIndex : -1
        saveMapImageDialog.owner = page.mapPreview
        saveMapImageDialog.recordIndex = recordIndex
        const fileName = "map-image-" + (recordIndex >= 0 ? recordIndex + 1 : "current")
        saveMapImageDialog.selectedFile = FileDialogSupport.selectedUrlWithSuffix(saveMapImageDialog, fileName, "png")
        saveMapImageDialog.open()
    }

    FileUrlDialog {
        id: saveMapImageDialog
        property var owner: null
        property int recordIndex: -1
        title: qsTr("Save image")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: FileDialogSupport.pngFilters
        onAccepted: {
            if (owner && owner === page.mapPreview && page.document && page.document.currentIndex === recordIndex) {
                if (owner.saveImage(selectedUrl))
                App.notifications.info(qsTr("Save successful"))
                else
                AppErrorReporter.report(qsTr("Save failed"), owner)
            }
            owner = null
        }
        onRejected: owner = null
    }

    FileUrlDialog {
        id: replaceMapImageDialog
        property var owner: null
        property int recordIndex: -1
        title: qsTr("Modify image")
        fileMode: FileDialog.OpenFile
        nameFilters: FileDialogSupport.imageFilters
        onAccepted: {
            if (owner && owner === page.mapPreview && page.document && page.document.currentIndex === recordIndex) {
                if (owner.replaceImage(selectedUrl))
                App.notifications.info(qsTr("Import successful"))
                else
                AppErrorReporter.report(qsTr("Import failed"), owner)
            }
            owner = null
        }
        onRejected: owner = null
    }

    Connections {
        target: page.document
        function onCurrentIndexChanged() {
            saveMapImageDialog.close()
            replaceMapImageDialog.close()
        }
        function onItemsChanged() {
            saveMapImageDialog.close()
            replaceMapImageDialog.close()
        }
    }

    NbtDocumentActions {
        id: documentActions
        document: page.document
        allowReplace: page.singleRecord
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.gapSmall
        spacing: Theme.gapSmall

        NbtToolbar {
            Layout.fillWidth: true
            document: page.document
            compact: page.compact
            allowSave: false
            allowClose: false
            allowImport: !page.singleRecord
            allowReplace: page.singleRecord
            allowAdd: !page.singleRecord
            allowClear: !page.singleRecord
            showSearch: !page.singleRecord
            regexSearch: page.itemFilterRegex
            allowDetailsToggle: !page.singleRecord
            detailsHidden: page.detailsHidden
            onDetailsToggleRequested: page.detailsHidden = !page.detailsHidden
            searchText: page.itemFilter
            onRegexSearchToggleRequested: page.itemFilterRegex = !page.itemFilterRegex
            onImportRequested: documentActions.openImport()
            onReplaceRequested: documentActions.openReplace()
            onExportRequested: documentActions.openExport()
            onClearRequested: documentActions.openClear()
            onSearchTextEdited: function (text) {
                page.itemFilter = text
            }
        }

        SplitView {
            id: panes
            readonly property real editorMinimumWidth: Theme.ui(page.detailsHidden ? 200 : 420)
            readonly property real previewMinimumWidth: Theme.ui(320)
            readonly property bool previewBeside: (page.hasStructurePreview || page.hasMapPreview) && width
                                                  >= editorMinimumWidth + previewMinimumWidth + Theme.gapSmall && width
                                                  >= height * 1.1
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: previewBeside ? Qt.Horizontal : Qt.Vertical
            handle: AppSplitHandle {
                orientation: panes.orientation
            }

            NbtEditorSurface {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumWidth: panes.previewBeside ? panes.editorMinimumWidth : 0
                SplitView.minimumHeight: panes.previewBeside ? 0 : Math.min(Theme.ui(160), panes.height * 0.4)
                document: page.document
                compact: page.compact
                filterText: page.itemFilter
                filterRegex: page.itemFilterRegex
                itemListTitle: qsTr("Records")
                showItemList: !page.singleRecord
                detailsHidden: page.detailsHidden
            }

            VoxelView {
                visible: page.hasStructurePreview
                SplitView.fillWidth: !panes.previewBeside
                SplitView.minimumWidth: panes.previewBeside ? panes.previewMinimumWidth : 0
                SplitView.minimumHeight: panes.previewBeside ? 0 : Math.min(Theme.ui(180), panes.height * 0.5)
                SplitView.preferredWidth: panes.width * (page.detailsHidden ? 0.65 : 0.45)
                SplitView.preferredHeight: panes.height * 0.5
                controller: page.structurePreview
            }

            AppCard {
                visible: page.hasMapPreview
                SplitView.fillWidth: !panes.previewBeside
                SplitView.minimumWidth: panes.previewBeside ? panes.previewMinimumWidth : 0
                SplitView.minimumHeight: panes.previewBeside ? 0 : Math.min(Theme.ui(180), panes.height * 0.5)
                SplitView.preferredWidth: panes.width * (page.detailsHidden ? 0.65 : 0.45)
                SplitView.preferredHeight: panes.height * 0.5

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.gapSmall
                    spacing: Theme.gapMedium

                    Item {
                        implicitWidth: 128
                        implicitHeight: 128
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 0
                        clip: true

                        Image {
                            id: mapImage
                            anchors.fill: parent
                            anchors.margins: Theme.gapSmall
                            source: page.mapPreview ? page.mapPreview.imageSource : ""
                            fillMode: Image.PreserveAspectFit
                            smooth: false
                            cache: false
                        }

                        Rectangle {
                            anchors.centerIn: mapImage
                            width: mapImage.paintedWidth + 2
                            height: mapImage.paintedHeight + 2
                            visible: mapImage.status === Image.Ready
                            color: "transparent"
                            border.width: Theme.borderWidth
                            border.color: Theme.muted
                        }

                        Text {
                            anchors.centerIn: parent
                            width: Math.max(0, parent.width - Theme.gapSmall * 2)
                            visible: mapImage.status === Image.Null || mapImage.status === Image.Error
                            text: qsTr("No map image available")
                            color: Theme.muted
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        uniformCellSizes: true
                        spacing: Theme.gapSmall

                        AppButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: qsTr("Save image")
                            compact: page.compact
                            enabled: !!page.mapPreview && page.mapPreview.hasImage
                            onClicked: page.openMapImageSave()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: qsTr("Modify image")
                            compact: page.compact
                            enabled: !!page.mapPreview && !!page.document && page.document.currentIndex >= 0 &&
                                     !page.document.readOnly
                            onClicked: {
                                replaceMapImageDialog.owner = page.mapPreview
                                replaceMapImageDialog.recordIndex = page.document.currentIndex
                                replaceMapImageDialog.open()
                            }
                        }
                        AppButton {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: qsTr("Lock map image")
                            compact: page.compact
                            enabled: !!page.mapPreview && !!page.document && page.document.currentIndex >= 0 &&
                                     !page.document.readOnly && !page.mapPreview.locked
                            onClicked: page.reportMapChange(page.mapPreview, page.mapPreview.lockMap())
                        }
                    }
                }
            }
        }
    }
}
