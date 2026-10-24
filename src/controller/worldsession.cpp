#include "controller/worldsession.h"

#include <QDir>
#include <QtConcurrent>

#include "base/config.h"
#include "loader/asynclevelloader.h"
#include "loader/leveldbxor.h"
#include "model/worldpath.h"
#include "nbt.h"

WorldSession::WorldSession(QString path, QString title, QObject* parent)
    : WorkspaceSession(QStringLiteral("world"), world_path::normalize(path),
                       title.isEmpty() ? QStringLiteral("World workspace") : std::move(title), parent),
      map_session_(this),
      map_controller_(&map_session_, this),
      chunk_editor_(this),
      voxel_view_(this) {
    connect(&open_watcher_, &QFutureWatcher<bool>::finished, this, &WorldSession::onOpenFinished);
    connect(&save_watcher_, &QFutureWatcher<bool>::finished, this, &WorldSession::onSaveFinished);
    connect(&global_data_, &GlobalDataController::dirtyChanged, this, &WorldSession::refreshDirty);
    connect(&chunk_editor_, &ChunkEditorController::dirtyChanged, this, &WorldSession::refreshDirty);
    connect(this, &WorkspaceSession::savingChanged, this, &WorldSession::canSaveChanged);
    connect(this, &WorkspaceSession::savingChanged, this,
            [this] { map_controller_.setEditingBlocked(closing_ || saving()); });
    connect(&map_controller_, &MapController::mapDataChanged, this, [this](const QRegion& region, int dimension) {
        if (terrain_data_) terrain_data_->invalidateRegions(region, dimension);
    });
}

WorldSession::~WorldSession() {
    close();
}

void WorldSession::setWorkspaceSection(int section) {
    if (section < 0 || workspace_section_ == section) return;
    workspace_section_ = section;
    emit workspaceSectionChanged();
}

void WorldSession::setActive(bool active) {
    if (closing_ && active) return;
    if (this->active() == active) return;
    WorkspaceSession::setActive(active);
    if (terrain_data_) terrain_data_->setActive(active);
}

void WorldSession::ensureTerrainData() {
    if (terrain_data_) return;
    terrain_data_ = std::make_unique<TerrainDataProvider>(this);
    // The session may already be active when its renderer-independent provider
    // is created. In that case no later activeChanged signal will arrive.
    terrain_data_->setActive(active());
}

void WorldSession::open() {
    if (closing_) return;
    if (state_ == OperationState::Running || state_ == OperationState::Succeeded) return;
    if (path().isEmpty() || !QDir(path()).exists()) {
        setErrorCode(QStringLiteral("path-unavailable"));
        setState(OperationState::Failed);
        return;
    }
    setErrorCode({});
    setProgress(0);
    setIndexProgress(0, 0);
    setIndexLoading(setting::current().preloadAllChunkCoords);
    setState(OperationState::Running);
    opening_loader_ = std::make_unique<AsyncLevelLoader>();
    opening_loader_->setPreloadAllChunkCoords(setting::current().preloadAllChunkCoords);
    opening_loader_->setChunkOverviewEnabled(setting::current().chunkOverviewEnabled);
    connect(opening_loader_.get(), &AsyncLevelLoader::chunkCoordsPreloadProgress, this,
            [this](qulonglong scannedKeys, qulonglong chunks) { setIndexProgress(scannedKeys, chunks); });
    connect(opening_loader_.get(), &AsyncLevelLoader::chunkCoordsPreloadFinished, this, [this] {
        setIndexProgress(scanned_keys_, indexed_chunks_);
        setProgress(100);
        setIndexLoading(false);
    });
    auto* loader = opening_loader_.get();
    const auto native_path = path().toStdString();
    const auto xor_key = leveldb_xor::keyForLevel(native_path);
    open_watcher_.setFuture(QtConcurrent::run([loader, native_path, xor_key] { return loader->open(native_path, xor_key); }));
}

void WorldSession::close() {
    if (closing_) return;
    closing_ = true;
    map_controller_.setEditingBlocked(true);
    setActive(false);
    // Unload QML pages before clearing any controller's data. Watcher results
    // queued before close must never publish into this session again.
    if (state_ != OperationState::Idle) setState(OperationState::Cancelled);
    if (open_watcher_.isRunning()) open_watcher_.waitForFinished();
    if (save_watcher_.isRunning()) save_watcher_.waitForFinished();
    map_controller_.waitForOperation();
    global_data_.stopAndWait();
    chunk_editor_.setLoader(nullptr);
    voxel_view_.setLoader(nullptr);
    if (loader_) {
        map_controller_.setLevelLoader(nullptr);
        if (terrain_data_) terrain_data_->setLoader(nullptr);
        loader_->close();
        loader_.reset();
    }
    if (opening_loader_) {
        opening_loader_->close();
        opening_loader_.reset();
    }
    emit closed();
}

void WorldSession::onOpenFinished() {
    if (closing_) return;
    if (!open_watcher_.result()) {
        opening_loader_.reset();
        setErrorCode(QStringLiteral("database-open-failed"));
        setState(OperationState::Failed);
        return;
    }
    loader_ = std::move(opening_loader_);
    chunk_editor_.setLoader(loader_.get());
    voxel_view_.setLoader(loader_.get());
    if (terrain_data_) terrain_data_->setLoader(loader_.get());
    // Install the terrain provider first. MapController emits worldStateChanged
    // when its loader is attached, which is the signal used by QML terrain
    // items to issue their initial viewport requests.
    map_controller_.setLevelLoader(loader_.get());
    connect(loader_.get(), &AsyncLevelLoader::dirtyChanged, this, &WorldSession::refreshDirty);
    if (setting::current().loadGlobalData) {
        auto* level_root = loader_->level().dat().root();
        global_data_.start(loader_.get(), level_root);
    }
    if (!loader_->preloadAllChunkCoords()) {
        setIndexLoading(false);
    }
    setProgress(100);
    setState(OperationState::Succeeded);
    emit opened();
}

bool WorldSession::commit() {
    if (closing_ || saving() || !loader_ || !loader_->isOpen() || map_controller_.operationRunning()) return false;

    if (chunk_editor_.dirty()) {
        emit saveFailed(tr("Save chunk editor changes before saving the world."));
        refreshDirty();
        return true;
    }
    pending_global_modifies_ = global_data_.collectLevelDbChanges();
    pending_level_dat_ = global_data_.levelDatSnapshot();
    if (!loader_->isDirty() && pending_global_modifies_.empty() && !pending_level_dat_) {
        refreshDirty();
        return true;
    }

    setSaving(true);
    auto* loader = loader_.get();
    const auto global_modifies = pending_global_modifies_;
    auto* level_dat = pending_level_dat_.get();
    save_watcher_.setFuture(
        QtConcurrent::run([loader, global_modifies, level_dat] { return loader->commitEdits(global_modifies, level_dat); }));
    return true;
}

void WorldSession::onSaveFinished() {
    if (closing_) return;
    const bool success = save_watcher_.result();
    setSaving(false);

    if (success) {
        global_data_.markSaved();
        pending_level_dat_.reset();
        pending_global_modifies_.clear();
        refreshDirty();
        emit saved();
        return;
    }

    pending_level_dat_.reset();
    pending_global_modifies_.clear();
    refreshDirty();
    emit saveFailed(tr("Saving the world failed."));
}

void WorldSession::refreshDirty() {
    if (closing_) return;
    const bool dirty = (loader_ && loader_->isDirty()) || global_data_.isDirty() || chunk_editor_.dirty();
    setDirty(dirty);
}

void WorldSession::setState(OperationState state) {
    if (state_ == state) return;
    state_ = state;
    emit stateChanged();
    emit readyChanged();
    emit canSaveChanged();
}

void WorldSession::setProgress(int progress) {
    progress = qBound(0, progress, 100);
    if (progress_ == progress) return;
    progress_ = progress;
    emit progressChanged();
}

void WorldSession::setIndexLoading(bool loading) {
    if (index_loading_ == loading) return;
    index_loading_ = loading;
    emit indexLoadingChanged();
    emit readyChanged();
    emit canSaveChanged();
}

void WorldSession::setIndexProgress(qulonglong scannedKeys, qulonglong chunks) {
    if (scanned_keys_ == scannedKeys && indexed_chunks_ == chunks) return;
    scanned_keys_ = scannedKeys;
    indexed_chunks_ = chunks;
    emit indexProgressChanged();
}

void WorldSession::setErrorCode(const QString& code) {
    if (error_code_ == code) return;
    error_code_ = code;
    emit errorCodeChanged();
}
