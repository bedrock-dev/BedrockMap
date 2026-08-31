#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <memory>

#include "color.h"
#include "palette.h"
#include "voxelwidget.h"

namespace {
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
    chunk_task_.start([this, minPos, maxPos, &loader](AsyncTaskRunner* task) {
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
    mcstructure_task_.start([this, structure](AsyncTaskRunner*) {
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
