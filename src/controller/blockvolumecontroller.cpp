#include "controller/blockvolumecontroller.h"

#include <QMetaObject>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>
#include <vector>

#include "base/appcontext.h"
#include "base/binaryclipboard.h"
#include "nbt.h"

BlockVolumeController::BlockVolumeController(std::unique_ptr<BlockVolumeSource> source, AppContext& context, QObject* parent)
    : VoxelSceneController(parent),
      source_(std::move(source)),
      document_(new NbtDocumentController(this)),
      context_(context) {
    document_->setReadOnly(true);
    document_->setClipboard(&context_.binaryClipboard());
    connect(&load_watcher_, &QFutureWatcherBase::finished, this, &BlockVolumeController::finishLoad);
    connect(&import_load_watcher_, &QFutureWatcherBase::finished, this, &BlockVolumeController::finishImportLoad);
    connect(&import_watcher_, &QFutureWatcher<BlockVolumeMutationResult>::finished, this, &BlockVolumeController::finishImport);
    connect(&copy_watcher_, &QFutureWatcher<QByteArray>::finished, this, &BlockVolumeController::finishCopy);
    connect(&delete_watcher_, &QFutureWatcher<BlockVolumeMutationResult>::finished, this, &BlockVolumeController::finishDelete);
}

BlockVolumeController::~BlockVolumeController() {
    waitForExport();
    if (load_watcher_.isRunning()) load_watcher_.waitForFinished();
    if (import_load_watcher_.isRunning()) import_load_watcher_.waitForFinished();
    if (import_watcher_.isRunning()) import_watcher_.waitForFinished();
    if (copy_watcher_.isRunning()) copy_watcher_.waitForFinished();
    if (delete_watcher_.isRunning()) delete_watcher_.waitForFinished();
}

void BlockVolumeController::load(const QUrl& url) {
    if (!source_ || loading_ || importing_ || deleting_ || copying_ || exporting() || !source_->configureStructure(url)) return;
    startLoad();
}

void BlockVolumeController::loadBytes(const QByteArray& raw) {
    if (!source_ || loading_ || importing_ || deleting_ || copying_ || exporting() || !source_->configureBytes(raw)) return;
    startLoad();
}

void BlockVolumeController::clearPreview() {
    if (!scene_ && !import_scene_) return;
    scene_.reset();
    import_scene_.reset();
    error_.clear();
    setVoxelProgress(0);
    ++voxel_revision_;
    emit statusChanged();
    emit voxelChanged();
    emit overlayChanged();
}

void BlockVolumeController::openArea(int minChunkX, int minChunkZ, int maxChunkX, int maxChunkZ, int dimension) {
    if (!source_ || loading_ || importing_ || deleting_ || copying_ || exporting() ||
        !source_->configureArea(minChunkX, minChunkZ, maxChunkX, maxChunkZ, dimension))
        return;
    startLoad();
}

bool BlockVolumeController::importStructure(const QUrl& url) {
    if (!source_ || !source_->blockEditingEnabled() || loading_ || importing_ || deleting_ || copying_ || exporting() || !scene_ ||
        !source_->configureImport(url))
        return false;
    return beginImport();
}

bool BlockVolumeController::pasteStructure() {
    if (!source_ || !source_->blockEditingEnabled() || loading_ || importing_ || deleting_ || copying_ || exporting() || !scene_)
        return false;
    const QByteArray raw = context_.binaryClipboard().read(BinaryClipboard::PayloadKind::Mcstructure);
    if (raw.isEmpty() || !source_->configureImportData(raw)) {
        emit importFailed(tr("The clipboard does not contain a copied structure."));
        return false;
    }
    return beginImport();
}

bool BlockVolumeController::beginImport() {
    clearImport();
    importing_ = true;
    error_.clear();
    emit importingChanged();
    startImportLoad();
    return true;
}

bool BlockVolumeController::copySelection(const QVector3D& minimum, const QVector3D& maximum, bool copyEntities, bool useNewFormat) {
    if (!source_ || loading_ || importing_ || deleting_ || copying_ || exporting() || !scene_) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] < 0 ||
            maximum[axis] > scene_->modelSize[axis] || minimum[axis] >= maximum[axis])
            return false;
    }
    const bl::block_box localBounds{{qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
                                    {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())}};
    if (!localBounds.is_valid()) return false;

    copying_ = true;
    emit copyingChanged();
    auto* source = source_.get();
    const auto scene = scene_;
    copy_watcher_.setFuture(QtConcurrent::run([source, scene, localBounds, copyEntities, useNewFormat] {
        try {
            return source->exportStructure(*scene, localBounds, copyEntities, useNewFormat);
        } catch (...) {
            return QByteArray{};
        }
    }));
    return true;
}

bool BlockVolumeController::commitImport(const QVector3D& minimum, const QVector3D& maximum, bool importEntities) {
    if (!source_ || !import_scene_ || importing_ || loading_ || deleting_ || copying_ || exporting()) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
            maximum[axis] - minimum[axis] != import_scene_->modelSize[axis])
            return false;
    }
    const bl::block_box localBounds{{qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
                                    {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())}};
    if (!localBounds.is_valid()) return false;

    importing_ = true;
    emit importingChanged();
    auto* source = source_.get();
    const auto scene = scene_;
    import_watcher_.setFuture(QtConcurrent::run([source, scene, localBounds, importEntities] {
        try {
            return source->commitImport(*scene, localBounds, importEntities);
        } catch (...) {
            return BlockVolumeMutationResult{};
        }
    }));
    return true;
}

void BlockVolumeController::cancelImport() {
    if (importing_ || !import_scene_) return;
    clearImport();
}

void BlockVolumeController::setLoader(AsyncLevelLoader* loader) {
    waitForExport();
    if (load_watcher_.isRunning()) load_watcher_.waitForFinished();
    if (import_load_watcher_.isRunning()) import_load_watcher_.waitForFinished();
    if (import_watcher_.isRunning()) import_watcher_.waitForFinished();
    if (copy_watcher_.isRunning()) copy_watcher_.waitForFinished();
    if (delete_watcher_.isRunning()) delete_watcher_.waitForFinished();
    loading_ = false;
    importing_ = false;
    copying_ = false;
    deleting_ = false;
    if (source_) source_->setLevelLoader(loader);
    if (!loader) {
        scene_.reset();
        import_scene_.reset();
        error_.clear();
        setVoxelProgress(0);
        document_->loadMemory({}, {}, {});
        emit loadingChanged();
        emit importingChanged();
        emit copyingChanged();
        emit importPreviewChanged();
        emit overlayChanged();
        emit deletingChanged();
        emit statusChanged();
        emit voxelChanged();
    }
}

void BlockVolumeController::startLoad(bool allowWhileDeleting) {
    if (!source_ || loading_ || importing_ || copying_ || exporting() || (deleting_ && !allowWhileDeleting)) return;
    loading_ = true;
    error_.clear();
    scene_.reset();
    setVoxelProgress(0);
    document_->loadMemory({}, {}, {});
    emit loadingChanged();
    emit statusChanged();
    emit voxelChanged();

    auto* source = source_.get();
    load_watcher_.setFuture(QtConcurrent::run([this, source] {
        const auto reportProgress = [this](int progress) {
            QMetaObject::invokeMethod(this, [this, progress] { setVoxelProgress(progress); }, Qt::QueuedConnection);
        };
        try {
            auto result = source->load(reportProgress);
            if (!result) {
                result = std::make_shared<BlockVolumeLoadResult>();
                result->error = tr("Could not build the block volume.");
            }
            return result;
        } catch (const std::exception& error) {
            auto result = std::make_shared<BlockVolumeLoadResult>();
            result->error = tr("Could not build the block volume: %1").arg(QString::fromUtf8(error.what()));
            return result;
        } catch (...) {
            auto result = std::make_shared<BlockVolumeLoadResult>();
            result->error = tr("Could not build the block volume.");
            return result;
        }
    }));
}

void BlockVolumeController::startImportLoad() {
    if (!source_ || !importing_) return;
    const quint64 generation = import_generation_;
    auto* source = source_.get();
    import_load_watcher_.setFuture(QtConcurrent::run([this, source, generation] {
        const auto reportProgress = [this](int progress) {
            QMetaObject::invokeMethod(this, [this, progress] { setVoxelProgress(progress); }, Qt::QueuedConnection);
        };
        try {
            auto result = source->loadImport(reportProgress);
            if (!result) {
                result = std::make_shared<BlockVolumeLoadResult>();
                result->error = tr("Could not read the import structure.");
            } else if (generation != import_generation_) {
                result->error = {};
                result->scene.reset();
            }
            return result;
        } catch (const std::exception& error) {
            auto result = std::make_shared<BlockVolumeLoadResult>();
            result->error = tr("Could not read the import structure: %1").arg(QString::fromUtf8(error.what()));
            return result;
        } catch (...) {
            auto result = std::make_shared<BlockVolumeLoadResult>();
            result->error = tr("Could not read the import structure.");
            return result;
        }
    }));
}

void BlockVolumeController::finishLoad() {
    if (!loading_) return;
    auto result = load_watcher_.result();
    loading_ = false;
    error_ = result ? result->error : tr("Could not build the block volume.");
    scene_ = result ? std::move(result->scene) : nullptr;
    setVoxelProgress(error_.isEmpty() && scene_ ? 100 : 0);
    if (scene_) ++voxel_revision_;

    std::vector<std::unique_ptr<bl::nbt::compound_tag>> roots;
    if (result && result->documentRoot) roots.push_back(std::move(result->documentRoot));
    const QString documentTitle = result ? result->documentTitle : QString();
    document_->loadMemory(std::move(roots), {}, documentTitle);

    emit loadingChanged();
    emit statusChanged();
    emit voxelChanged();
    if (!error_.isEmpty()) emit loadFailed(error_);
}

void BlockVolumeController::finishImportLoad() {
    if (!importing_) return;
    const auto result = import_load_watcher_.result();
    importing_ = false;
    if (!result || !result->valid()) {
        const QString message = result && !result->error.isEmpty() ? result->error : tr("Could not read the import structure.");
        emit importingChanged();
        emit importFailed(message);
        return;
    }
    import_scene_ = result->scene;
    setVoxelProgress(100);
    emit importingChanged();
    emit importPreviewChanged();
    emit overlayChanged();
}

void BlockVolumeController::finishImport() {
    if (!importing_) return;
    const auto result = import_watcher_.result();
    importing_ = false;
    if (!result.success) {
        emit importingChanged();
        emit importFailed(tr("Could not import the structure."));
        return;
    }

    import_scene_.reset();
    emit importPreviewChanged();
    emit overlayChanged();
    emit regionsChanged(result.region, result.dimension);
    startLoad(true);
    emit importingChanged();
    emit importFinished();
}

void BlockVolumeController::finishCopy() {
    if (!copying_) return;
    const QByteArray raw = copy_watcher_.result();
    copying_ = false;
    emit copyingChanged();
    if (raw.isEmpty()) {
        emit copyFailed(tr("Could not copy the selected blocks."));
        return;
    }

    if (!context_.binaryClipboard().write(BinaryClipboard::PayloadKind::Mcstructure, raw)) {
        emit copyFailed(tr("The clipboard is not available."));
        return;
    }
    emit copyFinished();
}

void BlockVolumeController::clearImport() {
    ++import_generation_;
    import_scene_.reset();
    emit importPreviewChanged();
    emit overlayChanged();
}

bool BlockVolumeController::deleteBlockVolumeAsync(const QVector3D& minimum, const QVector3D& maximum, bool deleteEntities) {
    if (!source_ || !source_->blockEditingEnabled() || loading_ || importing_ || deleting_ || copying_ || exporting() || !scene_)
        return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] < 0 ||
            maximum[axis] > scene_->modelSize[axis] || minimum[axis] >= maximum[axis])
            return false;
    }
    const bl::block_box localBounds{{qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
                                    {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())}};
    if (!localBounds.is_valid()) return false;

    deleting_ = true;
    emit deletingChanged();
    auto* source = source_.get();
    const auto scene = scene_;
    delete_watcher_.setFuture(QtConcurrent::run([source, scene, localBounds, deleteEntities] {
        try {
            return source->deleteBlocks(*scene, localBounds, deleteEntities);
        } catch (...) {
            return BlockVolumeMutationResult{};
        }
    }));
    return true;
}

void BlockVolumeController::finishDelete() {
    if (!deleting_) return;
    const auto result = delete_watcher_.result();
    if (!result.success) {
        deleting_ = false;
        emit deletingChanged();
        emit deleteFailed(tr("Could not delete the selected blocks."));
        return;
    }

    emit regionsChanged(result.region, result.dimension);
    startLoad(true);
    deleting_ = false;
    emit deletingChanged();
}

VoxelSceneController::StructureExportTask BlockVolumeController::structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                                                     bool useNewFormat) const {
    if (!source_ || !scene_) return {};
    auto* source = source_.get();
    const auto scene = scene_;
    return [source, scene, localBounds, exportEntities, useNewFormat] {
        return source->exportStructure(*scene, localBounds, exportEntities, useNewFormat);
    };
}

void BlockVolumeController::setVoxelProgress(int progress) {
    progress = std::clamp(progress, 0, 100);
    if (voxel_progress_ == progress) return;
    voxel_progress_ = progress;
    emit voxelProgressChanged();
}
