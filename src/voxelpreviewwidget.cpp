#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <memory>

#include "color.h"
#include "palette.h"
#include "voxelwidget.h"

namespace {
    // The selection is stored in model-local voxel coordinates; the panel edits
    // world coordinates so the numbers match the in-game positions.
    QVector3D toVector(const bl::block_pos& pos) {
        return {static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)};
    }

    QSpinBox* makeCoordinateBox(QWidget* parent) {
        auto* box = new QSpinBox(parent);
        box->setKeyboardTracking(false);  // only commit a typed value once it is complete
        box->setMinimumWidth(72);
        return box;
    }

    VoxelPreviewWidget::VoxelGrid buildVoxelDataFromMcstructure(const bl::mcstructure& structure) {
        const int sx = structure.size_x();
        const int sy = structure.size_y();
        const int sz = structure.size_z();

        std::vector<std::vector<std::vector<Voxel>>> data;
        data.resize(sy);
        for (auto& yLayer : data) {
            yLayer.resize(sx);
            for (auto& xRow : yLayer) {
                xRow.resize(sz, Voxel(QColor(255, 255, 255, 0), true));
            }
        }

        if (sx <= 0 || sy <= 0 || sz <= 0 || structure.palette_size() == 0) {
            return data;
        }

        for (int x = 0; x < sx; ++x) {
            for (int y = 0; y < sy; ++y) {
                for (int z = 0; z < sz; ++z) {
                    const auto* block = structure.block_at(x, y, z);
                    if (!block) continue;
                    if (block->name == "minecraft:air" || block->name == "minecraft:unknown") continue;

                    const auto baseColor = bl::get_block_by_name_tag(block->name);
                    // mcstructure files do not carry biome data; use plains as a stable preview biome.
                    const auto c = bl::blend_color_with_biome(block->name, baseColor, bl::biome::plains);
                    data[y][x][z] = Voxel(QColor(c.r, c.g, c.b, c.a), c.a < 255);
                }
            }
        }

        return data;
    }
}  // namespace

VoxelPreviewWidget::VoxelPreviewWidget(QWidget* parent) : QWidget(parent) {
    voxelWidget_ = new VoxelWidget(this);
    voxelWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    bar_ = new QProgressBar(this);
    bar_->hide();

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(voxelWidget_);

    auto* panel = new QWidget(splitter);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    panelLayout->setSpacing(8);
    panelLayout->addWidget(buildModelPanel());
    panelLayout->addWidget(buildSelectionPanel());
    panelLayout->addWidget(buildViewPanel());
    panelLayout->addWidget(buildMcstructurePanel());
    panelLayout->addWidget(buildGlbPanel());
    panelLayout->addStretch();
    import_bar_ = buildImportBar();
    panelLayout->addWidget(import_bar_);

    auto* panelScroll = new QScrollArea(splitter);
    panelScroll->setWidgetResizable(true);
    panelScroll->setWidget(panel);
    panelScroll->setMinimumWidth(260);
    splitter->addWidget(panelScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({900, 300});

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter, 1);
    layout->addWidget(bar_, 0);
    setLayout(layout);
    setGeometry({0, 0, 1200, 900});

    connect(voxelWidget_, &VoxelWidget::selectionChanged, this, [this](VoxelSelection) {
        refreshSelectionFields();
        if (import_mode_) voxelWidget_->setPreviewOffset(voxelWidget_->getSelection().minimum);
    });
    connect(voxelWidget_, &VoxelWidget::selectionEnabledChanged, this, [this](bool enabled) {
        if (selection_group_ && selection_group_->isChecked() != enabled) {
            QSignalBlocker blocker(selection_group_);
            selection_group_->setChecked(enabled);
        }
        refreshSelectionFields();
    });
    connect(voxelWidget_, &VoxelWidget::viewOptionsChanged, this, &VoxelPreviewWidget::refreshViewOptions);

    connect(&this->chunk_task_, &GuiTaskRunner::progressChanged, this, [this](int value, const QString&) { bar_->setValue(value); });
    connect(&this->chunk_task_, &GuiTaskRunner::finished, this, [this]() {
        bar_->hide();
        setVoxelData(std::move(pending_chunk_result_.data), pending_chunk_result_.origin);
    });
    connect(&this->chunk_task_, &GuiTaskRunner::failed, this, [this](const QString&) { bar_->hide(); });
    connect(&this->mcstructure_task_, &GuiTaskRunner::finished, this, [this]() {
        bar_->hide();
        setVoxelData(std::move(pending_mcstructure_result_.data), pending_mcstructure_result_.origin);
    });
    connect(&this->mcstructure_task_, &GuiTaskRunner::failed, this, [this](const QString&) { bar_->hide(); });
}

QVector3D VoxelPreviewWidget::worldOrigin() const { return toVector(voxel_origin_); }

QWidget* VoxelPreviewWidget::buildModelPanel() {
    auto* group = new QGroupBox(tr("voxelPreviewWidget.modelGroup"), this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 6, 8, 6);

    model_info_label_ = new QLabel(group);
    model_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont font;
    font.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("Microsoft YaHei"), QStringLiteral("Microsoft YaHei UI")});
    model_info_label_->setFont(font);
    layout->addWidget(model_info_label_);
    refreshModelInfo();
    return group;
}

QWidget* VoxelPreviewWidget::buildSelectionPanel() {
    selection_group_ = new QGroupBox(tr("voxelPreviewWidget.selectionGroup"), this);
    selection_group_->setCheckable(true);
    selection_group_->setChecked(voxelWidget_->isSelectionEnabled());
    selection_group_->setToolTip(tr("voxelPreviewWidget.selection.tooltip"));

    auto* layout = new QGridLayout(selection_group_);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(4);
    const QString axisLabels[3] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis) {
        layout->addWidget(new QLabel(axisLabels[axis], selection_group_), 0, axis + 1);
    }
    layout->addWidget(new QLabel(tr("voxelPreviewWidget.selection.min"), selection_group_), 1, 0);
    layout->addWidget(new QLabel(tr("voxelPreviewWidget.selection.max"), selection_group_), 2, 0);
    for (int axis = 0; axis < 3; ++axis) {
        selection_min_boxes_[axis] = makeCoordinateBox(selection_group_);
        selection_max_boxes_[axis] = makeCoordinateBox(selection_group_);
        layout->addWidget(selection_min_boxes_[axis], 1, axis + 1);
        layout->addWidget(selection_max_boxes_[axis], 2, axis + 1);
        connect(selection_min_boxes_[axis], &QSpinBox::valueChanged, this, [this](int) { applySelectionFields(); });
        connect(selection_max_boxes_[axis], &QSpinBox::valueChanged, this, [this](int) { applySelectionFields(); });
    }

    selection_move_box_ = new QCheckBox(tr("voxelPreviewWidget.selection.moveMode"), selection_group_);
    selection_move_box_->setToolTip(tr("voxelPreviewWidget.selection.moveMode.tooltip"));
    connect(selection_move_box_, &QCheckBox::toggled, this, [this](bool checked) { voxelWidget_->setSelectionMoveMode(checked); });
    layout->addWidget(selection_move_box_, 3, 0, 1, 4);

    // A checkable group box disables its children while unchecked, which is
    // exactly the wanted behaviour for a disabled selection.
    connect(selection_group_, &QGroupBox::toggled, this, [this](bool enabled) { voxelWidget_->setSelectionEnabled(enabled); });
    refreshSelectionFields();
    return selection_group_;
}

QWidget* VoxelPreviewWidget::buildViewPanel() {
    auto* group = new QGroupBox(tr("voxelPreviewWidget.viewGroup"), this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    axes_box_ = new QCheckBox(tr("voxelPreviewWidget.view.axes"), group);
    ortho_box_ = new QCheckBox(tr("voxelPreviewWidget.view.ortho"), group);
    rotation_lock_box_ = new QCheckBox(tr("voxelPreviewWidget.view.lockRotation"), group);
    rotation_lock_box_->setToolTip(tr("voxelPreviewWidget.view.lockRotation.tooltip"));
    axes_box_->setChecked(voxelWidget_->isAxesVisible());
    ortho_box_->setChecked(voxelWidget_->isOrthoMode());
    rotation_lock_box_->setChecked(voxelWidget_->isRotationLocked());
    connect(axes_box_, &QCheckBox::toggled, this, [this](bool checked) { voxelWidget_->setAxesVisible(checked); });
    connect(ortho_box_, &QCheckBox::toggled, this, [this](bool checked) { voxelWidget_->setOrthoMode(checked); });
    connect(rotation_lock_box_, &QCheckBox::toggled, this, [this](bool checked) { voxelWidget_->setRotationLocked(checked); });
    auto* optionsRow = new QHBoxLayout();
    optionsRow->setContentsMargins(0, 0, 0, 0);
    optionsRow->setSpacing(8);
    optionsRow->addWidget(axes_box_);
    optionsRow->addWidget(ortho_box_);
    optionsRow->addWidget(rotation_lock_box_);
    optionsRow->addStretch();
    layout->addLayout(optionsRow);

    auto* rotateGrid = new QGridLayout();
    rotateGrid->setContentsMargins(0, 0, 0, 0);
    rotateGrid->setSpacing(2);
    // Text arrows instead of QStyle standard icons: the standard pixmaps are
    // drawn in black and become invisible on a dark theme.
    const struct {
        int row, column;
        QString label;
        QString tooltip;
        float yaw, pitch;
    } arrows[] = {
        {0, 1, QStringLiteral("\u25B2"), tr("voxelPreviewWidget.view.rotateUp"), 0.0f, -90.0f},
        {1, 0, QStringLiteral("\u25C0"), tr("voxelPreviewWidget.view.rotateLeft"), -90.0f, 0.0f},
        {1, 2, QStringLiteral("\u25B6"), tr("voxelPreviewWidget.view.rotateRight"), 90.0f, 0.0f},
        {2, 1, QStringLiteral("\u25BC"), tr("voxelPreviewWidget.view.rotateDown"), 0.0f, 90.0f},
    };
    for (const auto& arrow : arrows) {
        auto* button = new QToolButton(group);
        button->setText(arrow.label);
        button->setFixedSize(28, 28);
        button->setToolTip(arrow.tooltip);
        button->setFocusPolicy(Qt::NoFocus);
        const float yaw = arrow.yaw;
        const float pitch = arrow.pitch;
        connect(button, &QToolButton::clicked, this, [this, yaw, pitch]() { voxelWidget_->rotateView(yaw, pitch); });
        rotateGrid->addWidget(button, arrow.row, arrow.column);
    }

    auto* faceFrontButton = new QToolButton(group);
    faceFrontButton->setText(QStringLiteral("\u2299"));
    faceFrontButton->setFixedSize(28, 28);
    faceFrontButton->setToolTip(tr("voxelPreviewWidget.view.faceFront"));
    faceFrontButton->setFocusPolicy(Qt::NoFocus);
    connect(faceFrontButton, &QToolButton::clicked, this, [this]() { voxelWidget_->focusFrontFace(); });
    rotateGrid->addWidget(faceFrontButton, 1, 1);
    rotateGrid->setColumnStretch(0, 1);
    rotateGrid->setColumnStretch(2, 1);
    layout->addLayout(rotateGrid);
    return group;
}

QWidget* VoxelPreviewWidget::buildMcstructurePanel() {
    auto* group = new QGroupBox(QStringLiteral("mcstructure"), this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    auto* importButton = new QPushButton(tr("voxelPreviewWidget.importMcstructure"), group);
    importButton->setEnabled(false);
    importButton->setToolTip(tr("voxelPreviewWidget.import.tooltip"));
    connect(importButton, &QPushButton::clicked, this, [this]() { chooseImportFile(); });
    mcstructure_import_button_ = importButton;
    auto* exportButton = new QPushButton(tr("voxelPreviewWidget.exportMcstructure"), group);
    mcstructure_export_button_ = exportButton;
    mcstructureEntitiesBox_ = new QCheckBox(tr("voxelPreviewWidget.exportEntities"), group);
    mcstructureEntitiesBox_->setChecked(true);
    mcstructureCompressBox_ = new QCheckBox(tr("voxelPreviewWidget.compress"), group);
    mcstructureNewFormatBox_ = new QCheckBox(tr("voxelPreviewWidget.useNewFormat"), group);
    mcstructureNewFormatBox_->setToolTip(tr("voxelPreviewWidget.useNewFormat.tooltip"));
    connect(exportButton, &QPushButton::clicked, this, [this]() {
        emit exportMcstructureRequested(voxelWidget_->getSelection(), voxelWidget_->isSelectionEnabled(),
                                        mcstructureCompressBox_->isChecked(), mcstructureEntitiesBox_->isChecked(),
                                        mcstructureNewFormatBox_->isChecked());
    });

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(4);
    buttonRow->addWidget(exportButton);
    buttonRow->addWidget(importButton);

    auto* optionRow = new QHBoxLayout();
    optionRow->setContentsMargins(0, 0, 0, 0);
    optionRow->setSpacing(8);
    optionRow->addWidget(mcstructureEntitiesBox_);
    optionRow->addWidget(mcstructureNewFormatBox_);
    optionRow->addStretch();

    layout->addLayout(buttonRow);
    layout->addLayout(optionRow);
    layout->addWidget(mcstructureCompressBox_);
    // not ready to be exposed in the UI yet
    mcstructureCompressBox_->hide();
    return group;
}

QWidget* VoxelPreviewWidget::buildGlbPanel() {
    auto* group = new QGroupBox(QStringLiteral("GLB"), this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 6, 8, 6);

    auto* exportButton = new QPushButton(tr("voxelPreviewWidget.exportGlb.title"), group);
    exportButton->setToolTip(tr("voxelPreviewWidget.exportGlb.tooltip"));
    connect(exportButton, &QPushButton::clicked, this, [this]() { exportGlbModel(); });
    glb_export_button_ = exportButton;
    layout->addWidget(exportButton);
    return group;
}

void VoxelPreviewWidget::chooseImportFile() {
    const QString filePath = QFileDialog::getOpenFileName(this, tr("voxelPreviewWidget.importMcstructure"), QString(),
                                                          tr("MCStructure files (*.mcstructure)"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_F(WARNING, "Can not open mcstructure file: %s", filePath.toStdString().c_str());
        QMessageBox::warning(this, tr("voxelPreviewWidget.importMcstructure"), tr("voxelPreviewWidget.import.openFailed"));
        return;
    }
    const QByteArray raw = file.readAll();
    auto structure = std::make_shared<bl::mcstructure>(
        bl::parse_mcstructure(reinterpret_cast<const byte_t*>(raw.constData()), static_cast<size_t>(raw.size())));
    if (structure->size_x() <= 0 || structure->size_y() <= 0 || structure->size_z() <= 0) {
        LOG_F(WARNING, "Invalid mcstructure file: %s", filePath.toStdString().c_str());
        QMessageBox::warning(this, tr("voxelPreviewWidget.importMcstructure"), tr("voxelPreviewWidget.import.invalidFile"));
        return;
    }

    import_structure_ = std::move(structure);
    beginImportMode(import_structure_->size());
}

void VoxelPreviewWidget::beginImportMode(const bl::block_pos& importedSize) {
    if (import_mode_ || importedSize.x <= 0 || importedSize.y <= 0 || importedSize.z <= 0) return;
    import_mode_ = true;

    // The placement box matches the imported model, centred on the preview, and
    // can only be moved from there.
    const QVector3D model = voxelWidget_->modelSize();
    const QVector3D span(static_cast<float>(importedSize.x), static_cast<float>(importedSize.y), static_cast<float>(importedSize.z));
    const QVector3D center = (model - span) * 0.5f;
    const QVector3D minimum(std::round(center.x()), std::round(center.y()), std::round(center.z()));
    // ghost mesh of the structure to place, reusing the same voxel conversion as
    // the load path
    voxelWidget_->setPreviewVoxelData(buildVoxelDataFromMcstructure(*import_structure_));
    // lock before setting the box: a locked selection may exceed the model bounds
    voxelWidget_->setSelectionLocked(true);
    voxelWidget_->setSelectionEnabled(true);
    voxelWidget_->setSelection({minimum, minimum + span});
    voxelWidget_->setSelectionMoveMode(true);
    voxelWidget_->setPreviewOffset(voxelWidget_->getSelection().minimum);

    // The selection must stay on while placing, so the group checkbox is removed
    // instead of disabled (disabling it would grey out the position fields too).
    selection_group_->setCheckable(false);
    for (QSpinBox* box : selection_min_boxes_) box->setEnabled(false);
    for (QSpinBox* box : selection_max_boxes_) box->setEnabled(false);
    selection_move_box_->setEnabled(false);
    mcstructure_import_button_->setEnabled(false);
    mcstructure_export_button_->setEnabled(false);
    glb_export_button_->setEnabled(false);
    mcstructureEntitiesBox_->setEnabled(false);
    mcstructureNewFormatBox_->setEnabled(false);
    import_bar_->show();
}

void VoxelPreviewWidget::endImportMode() {
    if (!import_mode_) return;
    import_mode_ = false;

    voxelWidget_->setSelectionLocked(false);
    voxelWidget_->setSelectionMoveMode(false);

    selection_group_->setCheckable(true);
    selection_group_->setChecked(voxelWidget_->isSelectionEnabled());
    for (QSpinBox* box : selection_min_boxes_) box->setEnabled(true);
    for (QSpinBox* box : selection_max_boxes_) box->setEnabled(true);
    selection_move_box_->setEnabled(true);
    mcstructure_export_button_->setEnabled(true);
    glb_export_button_->setEnabled(true);
    mcstructureEntitiesBox_->setEnabled(true);
    mcstructureNewFormatBox_->setEnabled(true);
    import_bar_->hide();
    import_structure_.reset();
    voxelWidget_->clearPreviewVoxelData();
    refreshModelInfo();        // restores the import button state
    refreshSelectionFields();  // the placement box may have left the model bounds
}

QWidget* VoxelPreviewWidget::buildImportBar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* confirmButton = new QPushButton(tr("voxelPreviewWidget.import.confirm"), bar);
    connect(confirmButton, &QPushButton::clicked, this, [this]() {
        VoxelSelection placement = voxelWidget_->getSelection();
        placement.minimum += worldOrigin();
        placement.maximum += worldOrigin();
        emit importConfirmed(placement, import_structure_);
        endImportMode();
    });
    auto* cancelButton = new QPushButton(tr("voxelPreviewWidget.import.cancel"), bar);
    connect(cancelButton, &QPushButton::clicked, this, [this]() { endImportMode(); });

    layout->addWidget(confirmButton);
    layout->addWidget(cancelButton);
    bar->hide();
    return bar;
}

void VoxelPreviewWidget::refreshModelInfo() {
    if (!model_info_label_) return;
    const QVector3D size = voxelWidget_->modelSize();
    if (size.x() <= 0.0f || size.y() <= 0.0f || size.z() <= 0.0f) {
        model_info_label_->setText(tr("voxelPreviewWidget.model.empty"));
        if (mcstructure_import_button_) mcstructure_import_button_->setEnabled(false);
        return;
    }
    if (mcstructure_import_button_) mcstructure_import_button_->setEnabled(!import_mode_);

    const QVector3D origin = worldOrigin();
    const QVector3D end = origin + size;
    model_info_label_->setText(tr("voxelPreviewWidget.model.info")
                                   .arg(static_cast<int>(origin.x()))
                                   .arg(static_cast<int>(origin.y()))
                                   .arg(static_cast<int>(origin.z()))
                                   .arg(static_cast<int>(size.x()))
                                   .arg(static_cast<int>(size.y()))
                                   .arg(static_cast<int>(size.z()))
                                   .arg(static_cast<int>(end.x()))
                                   .arg(static_cast<int>(end.y()))
                                   .arg(static_cast<int>(end.z())));
}

void VoxelPreviewWidget::refreshSelectionFields() {
    if (!selection_group_) return;
    const QVector3D size = voxelWidget_->modelSize();
    const bool hasModel = size.x() > 0.0f && size.y() > 0.0f && size.z() > 0.0f;
    const QVector3D origin = worldOrigin();
    const VoxelSelection selection = voxelWidget_->getSelection();

    syncing_selection_fields_ = true;
    for (int axis = 0; axis < 3; ++axis) {
        const int modelLow = static_cast<int>(std::round(origin[axis]));
        const int modelHigh = static_cast<int>(std::round(origin[axis] + size[axis]));
        // an import placement box may extend past the model bounds, so the fields
        // cover the model and the current selection
        const int selectionLow = modelLow + static_cast<int>(std::round(selection.minimum[axis]));
        const int selectionHigh = modelLow + static_cast<int>(std::round(selection.maximum[axis]));
        const int low = std::min(modelLow, selectionLow);
        const int high = std::max(modelHigh, selectionHigh);
        // the maximum boundary is exclusive, so it can never equal the minimum
        selection_min_boxes_[axis]->setRange(low, std::max(low, high - 1));
        selection_max_boxes_[axis]->setRange(std::min(low + 1, high), high);
        if (!hasModel) continue;
        selection_min_boxes_[axis]->setValue(selectionLow);
        selection_max_boxes_[axis]->setValue(selectionHigh);
    }
    syncing_selection_fields_ = false;
}

void VoxelPreviewWidget::applySelectionFields() {
    if (syncing_selection_fields_) return;
    const QVector3D origin = worldOrigin();
    VoxelSelection selection;
    for (int axis = 0; axis < 3; ++axis) {
        int low = selection_min_boxes_[axis]->value();
        int high = selection_max_boxes_[axis]->value();
        if (low >= high) {
            // moving one boundary past the other drags it along instead of
            // rejecting the value
            if (selection_min_boxes_[axis]->hasFocus()) {
                low = high - 1;
            } else {
                high = low + 1;
            }
        }
        selection.minimum[axis] = static_cast<float>(low);
        selection.maximum[axis] = static_cast<float>(high);
    }
    selection.minimum -= origin;
    selection.maximum -= origin;
    voxelWidget_->setSelection(selection);
    refreshSelectionFields();  // the widget clamps, so mirror the committed values back
}

void VoxelPreviewWidget::refreshViewOptions() {
    if (axes_box_) axes_box_->setChecked(voxelWidget_->isAxesVisible());
    if (ortho_box_) ortho_box_->setChecked(voxelWidget_->isOrthoMode());
    if (rotation_lock_box_) rotation_lock_box_->setChecked(voxelWidget_->isRotationLocked());
    if (selection_move_box_) selection_move_box_->setChecked(voxelWidget_->isSelectionMoveMode());
}

bool VoxelPreviewWidget::loadChunksAsync(const bl::chunk_pos& minPos, const bl::chunk_pos& maxPos, AsyncLevelLoader& loader) {
    setWindowTitle(QString("%1 ~ %2").arg(minPos.to_string().c_str()).arg(maxPos.to_string().c_str()));
    if (chunk_task_.isRunning()) {
        LOG_F(WARNING, "Current render task is not finished");
        return false;
    }

    if (maxPos.x < minPos.x || maxPos.z < minPos.z) {
        LOG_F(WARNING, "Invald Chunk Area");
        return false;
    }

    bar_->show();
    bar_->setValue(0);
    bar_->setMaximum((maxPos.x - minPos.x + 1) * (maxPos.z - minPos.z + 1) * 2);

    voxelWidget_->updateVoxelData({});
    chunk_task_.start([this, minPos, maxPos, &loader](GuiTaskRunner* task) {
        std::vector<std::vector<bl::chunk*>> chunks;
        chunks.resize(maxPos.x - minPos.x + 1);
        for (auto& row : chunks) {
            row.resize(maxPos.z - minPos.z + 1);
        }
        size_t chunk_loaded = 0;
        auto dim = minPos.dim;
        for (int i = minPos.x; i <= maxPos.x; i++) {
            for (int j = minPos.z; j <= maxPos.z; j++) {
                chunks[i - minPos.x][j - minPos.z] = loader.getChunk(bl::chunk_pos{i, j, dim});
                chunk_loaded++;
                task->reportProgress(static_cast<int>(chunk_loaded));
            }
        }
        int firstWorldY = 0;
        auto voxelData = VoxelWidget::createVoxelDataFromChunks(
            chunks, [&](int cnt) { task->reportProgress(cnt + static_cast<int>(chunk_loaded)); }, &firstWorldY);
        for (auto& row : chunks)
            for (auto* c : row) delete c;
        pending_chunk_result_ = {std::move(voxelData), {minPos.x * 16, firstWorldY, minPos.z * 16}};
    });
    show();
    return true;
}

void VoxelPreviewWidget::setVoxelData(VoxelGrid&& data, const bl::block_pos& origin) {
    bar_->hide();
    voxel_origin_ = origin;
    voxelWidget_->updateVoxelData(std::move(data));
    refreshModelInfo();
    refreshSelectionFields();
}

void VoxelPreviewWidget::loadMcstructureAsync(std::shared_ptr<const bl::mcstructure> structure) {
    if (!structure) {
        return;
    }
    if (mcstructure_task_.isRunning()) {
        LOG_F(WARNING, "Current mcstructure render task is not finished");
        return;
    }
    bar_->show();
    bar_->setRange(0, 0);
    mcstructure_task_.start([this, structure](GuiTaskRunner*) {
        pending_mcstructure_result_ = {buildVoxelDataFromMcstructure(*structure), structure->origin()};
    });
}

void VoxelPreviewWidget::exportGlbModel() {
    QString filePath = QFileDialog::getSaveFileName(this, tr("voxelPreviewWidget.exportGlb.title"), QString(),
                                                    tr("voxelPreviewWidget.exportGlb.fileFilter"));
    if (filePath.isEmpty()) return;

    if (QFileInfo(filePath).suffix().isEmpty()) {
        filePath += QStringLiteral(".glb");
    }

    QString errorMessage;
    if (!voxelWidget_->exportGlb(filePath, &errorMessage)) {
        QMessageBox::warning(this, tr("voxelPreviewWidget.exportGlb.title"), errorMessage);
        return;
    }

    QMessageBox::information(this, tr("voxelPreviewWidget.exportGlb.title"), tr("voxelPreviewWidget.exportGlb.completed"));
}
