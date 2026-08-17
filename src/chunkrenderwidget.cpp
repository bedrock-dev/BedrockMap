#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QtConcurrent>

#include "voxelwidget.h"
bool ChunkRenderWidget::showChunks(const bl::chunk_pos& minPos, const bl::chunk_pos& maxPos, AsyncLevelLoader& loader) {
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

    for (auto& row : chunks_)
        for (auto* c : row) delete c;
    chunks_.clear();
    voxels_.clear();
    voxelWidget_->updateVoxelData({});
    chunk_render_watcher_.setFuture(QtConcurrent::run([this, minPos, maxPos, &loader]() {
        // load chunk in another thread
        for (auto& row : this->chunks_)
            for (auto* c : row) delete c;
        this->chunks_.clear();
        chunks_.resize(maxPos.x - minPos.x + 1);
        for (auto& row : chunks_) {
            row.resize(maxPos.z - minPos.z + 1);
        }
        size_t chunk_loaded = 0;
        auto dim = minPos.dim;
        for (int i = minPos.x; i <= maxPos.x; i++) {
            for (int j = minPos.z; j <= maxPos.z; j++) {
                this->chunks_[i - minPos.x][j - minPos.z] = loader.getChunk(bl::chunk_pos{i, j, dim});
                chunk_loaded++;
                emit chunkMeshBuilt(chunk_loaded);
            }
        }
        voxels_ = VoxelWidget::createVoxelDataFromChunks(this->chunks_, [&](int cnt) { emit chunkMeshBuilt(cnt + chunk_loaded); });
        for (auto& row : this->chunks_)
            for (auto* c : row) delete c;
        this->chunks_.clear();
        return true;
    }));
    show();
    return true;
}

void ChunkRenderWidget::exportGlbModel() {
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