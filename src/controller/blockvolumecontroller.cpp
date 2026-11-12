#include "controller/blockvolumecontroller.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include "base/appcontext.h"
#include "base/binaryclipboard.h"
#include "io/documentio.h"

BlockVolumeController::BlockVolumeController(std::unique_ptr<BlockVolumeSource> source, AppContext& context, QObject* parent)
    : VoxelSceneController(parent), source_(std::move(source)), context_(context) {
    connect(&watcher_, &QFutureWatcherBase::finished, this, &BlockVolumeController::finishOperation);
}

BlockVolumeController::~BlockVolumeController() {
    if (watcher_.isRunning()) watcher_.waitForFinished();
}

void BlockVolumeController::run(Operation operation, Task task) {
    setOperation(operation);
    watcher_.setFuture(QtConcurrent::run(std::move(task)));
}

void BlockVolumeController::finishOperation() {
    // setLoader() may already have drained and reset this operation.
    if (operation_ == Operation::None) return;
    const bool superseded = operation_ == Operation::Loading && pending_load_;
    auto finish = watcher_.result();
    setOperation(Operation::None);
    if (superseded) {
        std::exchange(pending_load_, {})();
        return;
    }
    if (finish) finish();
}

void BlockVolumeController::setOperation(Operation operation) {
    if (operation_ == operation) return;
    const bool was_loading = loading();
    operation_ = operation;
    emit operationChanged();
    if (was_loading != loading()) emit loadingChanged();
}

std::function<void(int)> BlockVolumeController::progressReporter() {
    return [this](int progress) {
        QMetaObject::invokeMethod(this, [this, progress] { setLoadProgress(progress); }, Qt::QueuedConnection);
    };
}

std::optional<bl::block_box> BlockVolumeController::selectionBounds(const QVector3D& minimum, const QVector3D& maximum) const {
    const auto scene = voxelScene();
    if (!scene) return std::nullopt;
    // Validate before converting floating-point QML coordinates to integers.
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] < 0 ||
            maximum[axis] > scene->modelSize[axis] || minimum[axis] >= maximum[axis])
            return std::nullopt;
    }
    const bl::block_box bounds{{qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
                               {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())}};
    return bounds.is_valid() ? std::optional<bl::block_box>(bounds) : std::nullopt;
}

QUrl BlockVolumeController::withDefaultSuffix(const QUrl& url, const QString& suffix) {
    // Content-provider URLs already name a document created by the Android picker.
    if (!url.isLocalFile()) return url;
    QString path = url.toLocalFile();
    if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + suffix;
    return QUrl::fromLocalFile(path);
}

void BlockVolumeController::load(BlockVolumeRequest request) {
    if (!source_) return;
    if (loading()) {
        pending_load_ = [this, request = std::move(request)]() mutable { startLoad(std::move(request), false); };
        return;
    }
    if (busy()) return;
    startLoad(std::move(request), false);
}

void BlockVolumeController::openArea(int minChunkX, int minChunkZ, int maxChunkX, int maxChunkZ, int dimension) {
    if (maxChunkX < minChunkX || maxChunkZ < minChunkZ) return;
    load(ChunkArea{minChunkX, minChunkZ, maxChunkX, maxChunkZ, dimension});
}

void BlockVolumeController::clearPreview() {
    if (loading()) {
        pending_load_ = [this] { clearPreview(); };
        return;
    }
    if (!volume_ && !import_volume_) return;
    volume_.reset();
    import_volume_.reset();
    error_.clear();
    setLoadProgress(0);
    emit statusChanged();
    emit voxelChanged();
    emit importPreviewChanged();
    emit overlayChanged();
}

void BlockVolumeController::startLoad(BlockVolumeRequest request, bool keepScene) {
    request_ = std::move(request);
    error_.clear();
    setLoadProgress(0);
    if (!keepScene) {
        volume_.reset();
        emit voxelChanged();
    }
    emit statusChanged();

    auto* source = source_.get();
    run(Operation::Loading, [this, source, request = request_, report = progressReporter()]() -> std::function<void()> {
        BlockVolumeLoadResult result;
        try {
            result = source->load(request, report);
        } catch (const std::exception& error) {
            result = {};
            result.error = tr("Could not build the block volume: %1").arg(QString::fromUtf8(error.what()));
        } catch (...) {
            result = {};
        }
        if (!result.valid() && result.error.isEmpty()) result.error = tr("Could not build the block volume.");
        return [this, result] { applyLoad(result); };
    });
}

void BlockVolumeController::applyLoad(const BlockVolumeLoadResult& result) {
    error_ = result.error;
    volume_ = result.valid() ? result.volume : nullptr;
    setLoadProgress(volume_ ? 100 : 0);
    emit statusChanged();
    emit voxelChanged();
    if (!error_.isEmpty()) emit loadFailed(error_);
}

bool BlockVolumeController::importStructure(const QUrl& url) {
    if (url.isEmpty()) return false;
    return beginImport(url);
}

bool BlockVolumeController::pasteStructure() {
    if (!blockVolumeEditingEnabled() || busy() || !volume_) return false;
    const QByteArray raw = context_.binaryClipboard().read(BinaryClipboard::PayloadKind::Mcstructure);
    if (raw.isEmpty()) {
        emit importFailed(tr("The clipboard does not contain a copied structure."));
        return false;
    }
    return beginImport(raw);
}

bool BlockVolumeController::beginImport(BlockVolumeRequest request) {
    if (!blockVolumeEditingEnabled() || busy() || !volume_) return false;
    clearImport();
    error_.clear();
    auto* source = source_.get();
    run(Operation::ImportLoading, [this, source, request = std::move(request), report = progressReporter()]() -> std::function<void()> {
        BlockVolumeLoadResult result;
        try {
            result = source->load(request, report);
        } catch (const std::exception& error) {
            result = {};
            result.error = tr("Could not read the import structure: %1").arg(QString::fromUtf8(error.what()));
        } catch (...) {
            result = {};
        }
        return [this, result] { applyImportLoad(result); };
    });
    return true;
}

void BlockVolumeController::applyImportLoad(const BlockVolumeLoadResult& result) {
    if (!result.valid() || !result.volume->structure) {
        emit importFailed(!result.error.isEmpty() ? result.error : tr("Could not read the import structure."));
        return;
    }
    import_volume_ = result.volume;
    setLoadProgress(100);
    emit importPreviewChanged();
    emit overlayChanged();
}

bool BlockVolumeController::copySelection(const QVector3D& minimum, const QVector3D& maximum, bool copyEntities, bool useNewFormat) {
    const auto bounds = selectionBounds(minimum, maximum);
    if (!source_ || busy() || !bounds) return false;
    auto* source = source_.get();
    run(Operation::Copying,
        [this, source, volume = volume_, bounds = *bounds, copyEntities, useNewFormat]() -> std::function<void()> {
            QByteArray raw;
            try {
                raw = source->exportStructure(*volume, bounds, copyEntities, useNewFormat);
            } catch (...) {
                raw.clear();
            }
            return [this, raw] {
                if (raw.isEmpty()) {
                    emit copyFailed(tr("Could not copy the selected blocks."));
                } else if (!context_.binaryClipboard().write(BinaryClipboard::PayloadKind::Mcstructure, raw)) {
                    emit copyFailed(tr("The clipboard is not available."));
                } else {
                    emit copyFinished();
                }
            };
        });
    return true;
}

bool BlockVolumeController::commitImport(const QVector3D& minimum, const QVector3D& maximum, bool importEntities) {
    if (!source_ || !volume_ || !import_volume_ || busy()) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
            maximum[axis] - minimum[axis] != import_volume_->scene->modelSize[axis])
            return false;
    }
    const bl::block_box bounds{{qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
                               {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())}};
    if (!bounds.is_valid()) return false;

    auto* source = source_.get();
    run(Operation::Importing,
        [this, source, target = volume_, import = import_volume_, bounds, importEntities]() -> std::function<void()> {
            BlockVolumeMutationResult result;
            try {
                result = source->commitImport(*target, *import, bounds, importEntities);
            } catch (...) {
                result = {};
            }
            return [this, result] { applyMutation(result, tr("Could not import the structure."), true); };
        });
    return true;
}

void BlockVolumeController::cancelImport() {
    if (importing() || !import_volume_) return;
    clearImport();
}

bool BlockVolumeController::deleteBlockVolumeAsync(const QVector3D& minimum, const QVector3D& maximum, bool deleteEntities) {
    const auto bounds = selectionBounds(minimum, maximum);
    if (!blockVolumeEditingEnabled() || busy() || !bounds) return false;
    auto* source = source_.get();
    run(Operation::Deleting, [this, source, volume = volume_, bounds = *bounds, deleteEntities]() -> std::function<void()> {
        BlockVolumeMutationResult result;
        try {
            result = source->deleteBlocks(*volume, bounds, deleteEntities);
        } catch (...) {
            result = {};
        }
        return [this, result] { applyMutation(result, tr("Could not delete the selected blocks."), false); };
    });
    return true;
}

void BlockVolumeController::applyMutation(const BlockVolumeMutationResult& result, const QString& failure, bool import) {
    if (!result.success) {
        if (import)
            emit importFailed(failure);
        else
            emit deleteFailed(failure);
        return;
    }
    if (import) {
        import_volume_.reset();
        emit importPreviewChanged();
        emit overlayChanged();
    }
    emit regionsChanged(result.region, result.dimension);
    // Keep the current scene visible until the edited volume has been rebuilt.
    startLoad(request_, true);
    if (import) emit importFinished();
}

bool BlockVolumeController::exportMcstructure(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, bool exportEntities,
                                              bool useNewFormat) {
    const auto bounds = selectionBounds(minimum, maximum);
    if (url.isEmpty() || !source_ || busy() || !hasVoxelModel() || !bounds) return false;
    auto* source = source_.get();
    const QUrl output = withDefaultSuffix(url, QStringLiteral("mcstructure"));
    run(Operation::Exporting,
        [this, source, volume = volume_, bounds = *bounds, exportEntities, useNewFormat, output]() -> std::function<void()> {
            QString error;
            try {
                const QByteArray raw = source->exportStructure(*volume, bounds, exportEntities, useNewFormat);
                if (raw.isEmpty()) {
                    error = tr("Could not generate the structure data.");
                } else if (const QString write_error = io::DocumentIo::writeBytes(output, raw); !write_error.isEmpty()) {
                    error = tr("Could not export the structure: %1").arg(write_error);
                }
            } catch (const std::exception& exception) {
                error = tr("Could not export the structure: %1").arg(QString::fromUtf8(exception.what()));
            }
            return [this, output, error] { emit exportFinished(output, error); };
        });
    return true;
}

bool BlockVolumeController::exportGlb(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum) {
    const auto bounds = selectionBounds(minimum, maximum);
    const auto scene = voxelScene();
    if (url.isEmpty() || busy() || !hasVoxelModel() || !scene->voxels || !bounds) return false;
    const QUrl output = withDefaultSuffix(url, QStringLiteral("glb"));
    // Capture the immutable preview, so navigating or closing the session cannot
    // invalidate an export or make it read a different version of the world.
    run(Operation::Exporting, [this, scene, bounds = *bounds, output]() -> std::function<void()> {
        QString error;
        try {
            const auto mesh = voxel_mesh::build(*scene->voxels, bounds);
            if (!voxel_mesh::exportGlb(mesh, output, &error) && error.isEmpty()) error = tr("Could not export GLB.");
        } catch (const std::exception& exception) {
            error = tr("Could not export GLB: %1").arg(QString::fromUtf8(exception.what()));
        }
        return [this, output, error] { emit exportFinished(output, error); };
    });
    return true;
}

void BlockVolumeController::setLoader(AsyncLevelLoader* loader) {
    if (watcher_.isRunning()) watcher_.waitForFinished();
    pending_load_ = {};
    setOperation(Operation::None);
    if (source_) source_->setLevelLoader(loader);
    if (loader) return;
    volume_.reset();
    import_volume_.reset();
    error_.clear();
    setLoadProgress(0);
    emit importPreviewChanged();
    emit overlayChanged();
    emit statusChanged();
    emit voxelChanged();
}

void BlockVolumeController::clearImport() {
    if (!import_volume_) return;
    import_volume_.reset();
    emit importPreviewChanged();
    emit overlayChanged();
}

void BlockVolumeController::setLoadProgress(int progress) {
    progress = std::clamp(progress, 0, 100);
    if (load_progress_ == progress) return;
    load_progress_ = progress;
    emit loadProgressChanged();
}
