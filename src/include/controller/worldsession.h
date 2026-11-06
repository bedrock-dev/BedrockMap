#ifndef BEDROCKMAP_WORLDSESSION_H
#define BEDROCKMAP_WORLDSESSION_H

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include <unordered_map>

#include "controller/blockvolumecontroller.h"
#include "controller/chunkeditorcontroller.h"
#include "controller/globaldatacontroller.h"
#include "controller/mapcontroller.h"
#include "controller/workspacesession.h"
#include "model/mapviewstate.h"
#include "render/terraindataprovider.h"

class AsyncLevelLoader;
class AppContext;
namespace bl::nbt {
    class compound_tag;
}

/// Owns all data and map state belonging to one opened world.
class WorldSession final : public WorkspaceSession {
    Q_OBJECT
    Q_PROPERTY(MapController* mapController READ mapController CONSTANT)
    Q_PROPERTY(ChunkEditorController* chunkEditor READ chunkEditor CONSTANT)
    Q_PROPERTY(BlockVolumeController* voxelView READ voxelView CONSTANT)
    Q_PROPERTY(TerrainDataProvider* terrainData READ terrainData CONSTANT)
    Q_PROPERTY(GlobalDataController* globalData READ globalData CONSTANT)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool indexLoading READ indexLoading NOTIFY indexLoadingChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged)
    Q_PROPERTY(qulonglong scannedKeys READ scannedKeys NOTIFY indexProgressChanged)
    Q_PROPERTY(qulonglong indexedChunks READ indexedChunks NOTIFY indexProgressChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorCodeChanged)
    Q_PROPERTY(int workspaceSection READ workspaceSection WRITE setWorkspaceSection NOTIFY workspaceSectionChanged)
    Q_PROPERTY(QString workspaceRoute READ workspaceRoute WRITE setWorkspaceRoute NOTIFY workspaceRouteChanged)

   public:
    explicit WorldSession(QString path, QString title, AppContext& context, QObject* parent = nullptr);
    ~WorldSession() override;

    MapController* mapController() { return &map_controller_; }
    ChunkEditorController* chunkEditor() { return &chunk_editor_; }
    BlockVolumeController* voxelView() { return &voxel_view_; }
    TerrainDataProvider* terrainData() const { return terrain_data_.get(); }
    GlobalDataController* globalData() { return &global_data_; }
    int state() const { return static_cast<int>(state_); }
    int progress() const { return progress_; }
    bool indexLoading() const { return index_loading_; }
    bool failed() const { return state_ == State::Failed; }
    bool ready() const { return state_ == State::Succeeded && !index_loading_; }
    bool canSave() const {
        return ready() && !index_loading_ && !saving() && !voxel_view_.loading() && !voxel_view_.deleting() && !voxel_view_.importing() &&
               !voxel_view_.importPreviewActive() && !voxel_view_.copying();
    }
    qulonglong scannedKeys() const { return scanned_keys_; }
    qulonglong indexedChunks() const { return indexed_chunks_; }
    QString errorCode() const { return error_code_; }
    int workspaceSection() const { return workspace_section_; }
    QString workspaceRoute() const { return workspace_route_; }
    AsyncLevelLoader* loader() const { return loader_.get(); }
    [[nodiscard]] bool titleBarSaveVisible() const override { return true; }
    [[nodiscard]] bool titleBarSaveEnabled() const override { return canSave(); }
    [[nodiscard]] bool titleBarNavigationVisible() const override { return true; }
    [[nodiscard]] QVariantList titleBarNavigationSections() const override { return global_data_.sections(); }
    [[nodiscard]] int titleBarNavigationIndex() const override { return workspace_section_; }
    [[nodiscard]] bool saveCompletesAsynchronously() const override { return true; }

    void open() override;
    void close() override;
    [[nodiscard]] bool commit() override;
    void ensureTerrainData();
    void setActive(bool active) override;
    void setWorkspaceSection(int section);
    void setWorkspaceRoute(const QString& route);
    void setTitleBarNavigationIndex(int index) override { setWorkspaceSection(index); }

   signals:
    void stateChanged();
    void progressChanged();
    void indexLoadingChanged();
    void readyChanged();
    void canSaveChanged();
    void indexProgressChanged();
    void errorCodeChanged();
    void workspaceSectionChanged();
    void workspaceRouteChanged();
    void opened();
    void closed();

   private:
    enum class State { Idle, Running, Succeeded, Failed, Cancelled };
    enum class OpenResult { Opened, Failed, WriteAccessDenied };
    void setState(State state);
    void setProgress(int progress);
    void setIndexLoading(bool loading);
    void setIndexProgress(qulonglong scannedKeys, qulonglong chunks);
    void setErrorCode(const QString& code);
    void refreshDirty();
    void onSaveFinished();
    void onOpenFinished();

    AppContext& context_;
    MapViewState map_view_state_;
    MapController map_controller_;
    ChunkEditorController chunk_editor_;
    BlockVolumeController voxel_view_;
    std::unique_ptr<TerrainDataProvider> terrain_data_;
    GlobalDataController global_data_;
    std::unique_ptr<AsyncLevelLoader> loader_;
    std::unique_ptr<AsyncLevelLoader> opening_loader_;
    QFutureWatcher<OpenResult> open_watcher_;
    QFutureWatcher<bool> save_watcher_;
    std::unordered_map<std::string, std::string> pending_global_modifies_;
    std::unique_ptr<bl::nbt::compound_tag> pending_level_dat_;
    State state_{State::Idle};
    int progress_{0};
    bool index_loading_{false};
    qulonglong scanned_keys_{0};
    qulonglong indexed_chunks_{0};
    QString error_code_;
    int workspace_section_{0};
    QString workspace_route_{QStringLiteral("root")};
    bool closing_{false};
};

#endif
