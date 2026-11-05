#ifndef BEDROCKMAP_BLOCKVOLUMECONTROLLER_H
#define BEDROCKMAP_BLOCKVOLUMECONTROLLER_H

#include <QFutureWatcher>
#include <QRegion>
#include <QUrl>
#include <memory>

#include "controller/blockvolumesource.h"
#include "controller/nbtdocumentcontroller.h"
#include "controller/voxelscenecontroller.h"

class AsyncLevelLoader;
class AppContext;

/// Shared controller for file-backed and world-backed block volumes.
class BlockVolumeController final : public VoxelSceneController {
    Q_OBJECT
    Q_PROPERTY(NbtDocumentController* document READ document CONSTANT)
    Q_PROPERTY(int voxelRevision READ voxelRevision NOTIFY voxelChanged)
    Q_PROPERTY(bool importPreviewActive READ importPreviewActive NOTIFY importPreviewChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(QVector3D importSize READ importSize NOTIFY importPreviewChanged)
    Q_PROPERTY(bool copying READ copying NOTIFY copyingChanged)

   public:
    explicit BlockVolumeController(std::unique_ptr<BlockVolumeSource> source, AppContext& context, QObject* parent = nullptr);
    ~BlockVolumeController() override;

    void load(const QUrl& url);
    void loadBytes(const QByteArray& raw);
    void clearPreview();
    Q_INVOKABLE void openArea(int minChunkX, int minChunkZ, int maxChunkX, int maxChunkZ, int dimension);
    Q_INVOKABLE bool importStructure(const QUrl& url);
    Q_INVOKABLE bool pasteStructure();
    Q_INVOKABLE bool copySelection(const QVector3D& minimum, const QVector3D& maximum, bool copyEntities, bool useNewFormat);
    Q_INVOKABLE bool commitImport(const QVector3D& minimum, const QVector3D& maximum, bool importEntities);
    Q_INVOKABLE void cancelImport();
    void setLoader(AsyncLevelLoader* loader);

    [[nodiscard]] NbtDocumentController* document() const { return document_; }
    [[nodiscard]] bool loading() const override { return loading_; }
    [[nodiscard]] QString errorMessage() const override { return error_; }
    [[nodiscard]] int voxelProgress() const override { return voxel_progress_; }
    [[nodiscard]] int voxelRevision() const { return voxel_revision_; }
    [[nodiscard]] bool importPreviewActive() const { return import_scene_ != nullptr; }
    [[nodiscard]] bool importing() const { return importing_; }
    [[nodiscard]] bool copying() const { return copying_; }
    [[nodiscard]] QVector3D importSize() const { return import_scene_ ? import_scene_->modelSize : QVector3D{}; }
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> voxelScene() const override { return scene_; }
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> voxelOverlayScene() const override { return import_scene_; }
    [[nodiscard]] bool blockVolumeEditingEnabled() const override { return source_ && source_->blockEditingEnabled(); }
    [[nodiscard]] bool deleting() const override { return deleting_; }
    bool deleteBlockVolumeAsync(const QVector3D& minimum, const QVector3D& maximum, bool deleteEntities) override;

   signals:
    void loadFailed(const QString& message);
    void importFailed(const QString& message);
    void importFinished();
    void copyFailed(const QString& message);
    void copyFinished();
    void importPreviewChanged();
    void importingChanged();
    void copyingChanged();
    void regionsChanged(const QRegion& region, int dimension);

   protected:
    [[nodiscard]] StructureExportTask structureExportTask(const bl::block_box& localBounds, bool exportEntities,
                                                          bool useNewFormat) const override;

   private:
    void startLoad(bool allowWhileDeleting = false);
    bool beginImport();
    void startImportLoad();
    void finishLoad();
    void finishImportLoad();
    void finishImport();
    void finishCopy();
    void finishDelete();
    void clearImport();
    void setVoxelProgress(int progress);

    std::unique_ptr<BlockVolumeSource> source_;
    NbtDocumentController* document_;
    AppContext& context_;
    QFutureWatcher<std::shared_ptr<BlockVolumeLoadResult>> load_watcher_;
    QFutureWatcher<std::shared_ptr<BlockVolumeLoadResult>> import_load_watcher_;
    QFutureWatcher<BlockVolumeMutationResult> import_watcher_;
    QFutureWatcher<QByteArray> copy_watcher_;
    QFutureWatcher<BlockVolumeMutationResult> delete_watcher_;
    std::shared_ptr<const VoxelSceneData> scene_;
    std::shared_ptr<const VoxelSceneData> import_scene_;
    QString error_;
    int voxel_progress_{0};
    int voxel_revision_{0};
    quint64 import_generation_{0};
    bool loading_{false};
    bool importing_{false};
    bool copying_{false};
    bool deleting_{false};
};

#endif  // BEDROCKMAP_BLOCKVOLUMECONTROLLER_H
