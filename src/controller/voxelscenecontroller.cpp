#include "controller/voxelscenecontroller.h"

#include <QFileInfo>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <exception>
#include <optional>

#include "io/documentio.h"

namespace {
    std::optional<bl::block_box> exportBounds(const VoxelSceneData& scene, const QVector3D& minimum, const QVector3D& maximum) {
        // Validate before converting floating-point QML coordinates to integers.
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] < 0 ||
                maximum[axis] > scene.modelSize[axis] || minimum[axis] >= maximum[axis])
                return std::nullopt;
        }
        const bl::block_box bounds{{qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
                                   {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())}};
        return bounds.is_valid() ? std::optional<bl::block_box>(bounds) : std::nullopt;
    }
}  // namespace

VoxelSceneController::VoxelSceneController(QObject* parent) : QObject(parent) {
    connect(&export_watcher_, &QFutureWatcherBase::finished, this, [this] {
        const QUrl url = export_url_;
        const QString error = export_watcher_.result();
        exporting_ = false;
        emit exportingChanged();
        emit exportFinished(url, error);
    });
}

void VoxelSceneController::waitForExport() { export_watcher_.waitForFinished(); }

bool VoxelSceneController::deleteBlockVolumeAsync(const QVector3D&, const QVector3D&, bool) { return false; }

bool VoxelSceneController::exportMcstructure(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, bool exportEntities,
                                             bool useNewFormat) {
    const auto scene = voxelScene();
    if (url.isEmpty() || loading() || deleting() || exporting_ || !scene || !hasVoxelModel()) return false;
    const auto bounds = exportBounds(*scene, minimum, maximum);
    if (!bounds) return false;
    auto task = structureExportTask(*bounds, exportEntities, useNewFormat);
    if (!task) return false;

    export_url_ = url;
    // Content-provider URLs already name a document created by the Android picker.
    if (url.isLocalFile()) {
        QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".mcstructure");
        export_url_ = QUrl::fromLocalFile(path);
    }
    exporting_ = true;
    emit exportingChanged();
    export_watcher_.setFuture(QtConcurrent::run([task = std::move(task), outputUrl = export_url_] {
        try {
            const QByteArray raw = task();
            if (raw.isEmpty()) return tr("Could not generate the structure data.");
            const QString error = io::DocumentIo::writeBytes(outputUrl, raw);
            return error.isEmpty() ? QString() : tr("Could not export the structure: %1").arg(error);
        } catch (const std::exception& error) {
            return tr("Could not export the structure: %1").arg(QString::fromUtf8(error.what()));
        }
    }));
    return true;
}

bool VoxelSceneController::exportGlb(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum) {
    const auto scene = voxelScene();
    if (url.isEmpty() || loading() || deleting() || exporting_ || !scene || !scene->voxels || !hasVoxelModel()) return false;
    const auto bounds = exportBounds(*scene, minimum, maximum);
    if (!bounds) return false;

    export_url_ = url;
    if (url.isLocalFile()) {
        QString path = url.toLocalFile();
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".glb");
        export_url_ = QUrl::fromLocalFile(path);
    }
    exporting_ = true;
    emit exportingChanged();
    // Capture the immutable preview, so navigating or closing the session cannot
    // invalidate an export or make it read a different version of the world.
    export_watcher_.setFuture(QtConcurrent::run([scene, bounds = *bounds, outputUrl = export_url_] {
        try {
            const auto mesh = voxel_mesh::build(*scene->voxels, bounds);
            QString error;
            if (!voxel_mesh::exportGlb(mesh, outputUrl, &error)) return error;
            return QString();
        } catch (const std::exception& error) {
            return tr("Could not export GLB: %1").arg(QString::fromUtf8(error.what()));
        }
    }));
    return true;
}
