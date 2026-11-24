#ifndef BEDROCKMAP_BLOCKVOLUMECONTROLLER_H
#define BEDROCKMAP_BLOCKVOLUMECONTROLLER_H

#include <QFutureWatcher>
#include <QRegion>
#include <QUrl>
#include <QVector3D>
#include <functional>
#include <memory>
#include <optional>

#include "controller/blockvolumesource.h"
#include "controller/voxelscenecontroller.h"

class AsyncLevelLoader;
class AppContext;

/// Shared controller for file-backed and world-backed block volumes.
class BlockVolumeController final : public VoxelSceneController {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
    Q_PROPERTY(bool importPreviewActive READ importPreviewActive NOTIFY importPreviewChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY operationChanged)
    Q_PROPERTY(QVector3D importSize READ importSize NOTIFY importPreviewChanged)
    Q_PROPERTY(bool copying READ copying NOTIFY operationChanged)
    Q_PROPERTY(bool deleting READ deleting NOTIFY operationChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY operationChanged)
    Q_PROPERTY(bool blockVolumeEditingEnabled READ blockVolumeEditingEnabled CONSTANT)

   public:
    explicit BlockVolumeController(std::unique_ptr<BlockVolumeSource> source, AppContext& context, QObject* parent = nullptr);
    ~BlockVolumeController() override;

    /// A request made while loading replaces the running one.
    void load(BlockVolumeRequest request);
    void clearPreview();
    Q_INVOKABLE void openArea(int minChunkX, int minChunkZ, int maxChunkX, int maxChunkZ, int dimension);
    void openArea(const ChunkArea& area);
    Q_INVOKABLE bool importStructure(const QUrl& url);
    Q_INVOKABLE bool pasteStructure();
    Q_INVOKABLE bool copySelection(const QVector3D& minimum, const QVector3D& maximum, bool copyEntities, bool useNewFormat,
                                   bool includeAir);
    Q_INVOKABLE bool commitImport(const QVector3D& minimum, const QVector3D& maximum, bool importEntities, bool replaceNonAirBlocks);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE bool exportMcstructure(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, bool exportEntities,
                                       bool useNewFormat, bool includeAir);
    bool copySelection(const QVector3D& minimum, const QVector3D& maximum, const StructureExportOptions& options);
    bool exportMcstructure(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, const StructureExportOptions& options);
    bool commitImport(const QVector3D& minimum, const QVector3D& maximum, const StructureImportOptions& options);
    Q_INVOKABLE bool exportGlb(const QUrl& url, const QVector3D& minimum, const QVector3D& maximum, bool compressMesh);
    Q_INVOKABLE bool deleteBlockVolumeAsync(const QVector3D& minimum, const QVector3D& maximum, bool deleteEntities);
    void setLoader(AsyncLevelLoader* loader);

    [[nodiscard]] std::shared_ptr<const BlockVolume> volume() const { return volume_; }
    [[nodiscard]] bool loading() const override { return operation_ == Operation::Loading; }
    [[nodiscard]] QString errorMessage() const override { return error_; }
    [[nodiscard]] bool busy() const { return operation_ != Operation::None; }
    [[nodiscard]] bool importPreviewActive() const { return import_volume_ != nullptr; }
    [[nodiscard]] bool importing() const { return operation_ == Operation::ImportLoading || operation_ == Operation::Importing; }
    [[nodiscard]] bool copying() const { return operation_ == Operation::Copying; }
    [[nodiscard]] bool deleting() const { return operation_ == Operation::Deleting; }
    [[nodiscard]] bool exporting() const { return operation_ == Operation::Exporting; }
    [[nodiscard]] QVector3D importSize() const { return import_volume_ ? import_volume_->scene->modelSize : QVector3D{}; }
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> voxelScene() const override { return volume_ ? volume_->scene : nullptr; }
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> voxelOverlayScene() const override {
        return import_volume_ ? import_volume_->scene : nullptr;
    }
    [[nodiscard]] bool blockVolumeEditingEnabled() const { return source_ && source_->blockEditingEnabled(); }

   signals:
    void operationChanged();
    void loadFailed(const QString& message);
    void importFailed(const QString& message);
    void importFinished();
    void copyFailed(const QString& message);
    void copyFinished();
    void deleteFailed(const QString& message);
    void exportFinished(const QUrl& url, const QString& error);
    void importPreviewChanged();
    void regionsChanged(const QRegion& region, int dimension);

   private:
    /// Operations are mutually exclusive; loads may be superseded by a newer request.
    enum class Operation { None, Loading, ImportLoading, Importing, Copying, Deleting, Exporting };
    /// Runs on a worker and returns the completion step for the GUI thread.
    using Task = std::function<std::function<void()>()>;
    /// Runs on the GUI thread when a task throws; detail is the exception text, if any.
    using Failure = std::function<void(const QString& detail)>;
    /// Runs on the worker after structure bytes are generated and returns the GUI completion step.
    using StructureExportCompletion = std::function<std::function<void()>(QByteArray)>;

    void run(Operation operation, Task task, Failure onFailure);
    bool beginStructureExport(Operation operation, const QVector3D& minimum, const QVector3D& maximum,
                              const StructureExportOptions& options, StructureExportCompletion completion, Failure onFailure);
    void finishOperation();
    void setOperation(Operation operation);
    void startLoad(BlockVolumeRequest request, bool keepScene);
    void applyLoad(const BlockVolumeLoadResult& result);
    bool beginImport(BlockVolumeRequest request);
    void applyImportLoad(const BlockVolumeLoadResult& result);
    void applyMutation(const BlockVolumeMutationResult& result, const QString& failure, bool import);
    void clearImport();
    [[nodiscard]] std::optional<bl::block_box> selectionBounds(const QVector3D& minimum, const QVector3D& maximum) const;
    [[nodiscard]] static QUrl withDefaultSuffix(const QUrl& url, const QString& suffix);

    std::unique_ptr<BlockVolumeSource> source_;
    AppContext& context_;
    QFutureWatcher<std::function<void()>> watcher_;
    Operation operation_{Operation::None};
    /// Runs after the current load finishes, replacing its result.
    std::function<void()> pending_load_;
    /// The last loaded request, reused to rebuild the volume after an edit.
    BlockVolumeRequest request_;
    std::shared_ptr<const BlockVolume> volume_;
    std::shared_ptr<const BlockVolume> import_volume_;
    QString error_;
};

#endif  // BEDROCKMAP_BLOCKVOLUMECONTROLLER_H
