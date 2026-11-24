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

void BlockVolumeController::run(Operation operation, Task task, Failure onFailure) {
    setOperation(operation);
    // Any exception escaping a task becomes its failure completion, so the
    // operation always returns to None in finishOperation().
    watcher_.setFuture(QtConcurrent::run([task = std::move(task), onFailure = std::move(onFailure)]() -> std::function<void()> {
        QString detail;
        try {
            return task();
        } catch (const std::exception& error) {
            detail = QString::fromUtf8(error.what());
        } catch (...) {
        }
        return [onFailure, detail] { onFailure(detail); };
    }));
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

std::optional<bl::block_box> BlockVolumeController::selectionBounds(const QVector3D& minimum, const QVector3D& maximum) const {
    const auto scene = voxelScene();
    if (!scene) return std::nullopt;
    // Validate before converting floating-point QML coordinates to integers.
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] < 0 || maximum[axis] > scene->modelSize[axis] ||
            minimum[axis] >= maximum[axis])
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
    openArea(ChunkArea{minChunkX, minChunkZ, maxChunkX, maxChunkZ, dimension});
}

void BlockVolumeController::openArea(const ChunkArea& area) {
    if (area.valid()) load(area);
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
    run(
        Operation::Loading,
        [this, source, request = request_, report = progressReporter()]() -> std::function<void()> {
            auto result = source->load(request, report);
            if (!result.valid() && result.error.isEmpty()) result.error = tr("Could not build the block volume.");
            return [this, result] { applyLoad(result); };
        },
        [this](const QString& detail) {
            BlockVolumeLoadResult result;
            result.error =
                detail.isEmpty() ? tr("Could not build the block volume.") : tr("Could not build the block volume: %1").arg(detail);
            applyLoad(result);
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
    run(
        Operation::ImportLoading,
        [this, source, request = std::move(request), report = progressReporter()]() -> std::function<void()> {
            return [this, result = source->load(request, report)] { applyImportLoad(result); };
        },
        [this](const QString& detail) {
            BlockVolumeLoadResult result;
            if (!detail.isEmpty()) result.error = tr("Could not read the import structure: %1").arg(detail);
            applyImportLoad(result);
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

bool BlockVolumeController::beginStructureExport(Operation operation, const QVector3D& minimum, const QVector3D& maximum,
                                                 const StructureExportOptions& options, StructureExportCompletion completion,
                                                 Failure onFailure) {
    const auto bounds = selectionBounds(minimum, maximum);
    if (!source_ || !volume_ || !hasVoxelModel() || busy() || !bounds) return false;
    auto* source = source_.get();
    run(
        operation,
        [source, volume = volume_, bounds = *bounds, options, completion = std::move(completion)]() -> std::function<void()> {
            return completion(source->exportStructure(*volume, bounds, options));
        },
        std::move(onFailure));
    return true;
}

bool BlockVolumeController::copySelection(const QVector3D& minimum, const QVector3D& maximum, bool copyEntities, bool useNewFormat,
                                          bool includeAir) {
    return copySelection(minimum, maximum,
                         StructureExportOptions{.includeEntities = copyEntities, .includeAir = includeAir, .useNewFormat = useNewFormat});
}

bool BlockVolumeController::copySelection(const QVector3D& minimum, const QVector3D& maximum, const StructureExportOptions& options) {
    return beginStructureExport(
        Operation::Copying, minimum, maximum, options,
        [this](QByteArray raw) -> std::function<void()> {
            return [this, raw = std::move(raw)] {
                if (raw.isEmpty()) {
                    emit copyFailed(tr("Could not copy the selected blocks."));
                } else if (!context_.binaryClipboard().write(BinaryClipboard::PayloadKind::Mcstructure, raw)) {
                    emit copyFailed(tr("The clipboard is not available."));
                } else {
                    emit copyFinished();
                }
            };
        },
        [this](const QString&) { emit copyFailed(tr("Could not copy the selected blocks.")); });
}

bool BlockVolumeController::commitImport(const QVector3D& minimum, const QVector3D& maximum, bool importEntities,
                                         bool replaceNonAirBlocks) {
    return commitImport(minimum, maximum,
                        StructureImportOptions{.includeEntities = importEntities, .replaceNonAirBlocks = replaceNonAirBlocks});
}

bool BlockVolumeController::commitImport(const QVector3D& minimum, const QVector3D& maximum, const StructureImportOptions& options) {
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
    const auto finish = [this](const BlockVolumeMutationResult& result) {
        applyMutation(result, tr("Could not import the structure."), true);
    };
    run(
        Operation::Importing,
        [source, target = volume_, import = import_volume_, bounds, options, finish]() -> std::function<void()> {
            return [finish, result = source->commitImport(*target, *import, bounds, options)] { finish(result); };
        },
        [finish](const QString&) { finish({}); });
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
    const auto finish = [this](const BlockVolumeMutationResult& result) {
        applyMutation(result, tr("Could not delete the selected blocks."), false);
    };
    run(
        Operation::Deleting,
        [source, volume = volume_, bounds = *bounds, deleteEntities, finish]() -> std::function<void()> {
            return [finish, result = source->deleteBlocks(*volume, bounds, deleteEntities)] { finish(result); };
        },
        [finish](const QString&) { finish({}); });
    return true;
}

void BlockVolumeController::applyMutation(const BlockVolumeMutationResult& result, const QString& failure, bool import) {
    if (!result.success) {
        const QString message = result.error.isEmpty() ? failure : result.error;
        if (import)
            emit importFailed(message);
        else
            emit deleteFailed(message);
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
                                              bool useNewFormat, bool includeAir) {
    return exportMcstructure(
        url, minimum, maximum,
        StructureExportOptions{.includeEntities = exportEntities, .includeAir = includeAir, .useNewFormat = useNewFormat});
}

bool BlockVolumeController::exportMcstructure(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum,
                                              const StructureExportOptions& options) {
    if (url.isEmpty()) return false;
    const QUrl output = withDefaultSuffix(url, QStringLiteral("mcstructure"));
    return beginStructureExport(
        Operation::Exporting, minimum, maximum, options,
        [this, output](QByteArray raw) -> std::function<void()> {
            QString error;
            if (raw.isEmpty()) {
                error = tr("Could not generate the structure data.");
            } else if (const QString write_error = io::DocumentIo::writeBytes(output, raw); !write_error.isEmpty()) {
                error = tr("Could not export the structure: %1").arg(write_error);
            }
            return [this, output, error] { emit exportFinished(output, error); };
        },
        [this, output](const QString& detail) {
            emit exportFinished(output, detail.isEmpty() ? tr("Could not generate the structure data.")
                                                         : tr("Could not export the structure: %1").arg(detail));
        });
}

bool BlockVolumeController::exportGlb(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, bool compressMesh) {
    const auto bounds = selectionBounds(minimum, maximum);
    const auto scene = voxelScene();
    if (url.isEmpty() || busy() || !hasVoxelModel() || !bounds) return false;
    const QUrl output = withDefaultSuffix(url, QStringLiteral("glb"));
    // Capture the immutable preview, so navigating or closing the session cannot
    // invalidate an export or make it read a different version of the world.
    run(
        Operation::Exporting,
        [this, scene, bounds = *bounds, output, compressMesh]() -> std::function<void()> {
            QString error;
            const auto mesh = VoxelMeshBuffers::fromGrid(scene->voxels, bounds);
            if (!mesh.exportGlb(output, compressMesh, &error) && error.isEmpty()) error = tr("Could not export GLB.");
            return [this, output, error] { emit exportFinished(output, error); };
        },
        [this, output](const QString& detail) {
            emit exportFinished(output, detail.isEmpty() ? tr("Could not export GLB.") : tr("Could not export GLB: %1").arg(detail));
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
