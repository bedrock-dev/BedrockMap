#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QtConcurrent>
#include <memory>

#include "color.h"
#include "palette.h"
#include "voxelwidget.h"

namespace {
VoxelPreviewWidget::VoxelGrid buildVoxelDataFromMcstructure(const bl::mcstructure& structure) {
    const int sx = structure.size_x;
    const int sy = structure.size_y;
    const int sz = structure.size_z;

    std::vector<std::vector<std::vector<Voxel>>> data;
    data.resize(sy);
    for (auto& yLayer : data) {
        yLayer.resize(sx);
        for (auto& xRow : yLayer) {
            xRow.resize(sz, Voxel(QColor(255, 255, 255, 0), true));
        }
    }

    if (structure.palette.empty() || structure.layers[0].empty()) {
        return data;
    }

    const auto& palette = structure.palette;
    const auto& indices = structure.layers[0];
    for (size_t i = 0; i < indices.size(); i++) {
        const int idx = indices[i];
        if (idx < 0 || idx >= static_cast<int>(palette.size())) continue;
        const auto& name = palette[static_cast<size_t>(idx)].name;
        if (name == "minecraft:air" || name == "minecraft:unknown") continue;

        const int x = static_cast<int>(i) / (sz * sy);
        const int y = (static_cast<int>(i) / sz) % sy;
        const int z = static_cast<int>(i) % sz;
        if (x < 0 || x >= sx || y < 0 || y >= sy || z < 0 || z >= sz) continue;

        const auto c = bl::get_block_by_name_tag(name);
        data[y][x][z] = Voxel(QColor(c.r, c.g, c.b, c.a), c.a < 255);
    }

    return data;
}
}  // namespace

bool VoxelPreviewWidget::loadChunksAsync(const bl::chunk_pos& minPos, const bl::chunk_pos& maxPos, AsyncLevelLoader& loader) {
    setWindowTitle(QString("%1 ~ %2").arg(minPos.to_string().c_str()).arg(maxPos.to_string().c_str()));
    if (!chunk_render_watcher_.isFinished()) {
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
    chunk_render_watcher_.setFuture(QtConcurrent::run([this, minPos, maxPos, &loader]() {
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
                emit chunkMeshBuilt(chunk_loaded);
            }
        }
        auto voxelData = VoxelWidget::createVoxelDataFromChunks(chunks, [&](int cnt) { emit chunkMeshBuilt(cnt + chunk_loaded); });
        for (auto& row : chunks)
            for (auto* c : row) delete c;
        return voxelData;
    }));
    show();
    return true;
}

void VoxelPreviewWidget::setVoxelData(VoxelGrid&& data) {
    bar_->hide();
    voxelWidget_->updateVoxelData(std::move(data));
}

void VoxelPreviewWidget::loadMcstructureAsync(std::shared_ptr<const bl::mcstructure> structure) {
    if (!structure) {
        return;
    }
    if (!mcstructure_render_watcher_.isFinished()) {
        LOG_F(WARNING, "Current mcstructure render task is not finished");
        return;
    }
    bar_->show();
    bar_->setRange(0, 0);
    mcstructure_render_watcher_.setFuture(QtConcurrent::run([structure]() { return buildVoxelDataFromMcstructure(*structure); }));
}

void VoxelPreviewWidget::exportGlbModel() {
    QString filePath = QFileDialog::getSaveFileName(this, tr("Export GLB"), QString(), tr("GLB files (*.glb)"));
    if (filePath.isEmpty()) return;

    if (QFileInfo(filePath).suffix().isEmpty()) {
        filePath += QStringLiteral(".glb");
    }

    QString errorMessage;
    if (!voxelWidget_->exportGlb(filePath, &errorMessage)) {
        QMessageBox::warning(this, tr("Export GLB"), errorMessage);
        return;
    }

    QMessageBox::information(this, tr("Export GLB"), tr("GLB export completed."));
}
