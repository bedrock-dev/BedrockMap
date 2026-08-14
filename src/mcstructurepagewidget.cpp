#include "mcstructurepagewidget.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "color.h"
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
    voxel_widget_ = new VoxelWidget(topSplitter);
    topSplitter->addWidget(nbt_editor_);
    topSplitter->addWidget(voxel_widget_);
    topSplitter->setStretchFactor(0, 0);
    topSplitter->setStretchFactor(1, 1);
    topSplitter->setChildrenCollapsible(false);

    // bottom: button bar (empty placeholder, wired up later)
    auto *bar = new QWidget(this);
    button_bar_ = new QHBoxLayout(bar);
    button_bar_->setContentsMargins(0, 2, 0, 0);
    button_bar_->addStretch();

    root->addWidget(topSplitter, 1);
    root->addWidget(bar, 0);
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

    structure_ = bl::parse_mcstructure(data, len);
    if (structure_.size_x <= 0 || structure_.size_y <= 0 || structure_.size_z <= 0) {
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
    // build the mesh once the widget is shown so the GL context is ready
    QTimer::singleShot(0, this, [this]() { buildVoxelData(); });
    return true;
}

void McstructurePageWidget::buildVoxelData() {
    const int sx = structure_.size_x;
    const int sy = structure_.size_y;
    const int sz = structure_.size_z;

    std::vector<std::vector<std::vector<Voxel>>> data;
    data.resize(sy);
    for (auto &yLayer : data) {
        yLayer.resize(sx);
        for (auto &xRow : yLayer) {
            xRow.resize(sz, Voxel(QColor(255, 255, 255, 0), true));
        }
    }

    const auto &palette = structure_.palette;
    const auto &indices = structure_.layers[0];
    for (size_t i = 0; i < indices.size(); i++) {
        const int idx = indices[i];
        if (idx < 0 || idx >= static_cast<int>(palette.size())) continue;
        const auto &name = palette[static_cast<size_t>(idx)].name;
        if (name == "minecraft:air" || name == "minecraft:unknown") continue;

        const int x = static_cast<int>(i) / (sz * sy);
        const int y = (static_cast<int>(i) / sz) % sy;
        const int z = static_cast<int>(i) % sz;
        if (x < 0 || x >= sx || y < 0 || y >= sy || z < 0 || z >= sz) continue;

        const auto c = bl::get_block_by_name_tag(name);
        data[y][x][z] = Voxel(QColor(c.r, c.g, c.b, c.a), c.a < 255);
    }

    voxel_widget_->updateVoxelData(data);
    voxel_widget_->setLayer(0, sy - 1);
}
