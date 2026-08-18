#include "mcstructurepagewidget.h"

#include <QFile>
#include <QFileInfo>
#include <QSplitter>
#include <QVBoxLayout>

#include "loguru/loguru.hpp"
McstructurePageWidget::McstructurePageWidget(QWidget *parent) : TabPageWidget(parent) { setupUI(); }

McstructurePageWidget::~McstructurePageWidget() = default;

void McstructurePageWidget::setupUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(2);

    // top: nbt editor | voxel view
    auto *topSplitter = new QSplitter(Qt::Horizontal, this);
    nbt_editor_ = new NbtWidget(topSplitter);
    nbt_editor_->setMode(NbtMode::Memory);    // data comes from the parsed file, not the load button
    nbt_editor_->setReadOnly(true);       // structure files are viewed, not edited
    nbt_editor_->setListVisible(false);   // single document: no left item list
    voxel_preview_widget_ = new VoxelPreviewWidget(topSplitter);
    topSplitter->addWidget(nbt_editor_);
    topSplitter->addWidget(voxel_preview_widget_);
    topSplitter->setStretchFactor(0, 0);
    topSplitter->setStretchFactor(1, 1);
    topSplitter->setChildrenCollapsible(false);

    root->addWidget(topSplitter, 1);
}

bool McstructurePageWidget::loadStructure(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_F(WARNING, "Can not open mcstructure file: %s", path.toStdString().c_str());
        return false;
    }
    QByteArray raw = file.readAll();
    const auto *data = reinterpret_cast<const byte_t *>(raw.constData());
    const auto len = static_cast<size_t>(raw.size());

    structure_ = std::make_shared<bl::mcstructure>(bl::parse_mcstructure(data, len));
    if (structure_->size_x <= 0 || structure_->size_y <= 0 || structure_->size_z <= 0) {
        LOG_F(WARNING, "Invalid mcstructure file: %s", path.toStdString().c_str());
        return false;
    }

    // left side: NBT editor showing the full structure tree
    int read = 0;
    if (auto *root = bl::nbt::read_one_palette(data, len, read)) {
        nbt_editor_->loadNewData({NBTListItem::from(root, QFileInfo(path).fileName())});
        // The item list is hidden in this view, so open the first item directly
        // to populate the NBT tree.
        nbt_editor_->openItem(0);
    }

    structure_name_ = QFileInfo(path).baseName();
    voxel_preview_widget_->loadMcstructureAsync(structure_);
    return true;
}
