#include "controller/mcstructurecontroller.h"

#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include "model/mcstructuredocument.h"
#include "render/voxel_mesh.h"

struct McstructureLoadResult {
    mcstructure_document::Data data;
    std::shared_ptr<VoxelSceneData> scene;
};

McstructureController::McstructureController(QObject* parent)
    : VoxelSceneController(parent), document_(new NbtDocumentController(this)) {
    document_->setReadOnly(true);
    connect(&watcher_, &QFutureWatcherBase::finished, this, [this] {
        auto result = watcher_.result();
        error_ = result->data.error;
        scene_ = std::move(result->scene);
        structure_ = std::move(result->data.structure);
        if (error_.isEmpty()) {
            std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
            roots.push_back(std::move(result->data.root));
            document_->loadMemory(std::move(roots), {}, title_);
        }
        loading_ = false;
        emit loadingChanged();
        emit statusChanged();
        emit voxelProgressChanged();
        emit voxelChanged();
        if (!error_.isEmpty()) emit loadFailed(error_);
    });
}

void McstructureController::load(const QString& path) {
    if (loading_) return;
    title_ = QFileInfo(path).fileName();
    loading_ = true;
    error_.clear();
    scene_.reset();
    structure_.reset();
    emit loadingChanged();
    emit statusChanged();
    emit voxelProgressChanged();
    emit voxelChanged();
    // Workers own their inputs and results, so closing a loading session never
    // leaves them referring to a destroyed controller or blocks the GUI thread.
    watcher_.setFuture(QtConcurrent::run([path] {
        auto result = std::make_shared<McstructureLoadResult>();
        try {
            result->data = mcstructure_document::load(path);
            if (!result->data.valid()) return result;
            const auto& structure = *result->data.structure;
            auto grid = voxel_mesh::createVoxelDataFromMcstructure(structure);
            auto scene = std::make_shared<VoxelSceneData>();
            if (const auto bounds = voxel_mesh::fullBounds(grid)) {
                auto mesh = voxel_mesh::build(grid, *bounds);
                scene->opaqueVertices = std::move(mesh.opaqueVertices);
                scene->opaqueIndices = std::move(mesh.opaqueIndices);
                scene->transparentVertices = std::move(mesh.transparentVertices);
                scene->transparentIndices = std::move(mesh.transparentIndices);
            }
            scene->modelSize = QVector3D(structure.size_x(), structure.size_y(), structure.size_z());
            scene->voxels = std::make_shared<const VoxelGrid>(std::move(grid));
            const auto origin = structure.origin();
            scene->origin = QVector3D(origin.x, origin.y, origin.z);
            scene->revision = 1;
            result->scene = std::move(scene);
            result->data.raw.clear();
        } catch (const std::exception& error) {
            result->data.error = tr("Could not build the structure preview: %1").arg(QString::fromUtf8(error.what()));
            result->scene.reset();
        }
        return result;
    }));
}

VoxelSceneController::StructureExportTask McstructureController::structureExportTask(
    const bl::block_box& localBounds, bool exportEntities, bool useNewFormat) const {
    if (!structure_) return {};
    return [structure = structure_, localBounds, exportEntities, useNewFormat] {
        return mcstructure_document::exportData(*structure, localBounds, exportEntities, useNewFormat);
    };
}
